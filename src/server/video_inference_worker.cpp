#include "server/video_inference_worker.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>

namespace yolo11_server {

    namespace {

        RedisSection makeWorkerRedisConfig(const RedisSection& base, const std::string& consumer_name) {
            RedisSection config = base;
            config.consumer_name = consumer_name;
            return config;
        }

        std::string toLowerString(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
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

    VideoInferenceWorker::VideoInferenceWorker(int worker_id, const AppConfig& config, const std::string& consumer_name)
        : worker_id_(worker_id),
        config_(config),
        redis_queue_(makeWorkerRedisConfig(config.redis, consumer_name)) {
        config_.redis.consumer_name = consumer_name;
        start_time_ms_ = nowMs();
    }

    VideoInferenceWorker::~VideoInferenceWorker() noexcept {
        stop();
        releaseRunnerNoexcept();
    }

    bool VideoInferenceWorker::start() {
        if (running_.load()) {
            return true;
        }
        if (!config_.redis.enabled) {
            spdlog::error("VideoInferenceWorker requires redis.enabled=true.");
            return false;
        }
        if (!config_.video.enabled) {
            spdlog::error("VideoInferenceWorker requires video.enabled=true.");
            return false;
        }
        if (toLowerString(config_.model.type) != "detect") {
            spdlog::error("Phase 13 video worker only supports model.type=detect. Current model.type={}", config_.model.type);
            return false;
        }

        std::string error;
        if (!redis_queue_.connect(error)) {
            spdlog::error("Video worker {} failed to connect Redis: {}", worker_id_, error);
            return false;
        }

        if (!initModelRunner()) {
            spdlog::error("Video worker {} failed to initialize model runner.", worker_id_);
            return false;
        }
        runner_initialized_ = true;

        std::filesystem::create_directories(config_.video.output_dir);
        setWorkerStatus("idle");
        running_ = true;

        if (config_.worker.heartbeat_enabled) {
            heartbeat_thread_ = std::thread([this]() { heartbeatLoop(); });
        }
        thread_ = std::thread([this]() { loop(); });
        return true;
    }

    void VideoInferenceWorker::stop() noexcept {
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
                if (thread_.get_id() == std::this_thread::get_id()) {
                    thread_.detach();
                }
                else {
                    thread_.join();
                }
            }
        }
        catch (...) {
            spdlog::error("VideoInferenceWorker stop exception ignored.");
        }
    }

    bool VideoInferenceWorker::running() const {
        return running_.load();
    }

    std::string VideoInferenceWorker::consumerName() const {
        return config_.redis.consumer_name;
    }

    bool VideoInferenceWorker::initModelRunner() {
        runner_ = createModelRunner(config_.model.type);
        if (!runner_) {
            spdlog::error("Unsupported video worker model.type={}", config_.model.type);
            return false;
        }
        std::string error;
        if (!runner_->init(config_, error)) {
            spdlog::error("Video ModelRunner init failed: {}", error);
            runner_.reset();
            return false;
        }
        spdlog::info("Video ModelRunner initialized: model_type={}, engine={}", runner_->modelType(), config_.model.engine_path);
        return true;
    }

    void VideoInferenceWorker::releaseRunnerNoexcept() noexcept {
        if (!runner_initialized_) {
            return;
        }
        try {
            if (runner_) {
                runner_->release();
                runner_.reset();
            }
        }
        catch (...) {
            runner_.reset();
        }
        runner_initialized_ = false;
    }

