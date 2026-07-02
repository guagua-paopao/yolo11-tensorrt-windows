#include "server/inference_worker.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <opencv2/opencv.hpp>

#include "postprocess.h"
#include "server/image_codec.h"
#include "server/result_serializer.h"

namespace yolo11_server {

    namespace {

        RedisSection makeWorkerRedisConfig(const RedisSection& base, const std::string& consumer_name) {
            RedisSection config = base;
            config.consumer_name = consumer_name;
            return config;
        }

        std::string processIdString() {
#ifdef _WIN32
            return std::to_string(static_cast<unsigned long long>(::GetCurrentProcessId()));
#else
            return std::to_string(static_cast<long long>(::getpid()));
#endif
        }

        std::string hostNameString() {
            char buffer[256] = { 0 };
#ifdef _WIN32
            DWORD size = static_cast<DWORD>(sizeof(buffer));
            if (::GetComputerNameA(buffer, &size)) {
                return std::string(buffer, size);
            }
#else
            if (::gethostname(buffer, sizeof(buffer) - 1) == 0) {
                return std::string(buffer);
            }
#endif
            return "unknown";
        }

    }  // namespace

    InferenceWorker::InferenceWorker(int worker_id, const AppConfig& config, const std::string& consumer_name)
        : worker_id_(worker_id),
        config_(config),
        redis_queue_(makeWorkerRedisConfig(config.redis, consumer_name)) {
        config_.redis.consumer_name = consumer_name;
        start_time_ms_ = nowMs();
    }

    InferenceWorker::~InferenceWorker() noexcept {
        stop();
        releaseDetectorNoexcept();
    }

    bool InferenceWorker::start() {
        if (running_.load()) {
            return true;
        }

        if (!config_.redis.enabled) {
            spdlog::error("InferenceWorker requires redis.enabled=true.");
            return false;
        }

        std::string error;
        if (!redis_queue_.connect(error)) {
            spdlog::error("Worker " + std::to_string(worker_id_) + " failed to connect Redis: " + error);
            return false;
        }

        yolo11::DetectorConfig detector_config;
        detector_config.engine_path = config_.model.engine_path;
        detector_config.gpu_id = config_.model.gpu_id;
        detector_config.use_gpu_postprocess = config_.model.use_gpu_postprocess;

        spdlog::info("Worker " + std::to_string(worker_id_) +
            " loading TensorRT engine: " + detector_config.engine_path +
            ", consumer=" + config_.redis.consumer_name);

        if (!detector_.init(detector_config)) {
            spdlog::error("Worker " + std::to_string(worker_id_) + " failed to initialize detector.");
            return false;
        }
        detector_initialized_ = true;

        std::string label_error;
        if (!label_map_.loadFromFile(config_.model.labels_path, label_error)) {
            spdlog::warn("Worker {} failed to load labels_path='{}': {}. class_name will fall back to class_<id>.",
                worker_id_, config_.model.labels_path, label_error);
        }
        else {
            spdlog::info("Worker {} loaded {} labels from {}", worker_id_, label_map_.size(), label_map_.sourcePath());
        }

        setWorkerStatus("idle");
        running_ = true;

        if (config_.worker.heartbeat_enabled) {
            heartbeat_thread_ = std::thread([this]() {
                heartbeatLoop();
                });
        }

        thread_ = std::thread([this]() {
            loop();
            });

        return true;
    }

    void InferenceWorker::stop() noexcept {
        try {
            running_.store(false);

            if (heartbeat_thread_.joinable()) {
                if (heartbeat_thread_.get_id() == std::this_thread::get_id()) {
                    heartbeat_thread_.detach();
                }
                else {
                    heartbeat_thread_.join();
                }
            }

            if (thread_.joinable()) {
                // stop() should normally be called by the owner thread. This guard prevents
                // std::terminate if stop() is accidentally called from inside the worker thread.
                if (thread_.get_id() == std::this_thread::get_id()) {
                    thread_.detach();
                }
                else {
                    thread_.join();
                }
            }
        }
        catch (const std::exception& e) {
            spdlog::error("InferenceWorker stop exception: " + std::string(e.what()));
        }
        catch (...) {
            spdlog::error("InferenceWorker stop unknown exception.");
        }
    }

