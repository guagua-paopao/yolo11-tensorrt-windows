#include "server/app_config.h"

#include <iostream>
#include <stdexcept>

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
        config.redis.stream_max_len = readOrDefault<long long>(redis, "stream_max_len", config.redis.stream_max_len);
        config.redis.enable_pending_reclaim = readOrDefault<bool>(redis, "enable_pending_reclaim", config.redis.enable_pending_reclaim);
        config.redis.pending_min_idle_ms = readOrDefault<long long>(redis, "pending_min_idle_ms", config.redis.pending_min_idle_ms);

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
        if (config.redis.stream_max_len < 0) {
            config.redis.stream_max_len = 0;
        }
        if (config.redis.pending_min_idle_ms < 1000) {
            config.redis.pending_min_idle_ms = 1000;
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

        auto worker = root["worker"];
        config.worker.enabled = readOrDefault<bool>(worker, "enabled", config.worker.enabled);
        config.worker.worker_num = readOrDefault<int>(worker, "worker_num", config.worker.worker_num);
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

        if (config.worker.worker_num <= 0) {
            config.worker.worker_num = 1;
        }
        if (config.worker.worker_num > 16) {
            std::cerr << "worker.worker_num is too large, clamp to 16." << std::endl;
            config.worker.worker_num = 16;
        }
        if (config.worker.consumer_name_prefix.empty()) {
            config.worker.consumer_name_prefix = "worker_";
        }

        return config;
    }

}  // namespace yolo11_server
