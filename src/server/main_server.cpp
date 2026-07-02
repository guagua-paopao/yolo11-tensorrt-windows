#include <iostream>
#include <string>
#include <exception>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#include <crow.h>
#include <opencv2/core/utils/logger.hpp>

#include "server/app_config.h"
#include "server/http_controller.h"
#include "yolo11_detector_api.h"

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa_data;
    const int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_result != 0) {
        std::cerr << "WSAStartup failed: " << wsa_result << std::endl;
        return -1;
    }
#endif

    int exit_code = 0;
    yolo11::Yolo11Detector detector;
    bool detector_initialized = false;

    try {
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

        std::string config_path = "config/server.yaml";
        if (argc >= 2) {
            config_path = argv[1];
        }

        std::cout << "Config path: " << config_path << std::endl;

        yolo11_server::AppConfig app_config =
            yolo11_server::AppConfig::loadFromYaml(config_path);

        if (app_config.model.type != "detect") {
            std::cerr << "Phase 2 currently supports model.type = detect only." << std::endl;
            std::cerr << "Current model.type = " << app_config.model.type << std::endl;
            exit_code = -1;
        }
        else {
            yolo11::DetectorConfig detector_config;
            detector_config.engine_path = app_config.model.engine_path;
            detector_config.gpu_id = app_config.model.gpu_id;
            detector_config.use_gpu_postprocess = app_config.model.use_gpu_postprocess;

            std::cout << "Loading TensorRT engine: "
                << detector_config.engine_path << std::endl;

            if (!detector.init(detector_config)) {
                std::cerr << "Failed to initialize Yolo11Detector." << std::endl;
                exit_code = -1;
            }
            else {
                detector_initialized = true;

                {
                    crow::SimpleApp app;
                    yolo11_server::HttpController controller(app_config, detector);
                    controller.registerRoutes(app);

                    std::cout << "YOLO11 server started." << std::endl;
                    std::cout << "Queue backend: "
                        << (app_config.redis.enabled ? "Redis Stream" : "Local memory")
                        << std::endl;

                    std::cout << "Health API: http://"
                        << app_config.server.host << ":"
                        << app_config.server.port
                        << "/api/v1/health" << std::endl;

                    std::cout << "Detect API: POST http://"
                        << app_config.server.host << ":"
                        << app_config.server.port
                        << "/api/v1/detect/image" << std::endl;

                    std::cout << "Async API: POST http://"
                        << app_config.server.host << ":"
                        << app_config.server.port
                        << "/api/v1/detect/image/async" << std::endl;

                    app.bindaddr(app_config.server.host)
                        .port(static_cast<uint16_t>(app_config.server.port))
                        .concurrency(static_cast<unsigned int>(app_config.server.threads))
                        .run();
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << std::endl;
        std::cerr << "Fatal error: " << e.what() << std::endl;
        exit_code = -1;
    }
    catch (...) {
        std::cerr << std::endl;
        std::cerr << "Fatal error: unknown exception" << std::endl;
        exit_code = -1;
    }

    if (detector_initialized) {
        try {
            detector.release();
        }
        catch (const std::exception& e) {
            std::cerr << "Warning: detector.release() failed: "
                << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "Warning: detector.release() failed with unknown exception." << std::endl;
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return exit_code;
}