    void InferenceWorker::releaseDetectorNoexcept() noexcept {
        if (!detector_initialized_) {
            return;
        }

        try {
            detector_.release();
            detector_initialized_ = false;
        }
        catch (const std::exception& e) {
            spdlog::error("Detector release exception: " + std::string(e.what()));
            detector_initialized_ = false;
        }
        catch (...) {
            spdlog::error("Detector release unknown exception.");
            detector_initialized_ = false;
        }
    }

    bool InferenceWorker::running() const {
        return running_.load();
    }

    std::string InferenceWorker::consumerName() const {
        return config_.redis.consumer_name;
    }

    void InferenceWorker::loop() {
        spdlog::info("InferenceWorker started: id=" + std::to_string(worker_id_) +
            ", consumer=" + config_.redis.consumer_name +
            ", backend=redis_stream");
        writeHeartbeatNoexcept();

        while (running_.load()) {
            RedisTask task;
            std::string error;

            // Phase 5: recover stale pending messages first, then read new messages.
            if (redis_queue_.claimPendingTask(task, error)) {
                spdlog::info("Worker " + std::to_string(worker_id_) +
                    " reclaimed pending task: task_id=" + task.task_id +
                    ", stream_id=" + task.stream_id +
                    ", consumer=" + config_.redis.consumer_name);
                processRedisTask(task);
                continue;
            }
            if (!error.empty()) {
                spdlog::error("Worker " + std::to_string(worker_id_) +
                    " Redis claimPendingTask failed: " + error);
            }

            error.clear();
            if (!redis_queue_.popTask(task, error)) {
                if (!error.empty()) {
                    spdlog::error("Worker " + std::to_string(worker_id_) +
                        " Redis popTask failed: " + error);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                continue;
            }

            processRedisTask(task);
        }

        setWorkerStatus("stopping");
        writeHeartbeatNoexcept();
        spdlog::info("InferenceWorker stopped: id=" + std::to_string(worker_id_) +
            ", consumer=" + config_.redis.consumer_name);
    }

    void InferenceWorker::processRedisTask(const RedisTask& task) {
        const long long start_time_ms = nowMs();
        const long long queue_wait_ms = task.create_time_ms > 0 ? std::max(0LL, start_time_ms - task.create_time_ms) : 0LL;

        setWorkerStatus("running", task.task_id);
        writeHeartbeatNoexcept();

        std::string redis_error;
        if (!redis_queue_.markRunning(task.task_id, start_time_ms, worker_id_, config_.redis.consumer_name, redis_error)) {
            spdlog::error("Worker " + std::to_string(worker_id_) +
                " Redis markRunning failed: task_id=" + task.task_id +
                ", error=" + redis_error);
        }

        try {
            cv::Mat image;
            if (!task.input_image_key.empty()) {
                std::string input_bytes;
                std::string get_error;
                if (!redis_queue_.getBinaryValue(task.input_image_key, input_bytes, get_error)) {
                    throw std::runtime_error("worker failed to read input image bytes from Redis: " + get_error);
                }
                image = ImageCodec::decodeImageBytes(input_bytes);
                if (image.empty()) {
                    throw std::runtime_error("worker failed to decode input image bytes from Redis key: " + task.input_image_key);
                }
            }
            else {
                image = cv::imread(task.input_image_path, cv::IMREAD_COLOR);
                if (image.empty()) {
                    throw std::runtime_error("worker failed to read input image: " + task.input_image_path);
                }
            }

            std::vector<Detection> detections;
            cv::Mat result_image;
            auto t0 = std::chrono::steady_clock::now();

            detections = detector_.infer(image);
            if (config_.output.save_result_image) {
                result_image = detector_.draw(image, detections);
            }

            auto t1 = std::chrono::steady_clock::now();
            const double infer_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::string result_image_key = task.result_image_key;
            std::string result_image_filename;
            std::string result_image_path;
            std::string result_image_url;

            if (config_.output.save_result_image) {
                if (result_image.empty()) {
                    throw std::runtime_error("result image is empty after detector.draw");
                }

                if (!result_image_key.empty()) {
                    const std::string result_bytes = ImageCodec::encodeJpegBytes(result_image, config_.output.jpeg_quality);
                    if (result_bytes.empty()) {
                        throw std::runtime_error("failed to encode result image bytes");
                    }
                    if (config_.redis.max_result_image_bytes > 0 &&
                        static_cast<long long>(result_bytes.size()) > config_.redis.max_result_image_bytes) {
                        throw std::runtime_error(
                            "encoded result image is too large: " + std::to_string(result_bytes.size()) +
                            " bytes, max_result_image_bytes=" + std::to_string(config_.redis.max_result_image_bytes)
                        );
                    }

                    std::string set_error;
                    if (!redis_queue_.setBinaryValueWithTtl(
                        result_image_key,
                        result_bytes,
                        config_.redis.result_image_ttl_seconds,
                        set_error)) {
                        throw std::runtime_error("failed to store result image bytes to Redis: " + set_error);
                    }
                    result_image_url = "/api/v1/result/" + task.task_id + "/image";
                }

                // Local output is kept as a debugging/legacy fallback.
                std::filesystem::create_directories(config_.output.output_dir);
                result_image_filename = makeResultImageFilename(task.task_id);
                result_image_path = makeResultImagePath(result_image_filename);
                std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, config_.output.jpeg_quality };
                if (!cv::imwrite(result_image_path, result_image, params)) {
                    result_image_path.clear();
                    result_image_filename.clear();
                }

                if (result_image_url.empty() && !result_image_filename.empty()) {
                    result_image_url = "/api/v1/image/" + result_image_filename;
                }
            }

            const long long finish_time_ms = nowMs();
            const long long total_ms = task.create_time_ms > 0 ? std::max(0LL, finish_time_ms - task.create_time_ms) : 0LL;

            nlohmann::json body;
            body["success"] = true;
            body["task_id"] = task.task_id;
            body["status"] = "done";
            body["queue_backend"] = "redis_stream";
            body["worker_id"] = worker_id_;
            body["consumer_name"] = config_.redis.consumer_name;
            body["model_type"] = config_.model.type;
            body["image"] = {
                {"width", image.cols},
                {"height", image.rows},
                {"channels", image.channels()}
            };
            body["bbox_coordinate_system"] = "original_image_pixels";
            body["bbox_format"] = "xywh_and_xyxy";
            body["num_detections"] = detections.size();
            body["queue_wait_ms"] = queue_wait_ms;
            body["inference_ms"] = infer_ms;
            body["total_ms"] = total_ms;
            body["create_time_ms"] = task.create_time_ms;
            body["start_time_ms"] = start_time_ms;
            body["finish_time_ms"] = finish_time_ms;
            body["detections"] = ResultSerializer::detectionsToJson(detections, image, label_map_, false);
            if (!result_image_key.empty()) {
                body["result_image_key"] = result_image_key;
            }
            if (!result_image_url.empty()) {
                if (!result_image_filename.empty()) {
                    body["result_image_filename"] = result_image_filename;
                }
                body["result_image_url"] = result_image_url;
            }

            const std::string result_json_text = body.dump(4);

            redis_error.clear();
            const bool mark_done_ok = redis_queue_.markDone(
                task.task_id,
                result_json_text,
                result_image_key,
                result_image_path,
                result_image_filename,
                finish_time_ms,
                queue_wait_ms,
                infer_ms,
                total_ms,
                worker_id_,
                config_.redis.consumer_name,
                redis_error);

            if (!mark_done_ok) {
                setLastError("markDone failed: " + redis_error);
                spdlog::error("Worker " + std::to_string(worker_id_) +
                    " Redis markDone failed, keep task pending for reclaim: task_id=" + task.task_id +
                    ", stream_id=" + task.stream_id +
                    ", error=" + redis_error);
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                return;
            }

            redis_error.clear();
            const bool ack_ok = redis_queue_.ackTask(task.stream_id, redis_error);
            if (!ack_ok) {
                setLastError("ackTask failed: " + redis_error);
                spdlog::error("Worker " + std::to_string(worker_id_) +
                    " Redis ackTask failed after markDone, keep input image for reclaim: stream_id=" + task.stream_id +
                    ", error=" + redis_error);
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                return;
            }

            if (config_.redis.delete_input_after_done && !task.input_image_key.empty()) {
                std::string del_error;
                if (!redis_queue_.deleteKey(task.input_image_key, del_error) && !del_error.empty()) {
                    spdlog::error("Worker " + std::to_string(worker_id_) +
                        " failed to delete input image key after done: key=" + task.input_image_key +
                        ", error=" + del_error);
                }
            }

            processed_count_.fetch_add(1);
            setLastError("");
            setWorkerStatus("idle");
            writeHeartbeatNoexcept();

            if (config_.worker.log_task_done) {
                std::ostringstream oss;
                oss << "Worker " << worker_id_
                    << " task done: task_id=" << task.task_id
                    << ", detections=" << detections.size()
                    << ", queue_wait_ms=" << queue_wait_ms
                    << ", infer_ms=" << infer_ms
                    << ", total_ms=" << total_ms
                    << ", consumer=" << config_.redis.consumer_name;

                spdlog::info(oss.str());
            }
        }
        catch (const std::exception& e) {
            const long long finish_time_ms = nowMs();
            const long long total_ms = task.create_time_ms > 0 ? std::max(0LL, finish_time_ms - task.create_time_ms) : 0LL;

            failed_count_.fetch_add(1);
            setLastError(e.what());

            redis_error.clear();
            const bool mark_failed_ok = redis_queue_.markFailed(
                task.task_id,
                e.what(),
                finish_time_ms,
                queue_wait_ms,
                total_ms,
                worker_id_,
                config_.redis.consumer_name,
                redis_error);

            if (!mark_failed_ok) {
                spdlog::error("Worker " + std::to_string(worker_id_) +
                    " Redis markFailed failed, keep task pending for reclaim: task_id=" + task.task_id +
                    ", stream_id=" + task.stream_id +
                    ", error=" + redis_error);
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                return;
            }

            redis_error.clear();
            const bool ack_ok = redis_queue_.ackTask(task.stream_id, redis_error);
            if (!ack_ok) {
                spdlog::error("Worker " + std::to_string(worker_id_) +
                    " Redis ackTask failed after markFailed, keep input image for reclaim: stream_id=" + task.stream_id +
                    ", error=" + redis_error);
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                return;
            }

            if (config_.redis.delete_input_after_done && !task.input_image_key.empty()) {
                std::string del_error;
                if (!redis_queue_.deleteKey(task.input_image_key, del_error) && !del_error.empty()) {
                    spdlog::error("Worker " + std::to_string(worker_id_) +
                        " failed to delete input image key after failed task: key=" + task.input_image_key +
                        ", error=" + del_error);
                }
            }

            setWorkerStatus("idle");
            writeHeartbeatNoexcept();

            spdlog::error("Worker " + std::to_string(worker_id_) +
                " task failed: task_id=" + task.task_id +
                ", total_ms=" + std::to_string(total_ms) +
                ", error=" + e.what());
        }
    }

