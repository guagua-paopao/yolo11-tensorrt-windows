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
    if (config.server.threads <= 0) {
        config.server.threads = 1;
    }

    auto model = root["model"];
    config.model.type = readOrDefault<std::string>(model, "type", config.model.type);
    config.model.engine_path = readOrDefault<std::string>(model, "engine_path", config.model.engine_path);
    config.model.gpu_id = readOrDefault<int>(model, "gpu_id", config.model.gpu_id);
    config.model.use_gpu_postprocess = readOrDefault<bool>(model, "use_gpu_postprocess", config.model.use_gpu_postprocess);

    auto output = root["output"];
    config.output.save_result_image = readOrDefault<bool>(output, "save_result_image", config.output.save_result_image);
    config.output.output_dir = readOrDefault<std::string>(output, "output_dir", config.output.output_dir);
    config.output.jpeg_quality = readOrDefault<int>(output, "jpeg_quality", config.output.jpeg_quality);
    if (config.output.jpeg_quality < 1) {
        config.output.jpeg_quality = 1;
    }
    if (config.output.jpeg_quality > 100) {
        config.output.jpeg_quality = 100;
    }

    return config;
}

}  // namespace yolo11_server
