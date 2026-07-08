#include "server/http_controller.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>

#include "server/image_codec.h"
#include "server/result_serializer.h"
#include "postprocess.h"

namespace yolo11_server {

    namespace {

        std::string readWholeFileBinary(const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return {};
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        nlohmann::json memoryStatsToJson(const RedisMemoryStats& stats) {
            nlohmann::json body;
            body["found"] = stats.found;
            body["used_memory_bytes"] = stats.used_memory_bytes;
            body["used_memory_mb"] = stats.used_memory_mb;
            body["used_memory_human"] = stats.used_memory_human;
            body["maxmemory_bytes"] = stats.maxmemory_bytes;
            body["maxmemory_mb"] = stats.maxmemory_mb;
            body["maxmemory_human"] = stats.maxmemory_human;
            return body;
        }

        nlohmann::json workerHeartbeatToJson(const WorkerHeartbeatRecord& worker) {
            nlohmann::json item;
            item["consumer_name"] = worker.consumer_name;
            item["heartbeat_key"] = worker.heartbeat_key;
            item["found"] = worker.found;
            item["alive"] = worker.alive;
            item["pid"] = worker.pid;
            item["host"] = worker.host;
            item["worker_id"] = worker.worker_id;
            item["gpu_id"] = worker.gpu_id;
            item["model_type"] = worker.model_type;
            item["status"] = worker.status;
            item["current_task_id"] = worker.current_task_id;
            item["processed_count"] = worker.processed_count;
            item["failed_count"] = worker.failed_count;
            item["start_time_ms"] = worker.start_time_ms;
            item["last_heartbeat_ms"] = worker.last_heartbeat_ms;
            item["last_heartbeat_age_ms"] = worker.last_heartbeat_age_ms;
            item["last_error"] = worker.last_error;
            return item;
        }


        std::string toLowerString(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
            return text;
        }

        bool isTrueString(const std::string& value) {
            const std::string lower = toLowerString(value);
            return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
        }

        std::string getOptionalModelFilter(const crow::request& request) {
            const char* raw = request.url_params.get("model");
            if (raw == nullptr) {
                return {};
            }
            return toLowerString(std::string(raw));
        }

        bool workerMatchesModelFilter(const WorkerHeartbeatRecord& worker, const std::string& model_filter) {
            if (model_filter.empty()) {
                return true;
            }
            return toLowerString(worker.model_type) == model_filter;
        }

        std::string phaseNameForModelType(const std::string& model_type) {
            (void)model_type;
            return "phase10_5_multimodel_runner_refactor";
        }

        crow::response makeJsonResponse(int code, const nlohmann::json& body) {
            crow::response response(code);
            response.set_header("Content-Type", "application/json; charset=utf-8");
            response.body = body.dump(4);
            return response;
        }

    }  // namespace

    HttpController::HttpController(const AppConfig& config, yolo11::Yolo11Detector* detector)
        : config_(config), detector_(detector), redis_queue_(config.redis) {
        std::filesystem::create_directories(config_.output.input_dir);
        std::filesystem::create_directories(config_.output.output_dir);

        sync_enabled_ = config_.server.enable_sync_detect && detector_ != nullptr;

        if (config_.redis.enabled) {
            std::string error;
            if (!redis_queue_.connect(error)) {
                throw std::runtime_error("failed to connect Redis: " + error);
            }
            redis_mode_ = true;
            spdlog::info("Redis queue enabled for HTTP producer: {}:{}, stream={}, group={}",
                config_.redis.host, config_.redis.port, config_.redis.stream_key, config_.redis.consumer_group);
        }
        else {
            redis_mode_ = false;
            spdlog::warn("Redis queue disabled. Async HTTP API will return 503.");
        }

        std::string label_error;
        if (!label_map_.loadFromFile(config_.model.labels_path, label_error)) {
            spdlog::warn("Failed to load labels_path='{}': {}. class_name will fall back to class_<id>.",
                config_.model.labels_path, label_error);
        }
        else {
            spdlog::info("Loaded {} labels from {}", label_map_.size(), label_map_.sourcePath());
        }

        if (!sync_enabled_) {
            spdlog::info("Sync detect endpoint is disabled in this process.");
        }
    }

