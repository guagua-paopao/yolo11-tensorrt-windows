#pragma once

#include <string>

namespace yolo11_server {

    struct ServerSection {
        std::string host = "0.0.0.0";
        int port = 8080;
        int threads = 4;

        // Production default: HTTP server is a producer/query process.
        // Keep sync detection disabled unless you really want the server process to load TensorRT.
        bool enable_sync_detect = false;

        // Request body guard. 0 means disabled.
        int max_body_size_mb = 16;
    };

    struct ModelSection {
        std::string type = "detect";
        std::string engine_path = "./engines/yolo11n.engine";
        int gpu_id = 0;
        bool use_gpu_postprocess = false;
    };

    struct OutputSection {
        // Generate and store a result image for async/sync detection.
        bool save_result_image = true;

        // Local paths are still kept for compatibility and debugging.
        // In production split mode, async workers prefer Redis image bytes first.
        std::string input_dir = "./temp/input";
        std::string output_dir = "./output";
        int jpeg_quality = 90;
    };

    struct RedisSection {
        bool enabled = true;
        std::string host = "127.0.0.1";
        int port = 6379;
        std::string password;
        int db = 0;
        std::string stream_key = "yolo:stream:detect";
        std::string consumer_group = "yolo11_group";
        std::string consumer_name = "worker_1";
        int block_ms = 1000;
        int ttl_seconds = 1800;

        // Keep Redis Stream length bounded. 0 means disabled.
        // Uses approximate trimming: XTRIM stream MAXLEN ~ stream_max_len.
        long long stream_max_len = 10000;

        // Reclaim messages delivered to a worker but not XACKed.
        bool enable_pending_reclaim = true;
        long long pending_min_idle_ms = 60000;
    };

    struct WorkerSection {
        // For all-in-one debug mode only. Production yolo11_server should use enabled=false.
        bool enabled = false;

        int worker_num = 1;
        std::string consumer_name_prefix = "worker_";
        bool log_task_done = true;
    };

    struct AppConfig {
        ServerSection server;
        ModelSection model;
        OutputSection output;
        RedisSection redis;
        WorkerSection worker;

        static AppConfig loadFromYaml(const std::string& yaml_path);
    };

}  // namespace yolo11_server