    void InferenceWorker::heartbeatLoop() noexcept {
        while (running_.load()) {
            writeHeartbeatNoexcept();
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.worker.heartbeat_interval_ms));
        }
        writeHeartbeatNoexcept();
    }

    void InferenceWorker::writeHeartbeatNoexcept() noexcept {
        if (!config_.worker.heartbeat_enabled || !config_.redis.enabled) {
            return;
        }

        try {
            WorkerHeartbeatRecord heartbeat;
            heartbeat.consumer_name = config_.redis.consumer_name;
            heartbeat.pid = processIdString();
            heartbeat.host = hostNameString();
            heartbeat.worker_id = worker_id_;
            heartbeat.gpu_id = config_.model.gpu_id;
            heartbeat.model_type = config_.model.type;
            heartbeat.processed_count = processed_count_.load();
            heartbeat.failed_count = failed_count_.load();
            heartbeat.start_time_ms = start_time_ms_;
            heartbeat.last_heartbeat_ms = nowMs();

            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                heartbeat.status = worker_status_;
                heartbeat.current_task_id = current_task_id_;
                heartbeat.last_error = last_error_;
            }

            std::string error;
            if (!redis_queue_.writeWorkerHeartbeat(heartbeat, config_.worker.heartbeat_ttl_seconds, error)) {
                spdlog::error("Worker " + std::to_string(worker_id_) +
                    " heartbeat write failed: consumer=" + config_.redis.consumer_name +
                    ", error=" + error);
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Worker heartbeat exception: " + std::string(e.what()));
        }
        catch (...) {
            spdlog::error("Worker heartbeat unknown exception.");
        }
    }

    void InferenceWorker::setWorkerStatus(const std::string& status, const std::string& current_task_id) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        worker_status_ = status;
        current_task_id_ = current_task_id;
    }

    void InferenceWorker::setLastError(const std::string& error_message) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = error_message;
    }

    std::string InferenceWorker::makeResultImageFilename(const std::string& task_id) const {
        return task_id + "_result.jpg";
    }

    std::string InferenceWorker::makeResultImagePath(const std::string& filename) const {
        std::filesystem::path output_dir(config_.output.output_dir);
        return (output_dir / filename).string();
    }

    long long InferenceWorker::nowMs() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

}  // namespace yolo11_server
