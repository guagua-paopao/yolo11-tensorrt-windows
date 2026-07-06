#include "server/inference_worker.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include "postprocess.h"
#include "server/image_codec.h"

namespace yolo11_server {

    namespace {

        std::mutex g_log_mutex;

        void safeLog(const std::string& msg) {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            std::cout << msg << std::endl;
        }

        void safeError(const std::string& msg) {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            std::cerr << msg << std::endl;
        }

        RedisSection makeWorkerRedisConfig(const RedisSection& base, const std::string& consumer_name) {
            RedisSection config = base;
            config.consumer_name = consumer_name;
            return config;
        }

        const std::vector<std::string>& cocoClassNames() {
            static const std::vector<std::string> names = {
                "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
                "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog",
                "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
                "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
                "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
                "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich",
                "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
                "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote",
                "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator", "book",
                "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
            };
            return names;
        }

        std::string classNameFromId(int class_id) {
            const auto& names = cocoClassNames();
            if (class_id >= 0 && class_id < static_cast<int>(names.size())) {
                return names[class_id];
            }
            return "class_" + std::to_string(class_id);
        }

        bool rectTouchesImageBoundary(const cv::Rect& rect, const cv::Mat& image) {
            if (image.empty()) {
                return true;
            }
            if (rect.x <= 0 || rect.y <= 0) {
                return true;
            }
            if (rect.x + rect.width >= image.cols) {
                return true;
            }
            if (rect.y + rect.height >= image.rows) {
                return true;
            }
            return false;
        }

        nlohmann::json detectionsToJsonForImage(
            const std::vector<Detection>& detections,
            const cv::Mat& image
        ) {
            nlohmann::json arr = nlohmann::json::array();

            for (const auto& detection : detections) {
                cv::Mat image_for_rect = image;
                float bbox_for_rect[4] = {
                    detection.bbox[0],
                    detection.bbox[1],
                    detection.bbox[2],
                    detection.bbox[3]
                };

                cv::Rect rect = ::get_rect(image_for_rect, bbox_for_rect);

                const int x1 = rect.x;
                const int y1 = rect.y;
                const int w = rect.width;
                const int h = rect.height;
                const int x2 = rect.x + rect.width;
                const int y2 = rect.y + rect.height;
                const int class_id = static_cast<int>(detection.class_id);

                nlohmann::json item;
                item["class_id"] = class_id;
                item["class_name"] = classNameFromId(class_id);
                item["confidence"] = detection.conf;
                item["bbox"] = {
                    {"x", x1},
                    {"y", y1},
                    {"w", w},
                    {"h", h},
                    {"x1", x1},
                    {"y1", y1},
                    {"x2", x2},
                    {"y2", y2}
                };
                item["clipped"] = rectTouchesImageBoundary(rect, image);

                arr.push_back(item);
            }

            return arr;
        }

    }  // namespace

    InferenceWorker::InferenceWorker(int worker_id, const AppConfig& config, const std::string& consumer_name)
        : worker_id_(worker_id),
        config_(config),
        redis_queue_(makeWorkerRedisConfig(config.redis, consumer_name)) {
        config_.redis.consumer_name = consumer_name;
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
            safeError("InferenceWorker requires redis.enabled=true.");
            return false;
        }

        std::string error;
        if (!redis_queue_.connect(error)) {
            safeError("Worker " + std::to_string(worker_id_) + " failed to connect Redis: " + error);
            return false;
        }

        yolo11::DetectorConfig detector_config;
        detector_config.engine_path = config_.model.engine_path;
        detector_config.gpu_id = config_.model.gpu_id;
        detector_config.use_gpu_postprocess = config_.model.use_gpu_postprocess;

        safeLog("Worker " + std::to_string(worker_id_) +
            " loading TensorRT engine: " + detector_config.engine_path +
            ", consumer=" + config_.redis.consumer_name);

        if (!detector_.init(detector_config)) {
            safeError("Worker " + std::to_string(worker_id_) + " failed to initialize detector.");
            return false;
        }
        detector_initialized_ = true;

        running_ = true;
        thread_ = std::thread([this]() {
            loop();
            });

