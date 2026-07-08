#pragma once

#include <map>
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
        std::string labels_path = "./labels/coco.txt";
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

    struct VideoSection {
        // Phase 13: video-file async inference. Keep this false for normal image detect/OBB servers.
        bool enabled = false;
        std::string input_dir = "./temp/video/input";
        std::string output_dir = "./temp/video/output";

        // Request/file limits for video upload. 0 means disabled.
        long long max_video_bytes = 256LL * 1024LL * 1024LL;

        // Worker updates Redis progress every N frames.
        int progress_update_interval_frames = 30;

        // Limit for early experiments. 0 means process all frames.
        int max_process_frames = 0;

        // Output writer settings. mp4v works on most Windows OpenCV builds.
        std::string output_extension = ".mp4";
        std::string output_fourcc = "mp4v";
        double fallback_fps = 25.0;
    };


    struct StreamSection {
        // Phase 14: live stream / RTSP / camera task management.
        bool enabled = false;

        // camera / file / rtsp. Phase 14.0 can start with camera or file to validate lifecycle.
        std::string default_source_type = "camera";
        int default_camera_id = 0;
        std::string default_rtsp_url;
        std::string default_file_path;

        // Latest snapshot is overwritten periodically, not every frame.
        std::string snapshot_dir = "./runtime/output/streams";
        int snapshot_interval_frames = 5;
        int target_fps = 10;

        // Phase 14.5: stream stability controls.
        // max_no_frame_count is the short local tolerance before a reconnect/fail decision.
        int max_no_frame_count = 30;
        bool enable_reconnect = true;
        int reconnect_max_attempts = 3;
        int reconnect_delay_ms = 1000;

        // 0 means unlimited. Useful for smoke tests and long-run guards.
        int max_runtime_seconds = 0;

        // JPEG quality for snapshot output.
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

        // Default TTL used by task status/meta/result JSON when more specific TTLs are not set.
        int ttl_seconds = 1800;
        int task_ttl_seconds = 1800;

        // Redis Binary Image Storage controls.
        // input_image_ttl_seconds can be shorter because workers delete input image after XACK when enabled.
        int input_image_ttl_seconds = 600;
        int result_image_ttl_seconds = 1800;
        long long max_image_bytes = 5LL * 1024LL * 1024LL;
        long long max_result_image_bytes = 5LL * 1024LL * 1024LL;
        bool delete_input_after_done = true;

        // Optional Redis memory guard for /ready and async submit. 0 means disabled.
        long long max_redis_used_memory_mb = 2048;

        // Keep Redis Stream length bounded. 0 means disabled.
        // Uses approximate trimming: XTRIM stream MAXLEN ~ stream_max_len.
        long long stream_max_len = 10000;

        // Reclaim messages delivered to a worker but not XACKed.
        bool enable_pending_reclaim = true;
        long long pending_min_idle_ms = 60000;

        // Phase 8.5: runtime metrics stored in Redis.
        bool metrics_enabled = true;
        int metrics_recent_window_seconds = 60;
        int metrics_ttl_seconds = 3600;
    };

    struct LoggingSection {
        bool enabled = true;
        std::string log_dir = "./logs";
        std::string level = "info";
        bool console = true;
        bool file = true;
        int flush_interval_sec = 3;
        int max_file_size_mb = 50;
        int max_files = 5;
    };

    struct WorkerSection {
        // For all-in-one debug mode only. Production yolo11_server should use enabled=false.
        bool enabled = false;

        int worker_num = 1;
        int min_alive_workers = 1;
        std::string consumer_name_prefix = "worker_";
        bool log_task_done = true;

        // Phase 8: heartbeat written by yolo11_worker.exe / InferenceWorker.
        bool heartbeat_enabled = true;
        int heartbeat_interval_ms = 3000;
        int heartbeat_ttl_seconds = 15;
    };

    // Phase 10.5: optional model profile entry used by config/server_multimodel.yaml.
    // The current process still activates one model profile at a time; this keeps the
    // Server/Worker split simple while making detect/obb configuration explicit.
    struct ModelProfile {
        std::string name;
        std::string type;
        std::string engine_path;
        std::string labels_path;
        int gpu_id = 0;
        bool use_gpu_postprocess = false;
        std::string stream_key;
        std::string consumer_group;
        std::string consumer_name_prefix;
        int worker_num = 1;
        int min_alive_workers = 1;
    };

    struct AppConfig {
        ServerSection server;
        ModelSection model;
        OutputSection output;
        VideoSection video;
        StreamSection stream;
        RedisSection redis;
        LoggingSection logging;
        WorkerSection worker;

        // Optional multi-model profile registry.
        // active_model is empty for legacy single-model YAML.
        std::string active_model;
        std::map<std::string, ModelProfile> model_profiles;

        static AppConfig loadFromYaml(const std::string& yaml_path);
    };

}  // namespace yolo11_server
