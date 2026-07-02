#pragma once

#include <string>

namespace yolo11_server {

    struct ServerSection {
        std::string host = "0.0.0.0";
        int port = 8080;
        int threads = 4;
    };

    struct ModelSection {
        std::string type = "detect";
        std::string engine_path = "./engines/yolo11n.engine";
        int gpu_id = 0;
        bool use_gpu_postprocess = false;
    };

    struct OutputSection {
        bool save_result_image = true;
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
    };

    struct AppConfig {
        ServerSection server;
        ModelSection model;
        OutputSection output;
        RedisSection redis;

        static AppConfig loadFromYaml(const std::string& yaml_path);
    };

}  // namespace yolo11_server