        return true;
    }

    void InferenceWorker::stop() noexcept {
        try {
            running_.store(false);

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
            safeError("InferenceWorker stop exception: " + std::string(e.what()));
        }
        catch (...) {
            safeError("InferenceWorker stop unknown exception.");
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
            safeError("Detector release exception: " + std::string(e.what()));
            detector_initialized_ = false;
        }
        catch (...) {
            safeError("Detector release unknown exception.");
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
        safeLog("InferenceWorker started: id=" + std::to_string(worker_id_) +
            ", consumer=" + config_.redis.consumer_name +
            ", backend=redis_stream");

        while (running_.load()) {
            RedisTask task;
            std::string error;

            // Phase 5: recover stale pending messages first, then read new messages.
            if (redis_queue_.claimPendingTask(task, error)) {
                safeLog("Worker " + std::to_string(worker_id_) +
                    " reclaimed pending task: task_id=" + task.task_id +
                    ", stream_id=" + task.stream_id +
                    ", consumer=" + config_.redis.consumer_name);
                processRedisTask(task);
                continue;
            }
            if (!error.empty()) {
                safeError("Worker " + std::to_string(worker_id_) +
                    " Redis claimPendingTask failed: " + error);
            }

            error.clear();
            if (!redis_queue_.popTask(task, error)) {
                if (!error.empty()) {
                    safeError("Worker " + std::to_string(worker_id_) +
                        " Redis popTask failed: " + error);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                continue;
            }

            processRedisTask(task);
        }

        safeLog("InferenceWorker stopped: id=" + std::to_string(worker_id_) +
            ", consumer=" + config_.redis.consumer_name);
    }

    void InferenceWorker::processRedisTask(const RedisTask& task) {
        const long long start_time_ms = nowMs();
        const long long queue_wait_ms = task.create_time_ms > 0 ? std::max(0LL, start_time_ms - task.create_time_ms) : 0LL;

        std::string redis_error;
        if (!redis_queue_.markRunning(task.task_id, start_time_ms, worker_id_, config_.redis.consumer_name, redis_error)) {
            safeError("Worker " + std::to_string(worker_id_) +
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
                    std::string set_error;
                    if (!redis_queue_.setBinaryValue(result_image_key, result_bytes, set_error)) {
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
            body["detections"] = detectionsToJsonForImage(detections, image);
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
            if (!redis_queue_.markDone(
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
                redis_error)) {
                safeError("Worker " + std::to_string(worker_id_) +
                    " Redis markDone failed: task_id=" + task.task_id +
                    ", error=" + redis_error);
            }

            redis_error.clear();
            if (!redis_queue_.ackTask(task.stream_id, redis_error)) {
                safeError("Worker " + std::to_string(worker_id_) +
                    " Redis ackTask failed: stream_id=" + task.stream_id +
                    ", error=" + redis_error);
            }

            if (config_.worker.log_task_done) {
                std::ostringstream oss;
                oss << "Worker " << worker_id_
                    << " task done: task_id=" << task.task_id
                    << ", detections=" << detections.size()
                    << ", queue_wait_ms=" << queue_wait_ms
                    << ", infer_ms=" << infer_ms
                    << ", total_ms=" << total_ms
                    << ", consumer=" << config_.redis.consumer_name;

                safeLog(oss.str());
            }
        }
        catch (const std::exception& e) {
            const long long finish_time_ms = nowMs();
            const long long total_ms = task.create_time_ms > 0 ? std::max(0LL, finish_time_ms - task.create_time_ms) : 0LL;

            redis_error.clear();
            if (!redis_queue_.markFailed(
                task.task_id,
                e.what(),
                finish_time_ms,
                queue_wait_ms,
                total_ms,
                worker_id_,
                config_.redis.consumer_name,
                redis_error)) {
                safeError("Worker " + std::to_string(worker_id_) +
                    " Redis markFailed failed: task_id=" + task.task_id +
                    ", error=" + redis_error);
            }

            redis_error.clear();
            if (!redis_queue_.ackTask(task.stream_id, redis_error)) {
                safeError("Worker " + std::to_string(worker_id_) +
                    " Redis ackTask failed after failed task: stream_id=" + task.stream_id +
                    ", error=" + redis_error);
            }

            safeError("Worker " + std::to_string(worker_id_) +
                " task failed: task_id=" + task.task_id +
                ", total_ms=" + std::to_string(total_ms) +
                ", error=" + e.what());
        }
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
