#include "server/stream_inference_worker.h"

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

#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>

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

        int safeSnapshotInterval(int value) {
            return value <= 0 ? 5 : value;
        }

    }  // namespace

    StreamInferenceWorker::StreamInferenceWorker(int worker_id, const AppConfig& config, const std::string& consumer_name)
        : worker_id_(worker_id),
          config_(config),
          redis_queue_(makeWorkerRedisConfig(config.redis, consumer_name)) {
        config_.redis.consumer_name = consumer_name;
        start_time_ms_ = nowMs();
    }

    StreamInferenceWorker::~StreamInferenceWorker() noexcept {
        stop();
        releaseRunnerNoexcept();
    }

    bool StreamInferenceWorker::start() {
        if (running_.load()) {
            return true;
        }
        if (!config_.redis.enabled) {
            spdlog::error("StreamInferenceWorker requires redis.enabled=true.");
            return false;
        }
        if (!config_.stream.enabled) {
            spdlog::error("StreamInferenceWorker requires stream.enabled=true.");
            return false;
        }
        if (toLower(config_.model.type) != "detect") {
            spdlog::error("Phase 14 stream worker only supports model.type=detect. Current model.type={}", config_.model.type);
            return false;
        }

        std::string error;
        if (!redis_queue_.connect(error)) {
            spdlog::error("Stream worker {} failed to connect Redis: {}", worker_id_, error);
            return false;
        }

        if (!initModelRunner()) {
            spdlog::error("Stream worker {} failed to initialize model runner.", worker_id_);
            return false;
        }
        runner_initialized_ = true;

        std::filesystem::create_directories(config_.stream.snapshot_dir);
        setWorkerStatus("idle");
        running_ = true;

        if (config_.worker.heartbeat_enabled) {
            heartbeat_thread_ = std::thread([this]() { heartbeatLoop(); });
        }
        thread_ = std::thread([this]() { loop(); });
        return true;
    }

    void StreamInferenceWorker::stop() noexcept {
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
            spdlog::error("StreamInferenceWorker stop exception ignored.");
        }
    }

    bool StreamInferenceWorker::running() const {
        return running_.load();
    }

    std::string StreamInferenceWorker::consumerName() const {
        return config_.redis.consumer_name;
    }

    bool StreamInferenceWorker::initModelRunner() {
        runner_ = createModelRunner(config_.model.type);
        if (!runner_) {
            spdlog::error("Unsupported stream worker model.type={}", config_.model.type);
            return false;
        }
        std::string error;
        if (!runner_->init(config_, error)) {
            spdlog::error("Stream ModelRunner init failed: {}", error);
            runner_.reset();
            return false;
        }
        spdlog::info("Stream ModelRunner initialized: model_type={}, engine={}", runner_->modelType(), config_.model.engine_path);
        return true;
    }

    void StreamInferenceWorker::releaseRunnerNoexcept() noexcept {
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

    void StreamInferenceWorker::loop() {
        spdlog::info("StreamInferenceWorker started: id={}, consumer={}, stream={}",
            worker_id_, config_.redis.consumer_name, config_.redis.stream_key);
        writeHeartbeatNoexcept();

        while (running_.load()) {
            RedisTask task;
            std::string error;

            if (!redis_queue_.popTask(task, error)) {
                if (!error.empty()) {
                    spdlog::error("Stream worker {} popTask failed: {}", worker_id_, error);
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                continue;
            }
            processStreamTask(task);
        }

        setWorkerStatus("stopping");
        writeHeartbeatNoexcept();
        spdlog::info("StreamInferenceWorker stopped: id={}, consumer={}", worker_id_, config_.redis.consumer_name);
    }

    void StreamInferenceWorker::processStreamTask(const RedisTask& task) {
        const std::string stream_task_id = task.stream_task_id.empty() ? task.task_id : task.stream_task_id;
        long long frame_count = 0;
        const long long start_time_ms = nowMs();
        const int snapshot_interval = safeSnapshotInterval(task.snapshot_interval_frames > 0 ? task.snapshot_interval_frames : config_.stream.snapshot_interval_frames);
        const int target_fps = task.target_fps > 0 ? task.target_fps : config_.stream.target_fps;
        const int frame_delay_ms = target_fps > 0 ? std::max(1, 1000 / target_fps) : 0;
        const int max_no_frame_count = std::max(1, config_.stream.max_no_frame_count);
        const bool reconnect_enabled = config_.stream.enable_reconnect;
        const int reconnect_max_attempts = std::max(0, config_.stream.reconnect_max_attempts);
        const int reconnect_delay_ms = std::max(100, config_.stream.reconnect_delay_ms);
        const long long max_runtime_ms = config_.stream.max_runtime_seconds > 0
            ? static_cast<long long>(config_.stream.max_runtime_seconds) * 1000LL
            : 0LL;

        setWorkerStatus("starting", stream_task_id);
        writeHeartbeatNoexcept();

        std::string redis_error;
        if (!redis_queue_.markStreamStarting(stream_task_id, start_time_ms, worker_id_, config_.redis.consumer_name, redis_error)) {
            spdlog::error("Stream worker {} markStreamStarting failed: stream_id={}, error={}", worker_id_, stream_task_id, redis_error);
        }

        cv::VideoCapture cap;
        int reconnect_count = 0;
        int no_frame_count = 0;
        int width = 0;
        int height = 0;
        double source_fps = 0.0;

        try {
            if (task.task_kind != "stream") {
                throw std::runtime_error("stream worker received non-stream task_kind=" + task.task_kind);
            }
            if (!runner_) {
                throw std::runtime_error("stream model runner is not initialized");
            }
            if (stream_task_id.empty()) {
                throw std::runtime_error("missing stream_id in stream task");
            }

            const std::string source_type = toLower(task.source_type.empty() ? config_.stream.default_source_type : task.source_type);
            const std::string source_uri = task.source_uri;

            auto open_capture = [&]() -> bool {
                cap.release();
                if (source_type == "camera") {
                    int camera_id = task.camera_id;
                    if (!source_uri.empty() && isIntegerString(source_uri)) {
                        camera_id = std::stoi(source_uri);
                    }
                    spdlog::info("Opening camera stream: stream_id={}, camera_id={}, attempt={}", stream_task_id, camera_id, reconnect_count);
                    cap.open(camera_id);
                }
                else if (source_type == "file" || source_type == "rtsp") {
                    if (source_uri.empty()) {
                        throw std::runtime_error("empty source_uri for source_type=" + source_type);
                    }
                    spdlog::info("Opening {} stream: stream_id={}, uri={}, attempt={}", source_type, stream_task_id, source_uri, reconnect_count);
                    cap.open(source_uri);
                }
                else {
                    throw std::runtime_error("unsupported stream source_type=" + source_type);
                }

                if (!cap.isOpened()) {
                    return false;
                }

                width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
                height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
                source_fps = cap.get(cv::CAP_PROP_FPS);
                if (source_fps <= 0.0 || source_fps > 240.0) {
                    source_fps = target_fps > 0 ? static_cast<double>(target_fps) : 0.0;
                }
                if (width <= 0) width = 0;
                if (height <= 0) height = 0;
                return true;
            };

            while (!open_capture()) {
                const std::string reason = "failed to open stream source: type=" + source_type + ", uri=" + source_uri;
                if (!reconnect_enabled || source_type == "file" || reconnect_count >= reconnect_max_attempts) {
                    throw std::runtime_error(reason + ", reconnect_count=" + std::to_string(reconnect_count));
                }
                ++reconnect_count;
                std::string reconnect_error;
                redis_queue_.markStreamReconnecting(stream_task_id, reason, reconnect_count, no_frame_count, nowMs(), reconnect_error);
                setLastError(reason);
                setWorkerStatus("reconnecting", stream_task_id);
                writeHeartbeatNoexcept();
                std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms));
            }

            redis_error.clear();
            if (!redis_queue_.markStreamRunning(stream_task_id, width, height, source_fps, start_time_ms, worker_id_, config_.redis.consumer_name, redis_error)) {
                throw std::runtime_error("markStreamRunning failed: " + redis_error);
            }

            redis_error.clear();
            if (!redis_queue_.ackTask(task.stream_id, redis_error)) {
                spdlog::warn("Stream worker {} failed to XACK start message, continue running: stream_id={}, error={}", worker_id_, stream_task_id, redis_error);
            }

            const std::filesystem::path snapshot_path = task.snapshot_path.empty()
                ? (std::filesystem::path(config_.stream.snapshot_dir) / stream_task_id / "snapshot.jpg")
                : std::filesystem::path(task.snapshot_path);
            std::filesystem::create_directories(snapshot_path.parent_path());

            setLastError("");
            setWorkerStatus("running", stream_task_id);
            writeHeartbeatNoexcept();
            spdlog::info("Stream task running: stream_id={}, snapshot={}", stream_task_id, snapshot_path.string());

            auto last_frame_time = std::chrono::steady_clock::now();
            cv::Mat frame;

            while (running_.load()) {
                bool stop_requested = false;
                std::string stop_error;
                if (redis_queue_.isStreamStopRequested(stream_task_id, stop_requested, stop_error) && stop_requested) {
                    spdlog::info("Stop requested for stream_id={}", stream_task_id);
                    break;
                }

                if (max_runtime_ms > 0 && nowMs() - start_time_ms >= max_runtime_ms) {
                    spdlog::info("Stream max_runtime_seconds reached: stream_id={}, seconds={}", stream_task_id, config_.stream.max_runtime_seconds);
                    break;
                }

                if (!cap.read(frame) || frame.empty()) {
                    if (source_type == "file" && frame_count > 0) {
                        spdlog::info("File stream reached EOF: stream_id={}, frames={}", stream_task_id, frame_count);
                        break;
                    }
                    ++no_frame_count;
                    if (no_frame_count >= max_no_frame_count) {
                        const std::string reason = "stream read failed continuously, no_frame_count=" + std::to_string(no_frame_count);
                        if (reconnect_enabled && source_type != "file" && reconnect_count < reconnect_max_attempts) {
                            ++reconnect_count;
                            spdlog::warn("{}; reconnect stream_id={}, reconnect_count={}/{}", reason, stream_task_id, reconnect_count, reconnect_max_attempts);
                            std::string reconnect_error;
                            redis_queue_.markStreamReconnecting(stream_task_id, reason, reconnect_count, no_frame_count, nowMs(), reconnect_error);
                            setLastError(reason);
                            setWorkerStatus("reconnecting", stream_task_id);
                            writeHeartbeatNoexcept();
                            std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms));
                            if (open_capture()) {
                                no_frame_count = 0;
                                std::string running_error;
                                redis_queue_.markStreamRunning(stream_task_id, width, height, source_fps, start_time_ms, worker_id_, config_.redis.consumer_name, running_error);
                                setLastError("");
                                setWorkerStatus("running", stream_task_id);
                                writeHeartbeatNoexcept();
                            }
                            continue;
                        }
                        throw std::runtime_error(reason + ", reconnect_count=" + std::to_string(reconnect_count));
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                no_frame_count = 0;
                ++frame_count;

                auto detections = runner_->infer(frame);
                cv::Mat result = runner_->draw(frame, detections);
                if (result.empty()) {
                    result = frame;
                }

                if (width <= 0 || height <= 0) {
                    width = result.cols;
                    height = result.rows;
                }

                const bool should_save_snapshot = frame_count == 1 || frame_count % snapshot_interval == 0;
                if (should_save_snapshot) {
                    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, config_.stream.jpeg_quality };
                    if (!cv::imwrite(snapshot_path.string(), result, params)) {
                        throw std::runtime_error("failed to write stream snapshot: " + snapshot_path.string());
                    }
                    const long long update_time_ms = nowMs();
                    std::string update_error;
                    redis_queue_.updateStreamLatest(
                        stream_task_id,
                        snapshot_path.string(),
                        frame_count,
                        source_fps,
                        width,
                        height,
                        static_cast<int>(detections.size()),
                        update_time_ms,
                        update_error);
                    writeHeartbeatNoexcept();
                }

                if (frame_delay_ms > 0) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time).count();
                    if (elapsed_ms < frame_delay_ms) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(frame_delay_ms - elapsed_ms));
                    }
                    last_frame_time = std::chrono::steady_clock::now();
                }
            }

            cap.release();
            const long long stop_time_ms = nowMs();
            redis_error.clear();
            if (!redis_queue_.markStreamStopped(stream_task_id, stop_time_ms, frame_count, redis_error)) {
                setLastError("markStreamStopped failed: " + redis_error);
                spdlog::error("markStreamStopped failed: stream_id={}, error={}", stream_task_id, redis_error);
            }

            processed_count_.fetch_add(1);
            setLastError("");
            setWorkerStatus("idle");
            writeHeartbeatNoexcept();
            spdlog::info("Stream task stopped: stream_id={}, frames={}, reconnect_count={}", stream_task_id, frame_count, reconnect_count);
        }
        catch (const std::exception& e) {
            cap.release();
            failed_count_.fetch_add(1);
            setLastError(e.what());
            std::string mark_error;
            redis_queue_.markStreamFailed(stream_task_id, e.what(), nowMs(), frame_count, mark_error);
            std::string ack_error;
            if (!task.stream_id.empty()) {
                redis_queue_.ackTask(task.stream_id, ack_error);
            }
            setWorkerStatus("idle");
            writeHeartbeatNoexcept();
            spdlog::error("Stream task failed: stream_id={}, error={}", stream_task_id, e.what());
        }
    }

    void StreamInferenceWorker::heartbeatLoop() noexcept {
        while (running_.load()) {
            writeHeartbeatNoexcept();
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.worker.heartbeat_interval_ms));
        }
        writeHeartbeatNoexcept();
    }

    void StreamInferenceWorker::writeHeartbeatNoexcept() noexcept {
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
            heartbeat.model_type = "stream";
            heartbeat.runner_model_type = config_.model.type;
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
                heartbeat.current_task_id = current_stream_id_;
                heartbeat.last_error = last_error_;
            }
            std::string error;
            if (!redis_queue_.writeWorkerHeartbeat(heartbeat, config_.worker.heartbeat_ttl_seconds, error)) {
                spdlog::error("Stream worker {} heartbeat write failed: {}", worker_id_, error);
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Stream worker heartbeat exception: {}", e.what());
        }
        catch (...) {
            spdlog::error("Stream worker heartbeat unknown exception.");
        }
    }

    void StreamInferenceWorker::setWorkerStatus(const std::string& status, const std::string& current_stream_id) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        worker_status_ = status;
        current_stream_id_ = current_stream_id;
    }

    void StreamInferenceWorker::setLastError(const std::string& error_message) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = error_message;
    }

    long long StreamInferenceWorker::nowMs() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    std::string StreamInferenceWorker::toLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    bool StreamInferenceWorker::isIntegerString(const std::string& value) {
        if (value.empty()) {
            return false;
        }
        size_t start = value[0] == '-' ? 1 : 0;
        if (start >= value.size()) {
            return false;
        }
        for (size_t i = start; i < value.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
                return false;
            }
        }
        return true;
    }

}  // namespace yolo11_server
