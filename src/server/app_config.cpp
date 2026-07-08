#include "server/app_config.h"

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

#include <yaml-cpp/yaml.h>

namespace yolo11_server {

    namespace {

        template <typename T>
        T readOrDefault(const YAML::Node& node, const std::string& key, const T& default_value) {
            if (!node || !node[key]) {
                return default_value;
            }
            return node[key].as<T>();
        }

        std::string toLowerString(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        std::string inferWorkerKind(const AppConfig& config) {
            if (config.stream.enabled) {
                return "stream";
            }
            if (config.video.enabled) {
                return "video";
            }
            return "image";
        }

        std::string inferTaskKind(const AppConfig& config) {
            if (config.stream.enabled) {
                return "live_stream";
            }
            if (config.video.enabled) {
                return "video_file";
            }
            return "image_async";
        }

        std::string inferStreamType(const AppConfig& config) {
            return config.stream.enabled ? std::string("long_running_stream") : std::string("redis_stream");
        }

        std::string inferProfileType(const AppConfig& config) {
            if (config.stream.enabled) {
                return "stream";
            }
            if (config.video.enabled) {
                return "video";
            }
            return toLowerString(config.model.type.empty() ? std::string("detect") : config.model.type);
        }

        std::string inferWorkerGroup(const AppConfig& config) {
            const std::string kind = inferWorkerKind(config);
            const std::string profile = inferProfileType(config);
            return kind + "_" + profile + "_gpu" + std::to_string(config.model.gpu_id);
        }

        void normalizeWorkerCapability(AppConfig& config) {
            if (config.worker.worker_kind.empty()) {
                config.worker.worker_kind = inferWorkerKind(config);
            }
            if (config.worker.task_kind.empty()) {
                config.worker.task_kind = inferTaskKind(config);
            }
            if (config.worker.stream_type.empty()) {
                config.worker.stream_type = inferStreamType(config);
            }
            if (config.worker.worker_group.empty()) {
                config.worker.worker_group = inferWorkerGroup(config);
            }
            if (config.worker.max_concurrency <= 0) {
                config.worker.max_concurrency = config.stream.enabled ? 1 : config.worker.worker_num;
            }
        }

    }  // namespace