    void VideoInferenceWorker::loop() {
        spdlog::info("VideoInferenceWorker started: id={}, consumer={}, stream={}",
            worker_id_, config_.redis.consumer_name, config_.redis.stream_key);
        writeHeartbeatNoexcept();

        while (running_.load()) {
            RedisTask task;
            std::string error;

            if (redis_queue_.claimPendingTask(task, error)) {
                spdlog::info("Video worker {} reclaimed pending task: task_id={}, stream_id={}",
                    worker_id_, task.task_id, task.stream_id);
                processVideoTask(task);
                continue;
            }
            if (!error.empty()) {
                spdlog::error("Video worker {} claimPendingTask failed: {}", worker_id_, error);
            }

            error.clear();
            if (!redis_queue_.popTask(task, error)) {
                if (!error.empty()) {
                    spdlog::error("Video worker {} popTask failed: {}", worker_id_, error);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                continue;
            }
            processVideoTask(task);
        }

        setWorkerStatus("stopping");
        writeHeartbeatNoexcept();
        spdlog::info("VideoInferenceWorker stopped: id={}, consumer={}", worker_id_, config_.redis.consumer_name);
    }

    void VideoInferenceWorker::processVideoTask(const RedisTask& task) {
        const long long start_time_ms = nowMs();
        const long long queue_wait_ms = task.create_time_ms > 0 ? std::max(0LL, start_time_ms - task.create_time_ms) : 0LL;
        long long processed_frames = 0;
        long long total_frames = 0;
        double fps = config_.video.fallback_fps;
        int width = 0;
        int height = 0;
        long long duration_ms = 0;
        long long total_detections = 0;
        long long max_objects_per_frame = 0;

        setWorkerStatus("running", task.task_id);
        writeHeartbeatNoexcept();

        std::string redis_error;
        if (!redis_queue_.markRunning(task.task_id, start_time_ms, worker_id_, config_.redis.consumer_name, redis_error)) {
            spdlog::error("Video worker {} markRunning failed: task_id={}, error={}", worker_id_, task.task_id, redis_error);
        }

        try {
            bool cancel_before_start = false;
            std::string cancel_before_start_error;
            if (redis_queue_.isCancelRequested(task.task_id, cancel_before_start, cancel_before_start_error) && cancel_before_start) {
                const long long finish_time_ms = nowMs();
                const long long total_ms = task.create_time_ms > 0 ? std::max(0LL, finish_time_ms - task.create_time_ms) : 0LL;
                nlohmann::json body;
                body["success"] = false;
                body["task_id"] = task.task_id;
                body["task_kind"] = "video";
                body["model_type"] = "detect";
                body["status"] = "canceled";
                body["error"] = "canceled before processing";
                body["queue_backend"] = "redis_stream";
                body["worker_id"] = worker_id_;
                body["consumer_name"] = config_.redis.consumer_name;
                body["queue_wait_ms"] = queue_wait_ms;
                body["process_ms"] = 0;
                body["total_ms"] = total_ms;
                body["video"] = {
                    {"processed_frames", 0},
                    {"progress", 0.0},
                    {"completed_by_eof", false}
                };
                std::string cancel_mark_error;
                if (!redis_queue_.markVideoCanceled(task.task_id, body.dump(4), finish_time_ms, queue_wait_ms, 0, total_ms, 0, 0, worker_id_, config_.redis.consumer_name, cancel_mark_error)) {
                    throw std::runtime_error("markVideoCanceled before start failed: " + cancel_mark_error);
                }
                std::string ack_error;
                if (!redis_queue_.ackTask(task.stream_id, ack_error)) {
                    throw std::runtime_error("ack canceled-before-start video task failed: " + ack_error);
                }
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                spdlog::info("Video task canceled before processing: task_id={}", task.task_id);
                return;
            }

            if (task.task_kind != "video") {
                throw std::runtime_error("video worker received non-video task_kind=" + task.task_kind);
            }
            if (!runner_) {
                throw std::runtime_error("video model runner is not initialized");
            }
            if (task.input_video_path.empty() || task.output_video_path.empty()) {
                throw std::runtime_error("missing input_video_path/output_video_path in video task");
            }

            cv::VideoCapture cap(task.input_video_path);
            if (!cap.isOpened()) {
                throw std::runtime_error("failed to open input video: " + task.input_video_path);
            }

            width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
            height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
            fps = cap.get(cv::CAP_PROP_FPS);
            total_frames = static_cast<long long>(cap.get(cv::CAP_PROP_FRAME_COUNT));
            if (width <= 0 || height <= 0) {
                throw std::runtime_error("invalid video width/height");
            }
            if (fps <= 0.0 || fps > 240.0) {
                fps = config_.video.fallback_fps;
            }
            if (total_frames < 0) {
                total_frames = 0;
            }
            if (total_frames > 0 && fps > 0.0) {
                duration_ms = static_cast<long long>((static_cast<double>(total_frames) / fps) * 1000.0);
            }

            std::filesystem::create_directories(std::filesystem::path(task.output_video_path).parent_path());
            cv::VideoWriter writer;
            writer.open(task.output_video_path, fourccFromString(config_.video.output_fourcc), fps, cv::Size(width, height));
            if (!writer.isOpened()) {
                throw std::runtime_error("failed to open output video writer: " + task.output_video_path);
            }

            const int update_interval = std::max(1, config_.video.progress_update_interval_frames);
            const int max_process_frames = config_.video.max_process_frames;
            const auto process_start = std::chrono::steady_clock::now();

            cv::Mat frame;
            while (running_.load() && cap.read(frame)) {
                if (frame.empty()) {
                    break;
                }

                bool cancel_requested = false;
                std::string cancel_error;
                if (redis_queue_.isCancelRequested(task.task_id, cancel_requested, cancel_error) && cancel_requested) {
                    const long long finish_time_ms = nowMs();
                    const long long process_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - process_start).count();
                    const long long total_ms = task.create_time_ms > 0 ? std::max(0LL, finish_time_ms - task.create_time_ms) : process_ms;

                    nlohmann::json body;
                    body["success"] = false;
                    body["task_id"] = task.task_id;
                    body["task_kind"] = "video";
                    body["model_type"] = "detect";
                    body["status"] = "canceled";
                    body["error"] = "canceled by user";
                    body["queue_backend"] = "redis_stream";
                    body["worker_id"] = worker_id_;
                    body["consumer_name"] = config_.redis.consumer_name;
                    body["queue_wait_ms"] = queue_wait_ms;
                    body["process_ms"] = process_ms;
                    body["total_ms"] = total_ms;
                    body["video"] = {
                        {"width", width},
                        {"height", height},
                        {"fps", fps},
                        {"total_frames", total_frames},
                        {"total_frames_estimated", total_frames},
                        {"processed_frames", processed_frames},
                        {"progress", total_frames > 0 ? static_cast<double>(processed_frames) / static_cast<double>(total_frames) : 0.0},
                        {"completed_by_eof", false}
                    };

                    writer.release();
                    cap.release();

                    std::string cancel_mark_error;
                    if (!redis_queue_.markVideoCanceled(
                        task.task_id,
                        body.dump(4),
                        finish_time_ms,
                        queue_wait_ms,
                        process_ms,
                        total_ms,
                        processed_frames,
                        total_frames,
                        worker_id_,
                        config_.redis.consumer_name,
                        cancel_mark_error)) {
                        throw std::runtime_error("markVideoCanceled failed: " + cancel_mark_error);
                    }
                    std::string ack_error;
                    if (!redis_queue_.ackTask(task.stream_id, ack_error)) {
                        throw std::runtime_error("ack canceled video task failed: " + ack_error);
                    }
                    setWorkerStatus("idle");
                    writeHeartbeatNoexcept();
                    spdlog::info("Video task canceled: task_id={}, processed_frames={}", task.task_id, processed_frames);
                    return;
                }

                auto detections = runner_->infer(frame);
                cv::Mat result = runner_->draw(frame, detections);
                if (result.empty()) {
                    result = frame;
                }
                writer.write(result);

                ++processed_frames;
                total_detections += static_cast<long long>(detections.size());
                max_objects_per_frame = std::max(max_objects_per_frame, static_cast<long long>(detections.size()));

                if (processed_frames % update_interval == 0) {
                    const long long process_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - process_start).count();
                    const double progress = total_frames > 0 ?
                        std::min(1.0, static_cast<double>(processed_frames) / static_cast<double>(total_frames)) : 0.0;
                    std::string progress_error;
                    redis_queue_.updateVideoProgress(
                        task.task_id,
                        processed_frames,
                        total_frames,
                        processed_frames,
                        progress,
                        fps,
                        width,
                        height,
                        process_ms,
                        progress_error);
                    writeHeartbeatNoexcept();
                }

                if (max_process_frames > 0 && processed_frames >= max_process_frames) {
                    break;
                }
            }

            if (!running_.load()) {
                writer.release();
                cap.release();
                spdlog::warn("Video worker stopped while processing task_id={}. Task is left pending for XAUTOCLAIM.", task.task_id);
                setWorkerStatus("stopping", task.task_id);
                writeHeartbeatNoexcept();
                return;
            }

            writer.release();
            cap.release();

            const long long finish_time_ms = nowMs();
            const long long process_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - process_start).count();
            const long long total_ms = task.create_time_ms > 0 ? std::max(0LL, finish_time_ms - task.create_time_ms) : process_ms;
            long long output_video_bytes = 0;
            try {
                if (std::filesystem::exists(task.output_video_path)) {
                    output_video_bytes = static_cast<long long>(std::filesystem::file_size(task.output_video_path));
                }
            }
            catch (...) {
                output_video_bytes = 0;
            }