    void HttpController::registerRoutes(crow::SimpleApp& app) {
        CROW_ROUTE(app, "/api/v1/health")
            .methods(crow::HTTPMethod::GET)
            ([this]() {
            return handleHealth();
                });

        CROW_ROUTE(app, "/api/v1/ready")
            .methods(crow::HTTPMethod::GET)
            ([this](const crow::request& request) {
            return handleReady(request);
                });

        CROW_ROUTE(app, "/api/v1/workers")
            .methods(crow::HTTPMethod::GET)
            ([this](const crow::request& request) {
            return handleWorkers(request);
                });

        CROW_ROUTE(app, "/api/v1/metrics")
            .methods(crow::HTTPMethod::GET)
            ([this](const crow::request& request) {
            return handleMetrics(request);
                });

        CROW_ROUTE(app, "/api/v1/detect/image")
            .methods(crow::HTTPMethod::POST)
            ([this](const crow::request& request) {
            return handleDetectImage(request);
                });

        CROW_ROUTE(app, "/api/v1/detect/image/async")
            .methods(crow::HTTPMethod::POST)
            ([this](const crow::request& request) {
            return handleDetectImageAsync(request);
                });

        CROW_ROUTE(app, "/api/v1/detect/obb/async")
            .methods(crow::HTTPMethod::POST)
            ([this](const crow::request& request) {
            return handleDetectObbImageAsync(request);
                });

        CROW_ROUTE(app, "/api/v1/result/<string>")
            .methods(crow::HTTPMethod::GET)
            ([this](const std::string& task_id) {
            return handleGetAsyncResult(task_id);
                });

        CROW_ROUTE(app, "/api/v1/result/<string>/image")
            .methods(crow::HTTPMethod::GET)
            ([this](const std::string& task_id) {
            return handleGetResultImageByTaskId(task_id);
                });

        CROW_ROUTE(app, "/api/v1/image/<string>")
            .methods(crow::HTTPMethod::GET)
            ([this](const std::string& filename) {
            return handleGetResultImage(filename);
                });
    }

    crow::response HttpController::handleHealth() const {
        nlohmann::json body;
        body["success"] = true;
        body["status"] = "ok";
        body["service"] = "yolo11_server";
        body["phase"] = phaseNameForModelType(config_.model.type);
        body["role"] = "http_producer";
        body["model_type"] = config_.model.type;
        body["active_model"] = config_.active_model.empty() ? nlohmann::json(nullptr) : nlohmann::json(config_.active_model);
        body["model_profile_count"] = config_.model_profiles.size();
        body["engine_path"] = config_.model.engine_path;
        body["labels_path"] = config_.model.labels_path;
        body["labels_loaded"] = !label_map_.empty();
        body["label_count"] = label_map_.size();
        body["gpu_id"] = config_.model.gpu_id;
        body["sync_detect_enabled"] = sync_enabled_;
        body["embedded_worker_enabled"] = config_.worker.enabled;
        body["queue_backend"] = redis_mode_ ? "redis_stream" : "disabled";
        body["image_storage"] = redis_mode_ ? "redis_binary_keys" : "local_file_only";
        body["server_health"] = "ok";

        if (redis_mode_) {
            std::string redis_error;
            body["redis_enabled"] = true;
            body["redis_host"] = config_.redis.host;
            body["redis_port"] = config_.redis.port;
            body["redis_stream_key"] = config_.redis.stream_key;
            body["redis_consumer_group"] = config_.redis.consumer_group;
            body["redis_ping"] = redis_queue_.ping(redis_error) ? "ok" : "failed";
            if (!redis_error.empty()) {
                body["redis_error"] = redis_error;
            }

            RedisStreamStats stats;
            std::string stats_error;
            if (redis_queue_.getStreamStats(stats, stats_error)) {
                body["redis_stream_len"] = stats.stream_len;
                body["redis_pending"] = stats.pending;
                body["redis_stream_max_len"] = config_.redis.stream_max_len;
                body["redis_pending_reclaim"] = config_.redis.enable_pending_reclaim;
                body["redis_pending_min_idle_ms"] = config_.redis.pending_min_idle_ms;
            }
            else if (!stats_error.empty()) {
                body["redis_stats_error"] = stats_error;
            }

            RedisMemoryStats memory_stats;
            std::string memory_error;
            if (redis_queue_.getRedisMemoryStats(memory_stats, memory_error)) {
                body["redis_memory"] = memoryStatsToJson(memory_stats);
                body["redis_max_used_memory_mb_config"] = config_.redis.max_redis_used_memory_mb;
            }
            else if (!memory_error.empty()) {
                body["redis_memory_error"] = memory_error;
            }
        }
        else {
            body["redis_enabled"] = false;
        }

        return makeJsonResponse(200, body);
    }