    AppConfig AppConfig::loadFromYaml(const std::string& yaml_path) {
        AppConfig config;

        YAML::Node root;
        try {
            root = YAML::LoadFile(yaml_path);
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to load config file: " << yaml_path << std::endl;
            std::cerr << "Reason: " << e.what() << std::endl;
            std::cerr << "Use built-in default config instead." << std::endl;
            return config;
        }

        auto server = root["server"];
        config.server.host = readOrDefault<std::string>(server, "host", config.server.host);
        config.server.port = readOrDefault<int>(server, "port", config.server.port);
        config.server.threads = readOrDefault<int>(server, "threads", config.server.threads);
        config.server.enable_sync_detect = readOrDefault<bool>(server, "enable_sync_detect", config.server.enable_sync_detect);
        config.server.max_body_size_mb = readOrDefault<int>(server, "max_body_size_mb", config.server.max_body_size_mb);
        if (config.server.threads <= 0) {
            config.server.threads = 1;
        }
        if (config.server.port <= 0 || config.server.port > 65535) {
            config.server.port = 8080;
        }
        if (config.server.max_body_size_mb < 0) {
            config.server.max_body_size_mb = 0;
        }

        auto model = root["model"];
        config.model.type = readOrDefault<std::string>(model, "type", config.model.type);
        config.model.engine_path = readOrDefault<std::string>(model, "engine_path", config.model.engine_path);
        config.model.labels_path = readOrDefault<std::string>(model, "labels_path", config.model.labels_path);
        config.model.gpu_id = readOrDefault<int>(model, "gpu_id", config.model.gpu_id);
        config.model.use_gpu_postprocess = readOrDefault<bool>(model, "use_gpu_postprocess", config.model.use_gpu_postprocess);

        auto output = root["output"];
        config.output.save_result_image = readOrDefault<bool>(output, "save_result_image", config.output.save_result_image);
        config.output.input_dir = readOrDefault<std::string>(output, "input_dir", config.output.input_dir);
        config.output.output_dir = readOrDefault<std::string>(output, "output_dir", config.output.output_dir);
        config.output.jpeg_quality = readOrDefault<int>(output, "jpeg_quality", config.output.jpeg_quality);
        if (config.output.jpeg_quality < 1) {
            config.output.jpeg_quality = 1;
        }
        if (config.output.jpeg_quality > 100) {
            config.output.jpeg_quality = 100;
        }


        auto video = root["video"];
        config.video.enabled = readOrDefault<bool>(video, "enabled", config.video.enabled);
        config.video.input_dir = readOrDefault<std::string>(video, "input_dir", config.video.input_dir);
        config.video.output_dir = readOrDefault<std::string>(video, "output_dir", config.video.output_dir);
        config.video.max_video_bytes = readOrDefault<long long>(video, "max_video_bytes", config.video.max_video_bytes);
        config.video.progress_update_interval_frames = readOrDefault<int>(video, "progress_update_interval_frames", config.video.progress_update_interval_frames);
        config.video.max_process_frames = readOrDefault<int>(video, "max_process_frames", config.video.max_process_frames);
        config.video.output_extension = readOrDefault<std::string>(video, "output_extension", config.video.output_extension);
        config.video.output_fourcc = readOrDefault<std::string>(video, "output_fourcc", config.video.output_fourcc);
        config.video.fallback_fps = readOrDefault<double>(video, "fallback_fps", config.video.fallback_fps);
        if (config.video.input_dir.empty()) {
            config.video.input_dir = "./temp/video/input";
        }
        if (config.video.output_dir.empty()) {
            config.video.output_dir = "./temp/video/output";
        }
        if (config.video.max_video_bytes < 0) {
            config.video.max_video_bytes = 0;
        }
        if (config.video.progress_update_interval_frames <= 0) {
            config.video.progress_update_interval_frames = 30;
        }
        if (config.video.max_process_frames < 0) {
            config.video.max_process_frames = 0;
        }
        if (config.video.output_extension.empty()) {
            config.video.output_extension = ".mp4";
        }
        if (!config.video.output_extension.empty() && config.video.output_extension[0] != '.') {
            config.video.output_extension = "." + config.video.output_extension;
        }
        if (config.video.output_fourcc.size() != 4) {
            config.video.output_fourcc = "mp4v";
        }
        if (config.video.fallback_fps <= 0.0 || config.video.fallback_fps > 240.0) {
            config.video.fallback_fps = 25.0;
        }


        auto stream = root["stream"];
        config.stream.enabled = readOrDefault<bool>(stream, "enabled", config.stream.enabled);
        config.stream.default_source_type = readOrDefault<std::string>(stream, "default_source_type", config.stream.default_source_type);
        config.stream.default_camera_id = readOrDefault<int>(stream, "default_camera_id", config.stream.default_camera_id);
        config.stream.default_rtsp_url = readOrDefault<std::string>(stream, "default_rtsp_url", config.stream.default_rtsp_url);
        config.stream.default_file_path = readOrDefault<std::string>(stream, "default_file_path", config.stream.default_file_path);
        config.stream.snapshot_dir = readOrDefault<std::string>(stream, "snapshot_dir", config.stream.snapshot_dir);
        config.stream.snapshot_interval_frames = readOrDefault<int>(stream, "snapshot_interval_frames", config.stream.snapshot_interval_frames);
        config.stream.target_fps = readOrDefault<int>(stream, "target_fps", config.stream.target_fps);
        config.stream.max_no_frame_count = readOrDefault<int>(stream, "max_no_frame_count", config.stream.max_no_frame_count);
        config.stream.enable_reconnect = readOrDefault<bool>(stream, "enable_reconnect", config.stream.enable_reconnect);
        config.stream.reconnect_max_attempts = readOrDefault<int>(stream, "reconnect_max_attempts", config.stream.reconnect_max_attempts);
        config.stream.reconnect_delay_ms = readOrDefault<int>(stream, "reconnect_delay_ms", config.stream.reconnect_delay_ms);
        config.stream.max_runtime_seconds = readOrDefault<int>(stream, "max_runtime_seconds", config.stream.max_runtime_seconds);
        config.stream.jpeg_quality = readOrDefault<int>(stream, "jpeg_quality", config.stream.jpeg_quality);
        if (config.stream.default_source_type.empty()) {
            config.stream.default_source_type = "camera";
        }
        if (config.stream.default_camera_id < 0) {
            config.stream.default_camera_id = 0;
        }
        if (config.stream.snapshot_dir.empty()) {
            config.stream.snapshot_dir = "./runtime/output/streams";
        }
        if (config.stream.snapshot_interval_frames <= 0) {
            config.stream.snapshot_interval_frames = 5;
        }
        if (config.stream.target_fps < 0) {
            config.stream.target_fps = 0;
        }
        if (config.stream.target_fps > 120) {
            config.stream.target_fps = 120;
        }
        if (config.stream.max_no_frame_count <= 0) {
            config.stream.max_no_frame_count = 30;
        }
        if (config.stream.reconnect_max_attempts < 0) {
            config.stream.reconnect_max_attempts = 0;
        }
        if (config.stream.reconnect_max_attempts > 100) {
            config.stream.reconnect_max_attempts = 100;
        }
        if (config.stream.reconnect_delay_ms < 100) {
            config.stream.reconnect_delay_ms = 100;
        }
        if (config.stream.reconnect_delay_ms > 60000) {
            config.stream.reconnect_delay_ms = 60000;
        }
        if (config.stream.max_runtime_seconds < 0) {
            config.stream.max_runtime_seconds = 0;
        }
        if (config.stream.jpeg_quality < 1) {
            config.stream.jpeg_quality = 1;
        }
        if (config.stream.jpeg_quality > 100) {
            config.stream.jpeg_quality = 100;
        }

        auto redis = root["redis"];
        config.redis.enabled = readOrDefault<bool>(redis, "enabled", config.redis.enabled);
        config.redis.host = readOrDefault<std::string>(redis, "host", config.redis.host);
        config.redis.port = readOrDefault<int>(redis, "port", config.redis.port);
        config.redis.password = readOrDefault<std::string>(redis, "password", config.redis.password);
        config.redis.db = readOrDefault<int>(redis, "db", config.redis.db);
        config.redis.stream_key = readOrDefault<std::string>(redis, "stream_key", config.redis.stream_key);
        config.redis.consumer_group = readOrDefault<std::string>(redis, "consumer_group", config.redis.consumer_group);
        config.redis.consumer_name = readOrDefault<std::string>(redis, "consumer_name", config.redis.consumer_name);
        config.redis.block_ms = readOrDefault<int>(redis, "block_ms", config.redis.block_ms);
        config.redis.ttl_seconds = readOrDefault<int>(redis, "ttl_seconds", config.redis.ttl_seconds);
        config.redis.task_ttl_seconds = readOrDefault<int>(redis, "task_ttl_seconds", config.redis.task_ttl_seconds);
        config.redis.input_image_ttl_seconds = readOrDefault<int>(redis, "input_image_ttl_seconds", config.redis.input_image_ttl_seconds);
        config.redis.result_image_ttl_seconds = readOrDefault<int>(redis, "result_image_ttl_seconds", config.redis.result_image_ttl_seconds);
        config.redis.max_image_bytes = readOrDefault<long long>(redis, "max_image_bytes", config.redis.max_image_bytes);
        config.redis.max_result_image_bytes = readOrDefault<long long>(redis, "max_result_image_bytes", config.redis.max_result_image_bytes);
        config.redis.delete_input_after_done = readOrDefault<bool>(redis, "delete_input_after_done", config.redis.delete_input_after_done);
        config.redis.max_redis_used_memory_mb = readOrDefault<long long>(redis, "max_redis_used_memory_mb", config.redis.max_redis_used_memory_mb);
        config.redis.stream_max_len = readOrDefault<long long>(redis, "stream_max_len", config.redis.stream_max_len);
        config.redis.enable_pending_reclaim = readOrDefault<bool>(redis, "enable_pending_reclaim", config.redis.enable_pending_reclaim);
        config.redis.pending_min_idle_ms = readOrDefault<long long>(redis, "pending_min_idle_ms", config.redis.pending_min_idle_ms);

        auto metrics = root["metrics"];
        config.redis.metrics_enabled = readOrDefault<bool>(metrics, "enabled", config.redis.metrics_enabled);
        config.redis.metrics_recent_window_seconds = readOrDefault<int>(metrics, "recent_window_seconds", config.redis.metrics_recent_window_seconds);
        config.redis.metrics_ttl_seconds = readOrDefault<int>(metrics, "ttl_seconds", config.redis.metrics_ttl_seconds);

        if (config.redis.port <= 0) {
            config.redis.port = 6379;
        }
        if (config.redis.db < 0) {
            config.redis.db = 0;
        }
        if (config.redis.block_ms < 100) {
            config.redis.block_ms = 100;
        }
        if (config.redis.ttl_seconds < 60) {
            config.redis.ttl_seconds = 60;
        }
        if (config.redis.task_ttl_seconds <= 0) {
            config.redis.task_ttl_seconds = config.redis.ttl_seconds;
        }
        if (config.redis.task_ttl_seconds < 60) {
            config.redis.task_ttl_seconds = 60;
        }
        if (config.redis.input_image_ttl_seconds < 60) {
            config.redis.input_image_ttl_seconds = 60;
        }
        if (config.redis.result_image_ttl_seconds < 60) {
            config.redis.result_image_ttl_seconds = 60;
        }
        if (config.redis.max_image_bytes < 0) {
            config.redis.max_image_bytes = 0;
        }
        if (config.redis.max_result_image_bytes < 0) {
            config.redis.max_result_image_bytes = 0;
        }
        if (config.redis.max_redis_used_memory_mb < 0) {
            config.redis.max_redis_used_memory_mb = 0;
        }
        if (config.redis.stream_max_len < 0) {
            config.redis.stream_max_len = 0;
        }
        if (config.redis.pending_min_idle_ms < 1000) {
            config.redis.pending_min_idle_ms = 1000;
        }
        if (config.redis.metrics_recent_window_seconds < 1) {
            config.redis.metrics_recent_window_seconds = 60;
        }
        if (config.redis.metrics_ttl_seconds < 60) {
            config.redis.metrics_ttl_seconds = 60;
        }
        if (config.redis.stream_key.empty()) {
            config.redis.stream_key = "yolo:stream:detect";
        }
        if (config.redis.consumer_group.empty()) {
            config.redis.consumer_group = "yolo11_group";
        }
        if (config.redis.consumer_name.empty()) {
            config.redis.consumer_name = "worker_1";
        }

        auto logging = root["logging"];
        config.logging.enabled = readOrDefault<bool>(logging, "enabled", config.logging.enabled);
        config.logging.log_dir = readOrDefault<std::string>(logging, "log_dir", config.logging.log_dir);
        config.logging.level = readOrDefault<std::string>(logging, "level", config.logging.level);
        config.logging.console = readOrDefault<bool>(logging, "console", config.logging.console);
        config.logging.file = readOrDefault<bool>(logging, "file", config.logging.file);
        config.logging.flush_interval_sec = readOrDefault<int>(logging, "flush_interval_sec", config.logging.flush_interval_sec);
        config.logging.max_file_size_mb = readOrDefault<int>(logging, "max_file_size_mb", config.logging.max_file_size_mb);
        config.logging.max_files = readOrDefault<int>(logging, "max_files", config.logging.max_files);
        if (config.logging.log_dir.empty()) {
            config.logging.log_dir = "./logs";
        }
        if (config.logging.flush_interval_sec < 0) {
            config.logging.flush_interval_sec = 0;
        }
        if (config.logging.max_file_size_mb <= 0) {
            config.logging.max_file_size_mb = 50;
        }
        if (config.logging.max_files <= 0) {
            config.logging.max_files = 5;
        }

        auto worker = root["worker"];
        config.worker.enabled = readOrDefault<bool>(worker, "enabled", config.worker.enabled);
        config.worker.worker_num = readOrDefault<int>(worker, "worker_num", config.worker.worker_num);
        config.worker.min_alive_workers = readOrDefault<int>(worker, "min_alive_workers", config.worker.min_alive_workers);
        config.worker.consumer_name_prefix = readOrDefault<std::string>(
            worker,
            "consumer_name_prefix",
            config.worker.consumer_name_prefix
        );
        config.worker.log_task_done = readOrDefault<bool>(
            worker,
            "log_task_done",
            config.worker.log_task_done
        );
        config.worker.worker_group = readOrDefault<std::string>(
            worker,
            "worker_group",
            config.worker.worker_group
        );
        config.worker.worker_kind = readOrDefault<std::string>(
            worker,
            "worker_kind",
            config.worker.worker_kind
        );
        config.worker.task_kind = readOrDefault<std::string>(
            worker,
            "task_kind",
            config.worker.task_kind
        );
        config.worker.stream_type = readOrDefault<std::string>(
            worker,
            "stream_type",
            config.worker.stream_type
        );
        config.worker.max_concurrency = readOrDefault<int>(
            worker,
            "max_concurrency",
            config.worker.max_concurrency
        );
        config.worker.heartbeat_enabled = readOrDefault<bool>(
            worker,
            "heartbeat_enabled",
            config.worker.heartbeat_enabled
        );
        config.worker.heartbeat_interval_ms = readOrDefault<int>(
            worker,
            "heartbeat_interval_ms",
            config.worker.heartbeat_interval_ms
        );
        config.worker.heartbeat_ttl_seconds = readOrDefault<int>(
            worker,
            "heartbeat_ttl_seconds",
            config.worker.heartbeat_ttl_seconds
        );

        if (config.worker.worker_num <= 0) {
            config.worker.worker_num = 1;
        }
        if (config.worker.worker_num > 16) {
            std::cerr << "worker.worker_num is too large, clamp to 16." << std::endl;
            config.worker.worker_num = 16;
        }
        if (config.worker.min_alive_workers <= 0) {
            config.worker.min_alive_workers = 1;
        }
        if (config.worker.min_alive_workers > config.worker.worker_num) {
            config.worker.min_alive_workers = config.worker.worker_num;
        }
        if (config.worker.consumer_name_prefix.empty()) {
            config.worker.consumer_name_prefix = "worker_";
        }
        if (config.worker.heartbeat_interval_ms < 500) {
            config.worker.heartbeat_interval_ms = 500;
        }
        if (config.worker.heartbeat_ttl_seconds < 3) {
            config.worker.heartbeat_ttl_seconds = 3;
        }
        if (config.worker.max_concurrency <= 0) {
            config.worker.max_concurrency = 1;
        }


        // Phase 10.5: optional multi-model profile registry.
        // Example:
        // active_model: "obb"
        // models:
        //   obb:
        //     type: "obb"
        //     engine_path: "..."
        //     labels_path: "..."
        //     stream_key: "yolo:stream:obb"
        //     consumer_group: "yolo11_obb_group"
        //     consumer_name_prefix: "obb_worker_"
        auto models = root["models"];
        if (models && models.IsMap()) {
            for (const auto& entry : models) {
                const std::string name = entry.first.as<std::string>();
                const YAML::Node node = entry.second;
                ModelProfile profile;
                profile.name = name;
                profile.type = readOrDefault<std::string>(node, "type", name);
                profile.engine_path = readOrDefault<std::string>(node, "engine_path", config.model.engine_path);
                profile.labels_path = readOrDefault<std::string>(node, "labels_path", config.model.labels_path);
                profile.gpu_id = readOrDefault<int>(node, "gpu_id", config.model.gpu_id);
                profile.use_gpu_postprocess = readOrDefault<bool>(node, "use_gpu_postprocess", config.model.use_gpu_postprocess);
                profile.stream_key = readOrDefault<std::string>(node, "stream_key", config.redis.stream_key);
                profile.consumer_group = readOrDefault<std::string>(node, "consumer_group", config.redis.consumer_group);
                profile.consumer_name_prefix = readOrDefault<std::string>(node, "consumer_name_prefix", config.worker.consumer_name_prefix);
                profile.worker_num = readOrDefault<int>(node, "worker_num", config.worker.worker_num);
                profile.min_alive_workers = readOrDefault<int>(node, "min_alive_workers", config.worker.min_alive_workers);
                config.model_profiles[name] = profile;
            }
        }

        config.active_model = readOrDefault<std::string>(root, "active_model", config.active_model);
        if (!config.active_model.empty()) {
            const auto it = config.model_profiles.find(config.active_model);
            if (it == config.model_profiles.end()) {
                std::cerr << "active_model='" << config.active_model
                          << "' is not found in models. Use legacy model/redis/worker sections." << std::endl;
            }
            else {
                const ModelProfile& profile = it->second;
                config.model.type = profile.type;
                config.model.engine_path = profile.engine_path;
                config.model.labels_path = profile.labels_path;
                config.model.gpu_id = profile.gpu_id;
                config.model.use_gpu_postprocess = profile.use_gpu_postprocess;
                config.redis.stream_key = profile.stream_key;
                config.redis.consumer_group = profile.consumer_group;
                config.worker.consumer_name_prefix = profile.consumer_name_prefix;
                config.worker.worker_num = profile.worker_num;
                config.worker.min_alive_workers = profile.min_alive_workers;

                // Keep consumer_name only as a default. yolo11_worker.exe normally overrides it by
                // --consumer-name. For convenience, derive one when the YAML did not set it clearly.
                if (config.redis.consumer_name.empty() || config.redis.consumer_name == "worker_1") {
                    config.redis.consumer_name = config.worker.consumer_name_prefix + "1";
                }
            }
        }

        // Re-validate fields that may have been overridden by an active model profile.
        if (config.worker.worker_num <= 0) {
            config.worker.worker_num = 1;
        }
        if (config.worker.worker_num > 16) {
            std::cerr << "worker.worker_num is too large, clamp to 16." << std::endl;
            config.worker.worker_num = 16;
        }
        if (config.worker.min_alive_workers <= 0) {
            config.worker.min_alive_workers = 1;
        }
        if (config.worker.min_alive_workers > config.worker.worker_num) {
            config.worker.min_alive_workers = config.worker.worker_num;
        }
        if (config.worker.consumer_name_prefix.empty()) {
            config.worker.consumer_name_prefix = "worker_";
        }
        if (config.redis.stream_key.empty()) {
            config.redis.stream_key = config.model.type == "obb" ? "yolo:stream:obb" : "yolo:stream:detect";
        }
        if (config.redis.consumer_group.empty()) {
            config.redis.consumer_group = config.model.type == "obb" ? "yolo11_obb_group" : "yolo11_group";
        }

        normalizeWorkerCapability(config);
        return config;
    }

}  // namespace yolo11_server