            nlohmann::json body;
            body["success"] = true;
            body["task_id"] = task.task_id;
            body["task_kind"] = "video";
            body["model_type"] = "detect";
            body["status"] = "done";
            body["queue_backend"] = "redis_stream";
            body["video_storage"] = "local_files";
            body["worker_id"] = worker_id_;
            body["consumer_name"] = config_.redis.consumer_name;
            body["queue_wait_ms"] = queue_wait_ms;
            body["process_ms"] = process_ms;
            body["inference_ms"] = process_ms;
            body["total_ms"] = total_ms;
            body["create_time_ms"] = task.create_time_ms;
            body["start_time_ms"] = start_time_ms;
            body["finish_time_ms"] = finish_time_ms;
            body["video"] = {
                {"width", width},
                {"height", height},
                {"fps", fps},
                {"total_frames", total_frames},
                {"total_frames_estimated", total_frames},
                {"processed_frames", processed_frames},
                {"effective_total_frames", processed_frames},
                {"progress", 1.0},
                {"duration_ms", duration_ms},
                {"completed_by_eof", true}
            };
            body["summary"] = {
                {"total_detections", total_detections},
                {"max_objects_per_frame", max_objects_per_frame},
                {"frame_results_saved", false}
            };
            body["input_video_path"] = task.input_video_path;
            body["output_video_path"] = task.output_video_path;
            body["output_video_filename"] = task.output_video_filename;
            body["output_video_bytes"] = output_video_bytes;
            body["output_video_url"] = "/api/v1/video/result/" + task.task_id + "/file";

