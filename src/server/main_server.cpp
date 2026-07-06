#include <exception>
#include <iostream>
#include <memory>
#include <string>

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
#include "server/inference_service.h"
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
    std::unique_ptr<yolo11::Yolo11Detector> sync_detector;
    bool sync_detector_initialized = false;

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
            std::cerr << "Current server supports model.type = detect only." << std::endl;
            std::cerr << "Current model.type = " << app_config.model.type << std::endl;
            exit_code = -1;
        }
        else {
            if (app_config.server.enable_sync_detect) {
                sync_detector = std::make_unique<yolo11::Yolo11Detector>();

                yolo11::DetectorConfig sync_detector_config;
                sync_detector_config.engine_path = app_config.model.engine_path;
                sync_detector_config.gpu_id = app_config.model.gpu_id;
                sync_detector_config.use_gpu_postprocess = app_config.model.use_gpu_postprocess;

                std::cout << "Loading sync TensorRT engine in HTTP server: "
                    << sync_detector_config.engine_path << std::endl;

                if (!sync_detector->init(sync_detector_config)) {
                    std::cerr << "Failed to initialize sync Yolo11Detector." << std::endl;
                    exit_code = -1;
                }
                else {
                    sync_detector_initialized = true;
                }
            }
            else {
                std::cout << "Production mode: sync detector is not loaded by yolo11_server." << std::endl;
                std::cout << "Use yolo11_worker.exe to consume Redis Stream tasks." << std::endl;
            }

            if (exit_code == 0) {
                std::unique_ptr<yolo11_server::InferenceService> embedded_worker_service;
                if (app_config.worker.enabled) {
                    std::cout << "Warning: embedded workers are enabled inside yolo11_server."
                        << " This is useful for local debug, but production should run yolo11_worker.exe separately."
                        << std::endl;
                    embedded_worker_service = std::make_unique<yolo11_server::InferenceService>(app_config);
                    if (!embedded_worker_service->start()) {
                        std::cerr << "Failed to start embedded InferenceService." << std::endl;
                        exit_code = -1;
                    }
                }

                if (exit_code == 0) {
                    crow::SimpleApp app;
                    app.loglevel(crow::LogLevel::Warning);

                    yolo11_server::HttpController controller(app_config, sync_detector.get());
                    controller.registerRoutes(app);

                    std::cout << "YOLO11 HTTP server started." << std::endl;
                    std::cout << "Role: HTTP producer / query" << std::endl;
                    std::cout << "Queue backend: "
                        << (app_config.redis.enabled ? "Redis Stream" : "Disabled")
                        << std::endl;
                    std::cout << "Sync detect enabled: "
                        << (app_config.server.enable_sync_detect ? "true" : "false")
                        << std::endl;
                    std::cout << "Embedded worker pool size: "
                        << (embedded_worker_service ? embedded_worker_service->workerCount() : 0)
                        << std::endl;

                    std::cout << "Health API: http://"
                        << app_config.server.host << ":"
                        << app_config.server.port
                        << "/api/v1/health" << std::endl;

                    std::cout << "Async API: POST http://"
                        << app_config.server.host << ":"
                        << app_config.server.port
                        << "/api/v1/detect/image/async" << std::endl;

                    app.bindaddr(app_config.server.host)
                        .port(static_cast<uint16_t>(app_config.server.port))
                        .concurrency(static_cast<unsigned int>(app_config.server.threads))
                        .run();

                    if (embedded_worker_service) {
                        embedded_worker_service->stop();
                    }
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

    if (sync_detector_initialized && sync_detector) {
        try {
            sync_detector->release();
        }
        catch (const std::exception& e) {
            std::cerr << "Warning: sync_detector.release() failed: "
                << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "Warning: sync_detector.release() failed with unknown exception." << std::endl;
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return exit_code;
}