    crow::response HttpController::handleReady(const crow::request& request) const {
        const std::string model_filter = getOptionalModelFilter(request);
        const std::string configured_model_type = toLowerString(config_.model.type);

        nlohmann::json body;
        body["success"] = true;
        body["service"] = "yolo11_server";
        body["phase"] = phaseNameForModelType(config_.model.type);
        body["configured_model_type"] = configured_model_type;
        body["model_filter"] = model_filter.empty() ? nlohmann::json(nullptr) : nlohmann::json(model_filter);
        body["server_status"] = "ok";
        body["redis_status"] = redis_mode_ ? "unknown" : "disabled";
        body["worker_status"] = "unknown";
        body["ready"] = false;
        body["expected_workers"] = config_.worker.worker_num;
        body["min_alive_workers"] = config_.worker.min_alive_workers;

        if (!model_filter.empty() && model_filter != configured_model_type) {
            body["success"] = false;
            body["error_code"] = "MODEL_PROFILE_NOT_ACTIVE";
            body["reason"] = "requested model is not the active model profile of this server process";
            return makeJsonResponse(503, body);
        }

        bool redis_ok = false;
        bool memory_ok = true;
        bool workers_ok = false;

        if (!redis_mode_) {
            body["redis_status"] = "disabled";
            body["reason"] = "redis disabled";
            return makeJsonResponse(503, body);
        }

        std::string redis_error;
        redis_ok = redis_queue_.ping(redis_error);
        body["redis_status"] = redis_ok ? "ok" : "failed";
        body["redis_ping"] = redis_ok ? "ok" : "failed";
        if (!redis_error.empty()) {
            body["redis_error"] = redis_error;
        }

        RedisStreamStats stats;
        std::string stats_error;
        if (redis_queue_.getStreamStats(stats, stats_error)) {
            body["redis_pending"] = stats.pending;
            body["redis_stream_len"] = stats.stream_len;
            body["redis_stream_max_len"] = config_.redis.stream_max_len;
            body["redis_stream_key"] = config_.redis.stream_key;
            body["redis_consumer_group"] = config_.redis.consumer_group;
        }
        else if (!stats_error.empty()) {
            body["redis_stats_error"] = stats_error;
        }

        RedisMemoryStats memory_stats;
        std::string memory_error;
        if (redis_queue_.getRedisMemoryStats(memory_stats, memory_error)) {
            body["redis_memory"] = memoryStatsToJson(memory_stats);
            body["redis_max_used_memory_mb_config"] = config_.redis.max_redis_used_memory_mb;
            if (config_.redis.max_redis_used_memory_mb > 0 &&
                memory_stats.used_memory_mb > static_cast<double>(config_.redis.max_redis_used_memory_mb)) {
                memory_ok = false;
                body["redis_memory_status"] = "over_limit";
            }
            else {
                body["redis_memory_status"] = "ok";
            }
        }
        else {
            body["redis_memory_status"] = "unknown";
            if (!memory_error.empty()) {
                body["redis_memory_error"] = memory_error;
            }
        }

        std::vector<WorkerHeartbeatRecord> workers;
        std::string workers_error;
        int alive_workers = 0;
        int matched_workers = 0;
        if (redis_queue_.getWorkerHeartbeats(
            config_.worker.consumer_name_prefix,
            config_.worker.worker_num,
            workers,
            workers_error)) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& worker : workers) {
                if (!workerMatchesModelFilter(worker, model_filter)) {
                    continue;
                }
                ++matched_workers;
                if (worker.alive) {
                    ++alive_workers;
                }
                arr.push_back(workerHeartbeatToJson(worker));
            }
            body["workers"] = arr;
            body["matched_workers"] = matched_workers;
            body["alive_workers"] = alive_workers;
            workers_ok = alive_workers >= config_.worker.min_alive_workers;
            body["worker_status"] = workers_ok ? "ok" : "no_enough_alive_workers";
        }
        else {
            body["worker_status"] = "query_failed";
            body["workers_error"] = workers_error;
        }

        const bool ready = redis_ok && memory_ok && workers_ok;
        body["ready"] = ready;
        if (!ready) {
            if (!redis_ok) {
                body["reason"] = "redis not healthy";
            }
            else if (!memory_ok) {
                body["reason"] = "redis memory over limit";
            }
            else if (!workers_ok) {
                body["reason"] = "not enough alive workers";
            }
        }