            redis_error.clear();
            if (!redis_queue_.markVideoDone(
                task.task_id,
                body.dump(4),
                task.output_video_path,
                task.output_video_filename,
                finish_time_ms,
                queue_wait_ms,
                process_ms,
                total_ms,
                processed_frames,
                total_frames,
                fps,
                width,
                height,
                duration_ms,
                worker_id_,
                config_.redis.consumer_name,
                redis_error)) {
                setLastError("markVideoDone failed: " + redis_error);
                spdlog::error("Video worker {} markVideoDone failed, keep task pending: task_id={}, error={}",
                    worker_id_, task.task_id, redis_error);
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                return;
            }

            redis_error.clear();
            if (!redis_queue_.ackTask(task.stream_id, redis_error)) {
                setLastError("ackTask failed: " + redis_error);
                spdlog::error("Video worker {} ackTask failed after markVideoDone: task_id={}, error={}",
                    worker_id_, task.task_id, redis_error);
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                return;
            }

            processed_count_.fetch_add(1);
            setLastError("");
            setWorkerStatus("idle");
            writeHeartbeatNoexcept();
            spdlog::info("Video task done: task_id={}, frames={}, total_ms={}, output={}",
                task.task_id, processed_frames, total_ms, task.output_video_path);
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
                spdlog::error("Video worker {} markFailed failed, keep task pending: task_id={}, error={}",
                    worker_id_, task.task_id, redis_error);
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                return;
            }

            redis_error.clear();
            if (!redis_queue_.ackTask(task.stream_id, redis_error)) {
                spdlog::error("Video worker {} ackTask failed after markFailed: task_id={}, error={}",
                    worker_id_, task.task_id, redis_error);
                setWorkerStatus("idle");
                writeHeartbeatNoexcept();
                return;
            }

            setWorkerStatus("idle");
            writeHeartbeatNoexcept();
            spdlog::error("Video task failed: task_id={}, error={}", task.task_id, e.what());
        }
    }

    void VideoInferenceWorker::heartbeatLoop() noexcept {
        while (running_.load()) {
            writeHeartbeatNoexcept();
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.worker.heartbeat_interval_ms));
        }
        writeHeartbeatNoexcept();
    }

    void VideoInferenceWorker::writeHeartbeatNoexcept() noexcept {
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
            heartbeat.model_type = "video";
            heartbeat.runner_model_type = config_.model.type.empty() ? "detect" : config_.model.type;
            heartbeat.worker_group = config_.worker.worker_group;
            heartbeat.worker_kind = config_.worker.worker_kind;
            heartbeat.task_kind = config_.worker.task_kind;
            heartbeat.stream_type = config_.worker.stream_type;
            heartbeat.engine_path = config_.model.engine_path;
            heartbeat.labels_path = config_.model.labels_path;
            heartbeat.max_concurrency = config_.worker.max_concurrency;
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
                spdlog::error("Video worker {} heartbeat write failed: {}", worker_id_, error);
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Video worker heartbeat exception: {}", e.what());
        }
        catch (...) {
            spdlog::error("Video worker heartbeat unknown exception.");
        }
    }

    void VideoInferenceWorker::setWorkerStatus(const std::string& status, const std::string& current_task_id) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        worker_status_ = status;
        current_task_id_ = current_task_id;
    }

    void VideoInferenceWorker::setLastError(const std::string& error_message) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = error_message;
    }

    long long VideoInferenceWorker::nowMs() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    int VideoInferenceWorker::fourccFromString(const std::string& value) {
        if (value.size() != 4) {
            return cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        }
        return cv::VideoWriter::fourcc(value[0], value[1], value[2], value[3]);
    }

}  // namespace yolo11_server
