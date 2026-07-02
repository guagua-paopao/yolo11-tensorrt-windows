#include <filesystem>
#include <iostream>
#include <string>

#include <crow.h>
#include <opencv2/core/utils/logger.hpp>

#include "server/app_config.h"
#include "server/http_controller.h"
#include "yolo11_detector_api.h"

int main(int argc, char** argv) {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    std::string config_path = "config/server.yaml";
    if (argc >= 2) {
        config_path = argv[1];
    }

    std::cout << "Config path: " << config_path << std::endl;

    yolo11_server::AppConfig app_config =
        yolo11_server::AppConfig::loadFromYaml(config_path);

    if (app_config.model.type != "detect") {
        std::cerr << "Phase 1/1.5 only supports model.type = detect." << std::endl;
        std::cerr << "Current model.type = " << app_config.model.type << std::endl;
        return -1;
    }

    std::filesystem::create_directories(app_config.output.input_dir);
    std::filesystem::create_directories(app_config.output.output_dir);

    yolo11::DetectorConfig detector_config;
    detector_config.engine_path = app_config.model.engine_path;
    detector_config.gpu_id = app_config.model.gpu_id;
    detector_config.use_gpu_postprocess = app_config.model.use_gpu_postprocess;

    std::cout << "Loading TensorRT engine: " << detector_config.engine_path << std::endl;

    yolo11::Yolo11Detector detector;
    if (!detector.init(detector_config)) {
        std::cerr << "Failed to initialize Yolo11Detector." << std::endl;
        return -1;
    }

    {
        crow::SimpleApp app;
        yolo11_server::HttpController controller(app_config, detector);
        controller.registerRoutes(app);

        std::cout << "YOLO11 server started." << std::endl;
        std::cout << "Health API: http://" << app_config.server.host << ":" << app_config.server.port
            << "/api/v1/health" << std::endl;
        std::cout << "Sync Detect API: POST http://" << app_config.server.host << ":" << app_config.server.port
            << "/api/v1/detect/image" << std::endl;
        std::cout << "Async Detect API: POST http://" << app_config.server.host << ":" << app_config.server.port
            << "/api/v1/detect/image/async" << std::endl;

        app.bindaddr(app_config.server.host)
            .port(static_cast<uint16_t>(app_config.server.port))
            .concurrency(static_cast<unsigned int>(app_config.server.threads))
            .run();
    }

    detector.release();
    return 0;
}