        return makeJsonResponse(ready ? 200 : 503, body);
    }

    crow::response HttpController::handleWorkers(const crow::request& request) const {
        const std::string model_filter = getOptionalModelFilter(request);
        const std::string configured_model_type = toLowerString(config_.model.type);

        if (!redis_mode_) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_DISABLED";
            body["error"] = "worker heartbeat query requires redis.enabled=true";
            return makeJsonResponse(503, body);
        }

        if (!model_filter.empty() && model_filter != configured_model_type) {
            nlohmann::json body;
            body["success"] = true;
            body["configured_model_type"] = configured_model_type;
            body["model_filter"] = model_filter;
            body["expected_workers"] = 0;
            body["min_alive_workers"] = 0;
            body["alive_workers"] = 0;
            body["workers"] = nlohmann::json::array();
            body["note"] = "requested model is not the active model profile of this server process";
            return makeJsonResponse(200, body);
        }

        std::vector<WorkerHeartbeatRecord> workers;
        std::string error;
        if (!redis_queue_.getWorkerHeartbeats(
            config_.worker.consumer_name_prefix,
            config_.worker.worker_num,
            workers,
            error)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "WORKER_HEARTBEAT_QUERY_FAILED";
            body["error"] = error;
            return makeJsonResponse(500, body);
        }

        int alive_workers = 0;
        int matched_workers = 0;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& worker : workers) {
            if (!workerMatchesModelFilter(worker, model_filter)) {
                continue;
            }
            ++matched_workers;
            if (worker.alive) {
                ++alive_workers;
            }
            arr.push_back(workerHeartbeatToJson(worker));
        }

        nlohmann::json body;
        body["success"] = true;
        body["configured_model_type"] = configured_model_type;
        body["model_filter"] = model_filter.empty() ? nlohmann::json(nullptr) : nlohmann::json(model_filter);
        body["expected_workers"] = config_.worker.worker_num;
        body["matched_workers"] = matched_workers;
        body["min_alive_workers"] = config_.worker.min_alive_workers;
        body["alive_workers"] = alive_workers;
        body["heartbeat_enabled"] = config_.worker.heartbeat_enabled;
        body["heartbeat_interval_ms"] = config_.worker.heartbeat_interval_ms;
        body["heartbeat_ttl_seconds"] = config_.worker.heartbeat_ttl_seconds;
        body["workers"] = arr;
        return makeJsonResponse(200, body);
    }

    crow::response HttpController::handleMetrics(const crow::request& request) const {
        const std::string model_filter = getOptionalModelFilter(request);
        const std::string configured_model_type = toLowerString(config_.model.type);

        if (!redis_mode_) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_DISABLED";
            body["error"] = "metrics require redis.enabled=true";
            return makeJsonResponse(503, body);
        }

        if (!model_filter.empty() && model_filter != configured_model_type) {
            nlohmann::json body;
            body["success"] = true;
            body["configured_model_type"] = configured_model_type;
            body["model_filter"] = model_filter;
            body["metrics_found"] = false;
            body["total_tasks_done"] = 0;
            body["total_tasks_failed"] = 0;
            body["total_tasks"] = 0;
            body["workers"] = nlohmann::json::array();
            body["note"] = "requested model is not the active model profile of this server process";
            return makeJsonResponse(200, body);
        }

        RedisRuntimeMetrics metrics;
        std::string metrics_error;
        if (!redis_queue_.getRuntimeMetrics(metrics, metrics_error)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "METRICS_QUERY_FAILED";
            body["error"] = metrics_error;
            return makeJsonResponse(500, body);
        }

        nlohmann::json worker_done = nlohmann::json::object();
        for (const auto& kv : metrics.worker_done_count) {
            worker_done[kv.first] = kv.second;
        }

        nlohmann::json worker_failed = nlohmann::json::object();
        for (const auto& kv : metrics.worker_failed_count) {
            worker_failed[kv.first] = kv.second;
        }

        RedisStreamStats stream_stats;
        std::string stream_error;
        const bool stream_ok = redis_queue_.getStreamStats(stream_stats, stream_error);

        RedisMemoryStats memory_stats;
        std::string memory_error;
        const bool memory_ok = redis_queue_.getRedisMemoryStats(memory_stats, memory_error);

        std::vector<WorkerHeartbeatRecord> workers;
        std::string workers_error;
        int alive_workers = 0;
        nlohmann::json worker_arr = nlohmann::json::array();
        if (redis_queue_.getWorkerHeartbeats(
            config_.worker.consumer_name_prefix,
            config_.worker.worker_num,
            workers,
            workers_error)) {
            for (const auto& worker : workers) {
                if (!workerMatchesModelFilter(worker, model_filter)) {
                    continue;
                }
                if (worker.alive) {
                    ++alive_workers;
                }
                worker_arr.push_back(workerHeartbeatToJson(worker));
            }
        }

        nlohmann::json model_summary;
        model_summary["model_type"] = configured_model_type;
        model_summary["stream_key"] = config_.redis.stream_key;
        model_summary["consumer_group"] = config_.redis.consumer_group;
        model_summary["total_done"] = metrics.done_count;
        model_summary["total_failed"] = metrics.failed_count;
        model_summary["total_tasks"] = metrics.total_count;
        model_summary["avg_queue_wait_ms"] = metrics.avg_queue_wait_ms;
        model_summary["avg_inference_ms"] = metrics.avg_inference_ms;
        model_summary["avg_total_ms"] = metrics.avg_total_ms;
        model_summary["recent_done_count"] = metrics.recent_done_count;
        model_summary["recent_qps_60s"] = metrics.qps_recent;

        nlohmann::json body;
        body["success"] = true;
        body["service"] = "yolo11_server";
        body["phase"] = "phase10_5_multimodel_runner_refactor";
        body["configured_model_type"] = configured_model_type;
        body["model_filter"] = model_filter.empty() ? nlohmann::json(nullptr) : nlohmann::json(model_filter);
        body["models"] = nlohmann::json::object();
        body["models"][configured_model_type] = model_summary;
        body["metrics_enabled"] = config_.redis.metrics_enabled;
        body["metrics_found"] = metrics.found;
        body["recent_window_seconds"] = metrics.recent_window_seconds;
        body["recent_done_count"] = metrics.recent_done_count;
        body["qps_recent"] = metrics.qps_recent;
        body["total_tasks_done"] = metrics.done_count;
        body["total_tasks_failed"] = metrics.failed_count;
        body["total_tasks"] = metrics.total_count;
        body["last_task_id"] = metrics.last_task_id;
        body["last_finish_time_ms"] = metrics.last_finish_time_ms;
        body["worker_distribution"] = worker_done;
        body["worker_failed_distribution"] = worker_failed;
        body["alive_workers"] = alive_workers;
        body["expected_workers"] = config_.worker.worker_num;
        body["workers"] = worker_arr;
        body["latency"] = {
            {"avg_queue_wait_ms", metrics.avg_queue_wait_ms},
            {"avg_inference_ms", metrics.avg_inference_ms},
            {"avg_total_ms", metrics.avg_total_ms}
        };

        if (stream_ok) {
            body["redis_pending"] = stream_stats.pending;
            body["redis_stream_len"] = stream_stats.stream_len;
            body["redis_stream_max_len"] = config_.redis.stream_max_len;
            body["redis_stream_key"] = config_.redis.stream_key;
            body["redis_consumer_group"] = config_.redis.consumer_group;
        }
        else if (!stream_error.empty()) {
            body["redis_stream_error"] = stream_error;
        }

        if (memory_ok) {
            body["redis_memory"] = memoryStatsToJson(memory_stats);
            body["redis_max_used_memory_mb_config"] = config_.redis.max_redis_used_memory_mb;
        }
        else if (!memory_error.empty()) {
            body["redis_memory_error"] = memory_error;
        }

        if (!workers_error.empty()) {
            body["workers_error"] = workers_error;
        }

        return makeJsonResponse(200, body);
    }

    crow::response HttpController::handleDetectImage(const crow::request& request) {
        if (!sync_enabled_) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "SYNC_DETECT_DISABLED";
            body["error"] = "sync detection is disabled in this server process. Use /api/v1/detect/image/async.";
            return makeJsonResponse(503, body);
        }

        const auto request_id = ++request_counter_;

        std::string memory_error;
        nlohmann::json memory_json;
        if (redisMemoryOverLimit(&memory_json, memory_error)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_MEMORY_OVER_LIMIT";
            body["error"] = memory_error;
            body["redis_memory"] = memory_json;
            return makeJsonResponse(503, body);
        }

        std::string error_message;
        if (!validateBodySize(request, error_message)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REQUEST_TOO_LARGE";
            body["error"] = error_message;
            return makeJsonResponse(413, body);
        }

        std::string image_bytes = extractImageBytes(request, error_message);

        if (image_bytes.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "EMPTY_IMAGE_BODY";
            body["error"] = error_message.empty() ? "empty image body" : error_message;
            return makeJsonResponse(400, body);
        }

        if (!validateRedisImageSize(image_bytes.size(), "input image", error_message)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "IMAGE_TOO_LARGE";
            body["error"] = error_message;
            return makeJsonResponse(413, body);
        }

        cv::Mat image = ImageCodec::decodeImageBytes(image_bytes);
        if (image.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "IMAGE_DECODE_FAILED";
            body["error"] = "failed to decode image. Please upload a valid jpg/png image.";
            return makeJsonResponse(400, body);
        }

        const int image_width = image.cols;
        const int image_height = image.rows;
        const int image_channels = image.channels();
        const bool draw = isTrueParam(request.url_params.get("draw"));
        const bool debug = isTrueParam(request.url_params.get("debug"));

        std::vector<Detection> detections;
        cv::Mat result_image;

        auto t0 = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(sync_detector_mutex_);
            detections = detector_->infer(image);
            if (draw || config_.output.save_result_image) {
                result_image = detector_->draw(image, detections);
            }
        }
        auto t1 = std::chrono::steady_clock::now();

        const double infer_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        nlohmann::json body;
        body["success"] = true;
        body["request_id"] = request_id;
        body["status"] = "done";
        body["model_type"] = config_.model.type;
        body["image"] = {
            {"width", image_width},
            {"height", image_height},
            {"channels", image_channels}
        };
        body["bbox_coordinate_system"] = "original_image_pixels";
        body["bbox_format"] = "xywh_and_xyxy";
        body["num_detections"] = detections.size();
        body["inference_ms"] = infer_ms;

        if (debug) {
            body["debug"] = true;
            body["debug_note"] = "raw_model_bbox is returned only when debug=true or debug=1.";
        }

        body["detections"] = ResultSerializer::detectionsToJson(detections, image, label_map_, debug);

        if (config_.output.save_result_image && !result_image.empty()) {
            try {
                std::filesystem::create_directories(config_.output.output_dir);
                const std::string output_filename = makeResultImageFilename(request_id);
                const std::string output_path = makeResultImagePath(output_filename);
                std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, config_.output.jpeg_quality };
                bool ok = cv::imwrite(output_path, result_image, params);
                if (ok) {
                    const std::string relative_url = "/api/v1/image/" + output_filename;
                    body["saved_result_image"] = output_path;
                    body["result_image_filename"] = output_filename;
                    body["result_image_url"] = relative_url;
                    const std::string host = request.get_header_value("Host");
                    if (!host.empty()) {
                        body["result_image_url_full"] = "http://" + host + relative_url;
                    }
                }
                else {
                    body["saved_result_image"] = "";
                    body["save_warning"] = "cv::imwrite failed";
                }
            }
            catch (const std::exception& e) {
                body["saved_result_image"] = "";
                body["save_warning"] = e.what();
            }
        }

        return makeJsonResponse(200, body);
    }

    crow::response HttpController::handleDetectImageAsync(const crow::request& request) {
        return handleImageAsync(request, "detect");
    }

    crow::response HttpController::handleDetectObbImageAsync(const crow::request& request) {
        return handleImageAsync(request, "obb");
    }

    crow::response HttpController::handleImageAsync(const crow::request& request, const std::string& expected_model_type) {
        const std::string current_model_type = toLowerString(config_.model.type);
        const std::string target_model_type = toLowerString(expected_model_type);
        if (current_model_type != target_model_type) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "MODEL_TYPE_MISMATCH";
            body["error"] = "this server config is not for the requested async endpoint";
            body["configured_model_type"] = config_.model.type;
            body["requested_model_type"] = target_model_type;
            return makeJsonResponse(503, body);
        }

        if (!redis_mode_) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_DISABLED";
            body["error"] = "async inference requires redis.enabled=true";
            return makeJsonResponse(503, body);
        }

        if (config_.worker.heartbeat_enabled && config_.worker.min_alive_workers > 0) {
            std::vector<WorkerHeartbeatRecord> workers;
            std::string workers_error;
            if (!redis_queue_.getWorkerHeartbeats(
                config_.worker.consumer_name_prefix,
                config_.worker.worker_num,
                workers,
                workers_error)) {
                nlohmann::json body;
                body["success"] = false;
                body["error_code"] = "WORKER_HEARTBEAT_QUERY_FAILED";
                body["error"] = "failed to query worker heartbeat before submitting async task";
                body["workers_error"] = workers_error;
                body["ready"] = false;
                return makeJsonResponse(503, body);
            }

            int alive_workers = 0;
            nlohmann::json worker_array = nlohmann::json::array();
            for (const auto& worker : workers) {
                if (worker.alive) {
                    ++alive_workers;
                }
                worker_array.push_back(workerHeartbeatToJson(worker));
            }

            if (alive_workers < config_.worker.min_alive_workers) {
                nlohmann::json body;
                body["success"] = false;
                body["error_code"] = "NOT_ENOUGH_ALIVE_WORKERS";
                body["error"] = "async task rejected because not enough alive workers";
                body["ready"] = false;
                body["reason"] = "not enough alive workers";
                body["alive_workers"] = alive_workers;
                body["expected_workers"] = config_.worker.worker_num;
                body["min_alive_workers"] = config_.worker.min_alive_workers;
                body["worker_status"] = "no_enough_alive_workers";
                body["workers"] = worker_array;
                return makeJsonResponse(503, body);
            }
        }

        std::string memory_error;
        nlohmann::json memory_json;
        if (redisMemoryOverLimit(&memory_json, memory_error)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_MEMORY_OVER_LIMIT";
            body["error"] = memory_error;
            body["redis_memory"] = memory_json;
            return makeJsonResponse(503, body);
        }

        std::string error_message;
        if (!validateBodySize(request, error_message)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REQUEST_TOO_LARGE";
            body["error"] = error_message;
            return makeJsonResponse(413, body);
        }

        std::string image_bytes = extractImageBytes(request, error_message);

        if (image_bytes.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "EMPTY_IMAGE_BODY";
            body["error"] = error_message.empty() ? "empty image body" : error_message;
            return makeJsonResponse(400, body);
        }

        if (!validateRedisImageSize(image_bytes.size(), "input image", error_message)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "IMAGE_TOO_LARGE";
            body["error"] = error_message;
            return makeJsonResponse(413, body);
        }

        cv::Mat image = ImageCodec::decodeImageBytes(image_bytes);
        if (image.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "IMAGE_DECODE_FAILED";
            body["error"] = "failed to decode image. Please upload a valid jpg/png image.";
            return makeJsonResponse(400, body);
        }

        const std::string task_id = makeTaskId();
        const std::string input_key = redis_queue_.inputImageKey(task_id);
        const std::string result_key = redis_queue_.resultImageKey(task_id);
        const std::string input_path = makeInputImagePath(task_id);

        // Normalize input to JPEG bytes. Redis binary storage makes server and worker separable.
        std::string normalized_jpeg = ImageCodec::encodeJpegBytes(image, config_.output.jpeg_quality);
        if (normalized_jpeg.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "IMAGE_ENCODE_FAILED";
            body["error"] = "failed to encode normalized input image";
            return makeJsonResponse(500, body);
        }

        if (!validateRedisImageSize(normalized_jpeg.size(), "normalized input image", error_message)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "IMAGE_TOO_LARGE_AFTER_ENCODE";
            body["error"] = error_message;
            return makeJsonResponse(413, body);
        }

        std::string redis_error;
        if (!redis_queue_.setBinaryValueWithTtl(input_key, normalized_jpeg, config_.redis.input_image_ttl_seconds, redis_error)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_SET_INPUT_FAILED";
            body["error"] = "failed to store input image bytes to Redis";
            body["redis_error"] = redis_error;
            return makeJsonResponse(500, body);
        }

        // Local input is only a compatibility/debug fallback. Worker will prefer Redis bytes.
        try {
            std::filesystem::create_directories(config_.output.input_dir);
            cv::imwrite(input_path, image, { cv::IMWRITE_JPEG_QUALITY, config_.output.jpeg_quality });
        }
        catch (...) {
            // Do not fail the request: Redis bytes are the production input source.
        }

        RedisTask task;
        task.task_id = task_id;
        task.model_type = target_model_type;
        task.input_image_key = input_key;
        task.result_image_key = result_key;
        task.input_image_path = input_path;
        task.create_time_ms = nowMs();

        redis_error.clear();
        if (!redis_queue_.submitTask(task, redis_error)) {
            std::string del_error;
            redis_queue_.deleteKey(input_key, del_error);
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_SUBMIT_FAILED";
            body["error"] = "failed to submit task to Redis";
            body["redis_error"] = redis_error;
            return makeJsonResponse(500, body);
        }

        nlohmann::json body;
        body["success"] = true;
        body["task_id"] = task_id;
        body["model_type"] = target_model_type;
        body["status"] = "queued";
        body["queue_backend"] = "redis_stream";
        body["image_storage"] = "redis_binary_keys";
        body["input_image_key"] = input_key;
        body["result_image_key"] = result_key;
        body["result_url"] = "/api/v1/result/" + task_id;
        body["result_image_url"] = "/api/v1/result/" + task_id + "/image";

        spdlog::info("Async task submitted: task_id={}, model_type={}, input_bytes={}, redis_input_key={}",
            task_id, target_model_type, normalized_jpeg.size(), input_key);

        return makeJsonResponse(202, body);
    }

    crow::response HttpController::handleGetAsyncResult(const std::string& task_id) const {
        if (!redis_mode_) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_DISABLED";
            body["error"] = "async result query requires redis.enabled=true";
            body["task_id"] = task_id;
            return makeJsonResponse(503, body);
        }

        RedisTaskStatus task_status;
        std::string redis_error;
        if (!redis_queue_.getTaskStatus(task_id, task_status, redis_error)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_QUERY_FAILED";
            body["error"] = "failed to query Redis task status";
            body["redis_error"] = redis_error;
            body["task_id"] = task_id;
            return makeJsonResponse(500, body);
        }

        if (!task_status.found) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "TASK_NOT_FOUND";
            body["error"] = "task not found or expired";
            body["task_id"] = task_id;
            return makeJsonResponse(404, body);
        }

        if (task_status.status == "done" && !task_status.result_json_text.empty()) {
            crow::response response(200, task_status.result_json_text);
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        }

        nlohmann::json body;
        body["success"] = task_status.status != "failed";
        body["task_id"] = task_status.task_id;
        body["status"] = task_status.status;
        body["model_type"] = task_status.model_type.empty() ? config_.model.type : task_status.model_type;
        body["queue_backend"] = "redis_stream";
        body["image_storage"] = "redis_binary_keys";
        body["create_time_ms"] = task_status.create_time_ms;
        body["start_time_ms"] = task_status.start_time_ms;
        body["finish_time_ms"] = task_status.finish_time_ms;
        body["queue_wait_ms"] = task_status.queue_wait_ms;
        body["inference_ms"] = task_status.infer_ms;
        body["total_ms"] = task_status.total_ms;
        if (!task_status.worker_id.empty()) {
            body["worker_id"] = task_status.worker_id;
        }
        if (!task_status.consumer_name.empty()) {
            body["consumer_name"] = task_status.consumer_name;
        }
        if (!task_status.input_image_key.empty()) {
            body["input_image_key"] = task_status.input_image_key;
        }
        if (!task_status.result_image_key.empty()) {
            body["result_image_key"] = task_status.result_image_key;
            body["result_image_url"] = "/api/v1/result/" + task_id + "/image";
        }

        if (!task_status.error.empty()) {
            body["error"] = task_status.error;
        }
        if (!task_status.result_image_filename.empty()) {
            body["result_image_filename"] = task_status.result_image_filename;
            body["legacy_result_image_url"] = "/api/v1/image/" + task_status.result_image_filename;
        }

        return makeJsonResponse(200, body);
    }

    crow::response HttpController::handleGetResultImageByTaskId(const std::string& task_id) const {
        if (!redis_mode_) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "REDIS_DISABLED";
            body["error"] = "result image by task_id requires redis.enabled=true";
            body["task_id"] = task_id;
            return makeJsonResponse(503, body);
        }

        const std::string image_key = redis_queue_.resultImageKey(task_id);
        std::string bytes;
        std::string redis_error;
        if (!redis_queue_.getBinaryValue(image_key, bytes, redis_error) || bytes.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "RESULT_IMAGE_NOT_FOUND";
            body["error"] = "result image not found or expired";
            body["task_id"] = task_id;
            body["redis_key"] = image_key;
            body["redis_error"] = redis_error;
            return makeJsonResponse(404, body);
        }

        crow::response response;
        response.code = 200;
        response.body = bytes;
        response.set_header("Content-Type", "image/jpeg");
        response.set_header("Cache-Control", "no-store");
        return response;
    }

    crow::response HttpController::handleGetResultImage(const std::string& filename) const {
        if (!isSafeImageFilename(filename)) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "INVALID_IMAGE_FILENAME";
            body["error"] = "invalid image filename";
            return makeJsonResponse(400, body);
        }

        const std::string image_path = makeResultImagePath(filename);
        std::string bytes = readWholeFileBinary(image_path);

        if (bytes.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error_code"] = "LOCAL_IMAGE_NOT_FOUND";
            body["error"] = "image not found";
            body["filename"] = filename;
            return makeJsonResponse(404, body);
        }

        crow::response response;
        response.code = 200;
        response.body = bytes;
        response.set_header("Content-Type", guessImageContentType(filename));
        response.set_header("Cache-Control", "no-store");
        return response;
    }

    bool HttpController::validateBodySize(const crow::request& request, std::string& error_message) const {
        if (config_.server.max_body_size_mb <= 0) {
            return true;
        }
        const size_t max_bytes = static_cast<size_t>(config_.server.max_body_size_mb) * 1024ULL * 1024ULL;
        if (request.body.size() > max_bytes) {
            error_message = "request body too large. max_body_size_mb=" + std::to_string(config_.server.max_body_size_mb);
            return false;
        }
        return true;
    }

    std::string HttpController::extractImageBytes(const crow::request& request, std::string& error_message) const {
        const std::string content_type = request.get_header_value("Content-Type");

        if (content_type.find("multipart/form-data") != std::string::npos) {
            try {
                crow::multipart::message multipart_message(request);
                auto part = multipart_message.get_part_by_name("file");
                if (part.body.empty()) {
                    error_message = "multipart field 'file' is empty or not found";
                    return {};
                }
                return part.body;
            }
            catch (const std::exception& e) {
                error_message = std::string("failed to parse multipart/form-data: ") + e.what();
                return {};
            }
        }

        if (content_type.find("image/jpeg") != std::string::npos ||
            content_type.find("image/jpg") != std::string::npos ||
            content_type.find("image/png") != std::string::npos ||
            content_type.find("application/octet-stream") != std::string::npos) {
            return request.body;
        }

        if (!request.body.empty()) {
            return request.body;
        }

        error_message = "request body is empty. Use multipart field name 'file' or raw jpg/png bytes.";
        return {};
    }

    bool HttpController::validateRedisImageSize(size_t bytes, const std::string& field_name, std::string& error_message) const {
        if (config_.redis.max_image_bytes <= 0) {
            return true;
        }
        if (static_cast<long long>(bytes) > config_.redis.max_image_bytes) {
            error_message = field_name + " too large. bytes=" + std::to_string(bytes) +
                ", max_image_bytes=" + std::to_string(config_.redis.max_image_bytes);
            return false;
        }
        return true;
    }

    bool HttpController::redisMemoryOverLimit(nlohmann::json* memory_json, std::string& error_message) const {
        if (!redis_mode_ || config_.redis.max_redis_used_memory_mb <= 0) {
            return false;
        }

        RedisMemoryStats memory_stats;
        std::string redis_error;
        if (!redis_queue_.getRedisMemoryStats(memory_stats, redis_error)) {
            return false;
        }

        if (memory_json != nullptr) {
            *memory_json = memoryStatsToJson(memory_stats);
            (*memory_json)["max_redis_used_memory_mb_config"] = config_.redis.max_redis_used_memory_mb;
        }

        if (memory_stats.used_memory_mb > static_cast<double>(config_.redis.max_redis_used_memory_mb)) {
            std::ostringstream oss;
            oss << "Redis used memory is over limit. used_memory_mb=" << memory_stats.used_memory_mb
                << ", max_redis_used_memory_mb=" << config_.redis.max_redis_used_memory_mb;
            error_message = oss.str();
            return true;
        }

        return false;
    }

    bool HttpController::isTrueParam(const char* value) const {
        if (value == nullptr) {
            return false;
        }
        return isTrueString(std::string(value));
    }

    std::string HttpController::makeTaskId() {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm_value{};

#ifdef _WIN32
        localtime_s(&tm_value, &now_time_t);
#else
        localtime_r(&now_time_t, &tm_value);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_value, "%Y%m%d_%H%M%S")
            << "_" << std::setw(3) << std::setfill('0') << ms
            << "_" << ++task_counter_;
        return oss.str();
    }

    std::string HttpController::makeResultImageFilename(unsigned long long request_id) const {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::ostringstream oss;
        oss << "result_" << ms << "_" << request_id << ".jpg";
        return oss.str();
    }

    std::string HttpController::makeInputImagePath(const std::string& task_id) const {
        std::filesystem::path input_dir(config_.output.input_dir);
        return (input_dir / (task_id + ".jpg")).string();
    }

    std::string HttpController::makeResultImagePath(const std::string& filename) const {
        std::filesystem::path output_dir(config_.output.output_dir);
        std::filesystem::path image_path = output_dir / filename;
        return image_path.string();
    }

    bool HttpController::isSafeImageFilename(const std::string& filename) const {
        if (filename.empty()) {
            return false;
        }
        if (filename.find("..") != std::string::npos) {
            return false;
        }
        if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
            return false;
        }

        const std::string lower = toLowerString(filename);
        const bool is_jpg = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".jpg";
        const bool is_jpeg = lower.size() >= 5 && lower.substr(lower.size() - 5) == ".jpeg";
        const bool is_png = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png";
        return is_jpg || is_jpeg || is_png;
    }

    std::string HttpController::guessImageContentType(const std::string& filename) const {
        const std::string lower = toLowerString(filename);
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png") {
            return "image/png";
        }
        return "image/jpeg";
    }

    long long HttpController::nowMs() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

}  // namespace yolo11_server
