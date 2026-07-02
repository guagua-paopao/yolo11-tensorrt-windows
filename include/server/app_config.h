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
    std::string output_dir = "./output";
    int jpeg_quality = 90;
};

struct AppConfig {
    ServerSection server;
    ModelSection model;
    OutputSection output;

    static AppConfig loadFromYaml(const std::string& yaml_path);
};

}  // namespace yolo11_server
