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
#include <spdlog/spdlog.h>

#include "server/app_config.h"
#include "server/app_logger.h"
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

        yolo11_server::AppConfig app_config =
            yolo11_server::AppConfig::loadFromYaml(config_path);

        std::string logger_error;
        if (!yolo11_server::initializeLogger(app_config, "server", logger_error)) {
            spdlog::warn("Failed to initialize spdlog logger: {}", logger_error);
        }
        spdlog::info("Config path: {}", config_path);

        if (app_config.model.type != "detect") {
            spdlog::error("Current server supports model.type=detect only. Current model.type={}", app_config.model.type);
            exit_code = -1;
        }
        else {
            if (app_config.server.enable_sync_detect) {
                sync_detector = std::make_unique<yolo11::Yolo11Detector>();

                yolo11::DetectorConfig sync_detector_config;
                sync_detector_config.engine_path = app_config.model.engine_path;
                sync_detector_config.gpu_id = app_config.model.gpu_id;
                sync_detector_config.use_gpu_postprocess = app_config.model.use_gpu_postprocess;

                spdlog::info("Loading sync TensorRT engine in HTTP server: {}", sync_detector_config.engine_path);

                if (!sync_detector->init(sync_detector_config)) {
                    spdlog::error("Failed to initialize sync Yolo11Detector.");
                    exit_code = -1;
                }
                else {
                    sync_detector_initialized = true;
                }
            }
            else {
                spdlog::info("Production mode: sync detector is not loaded by yolo11_server.");
                spdlog::info("Use yolo11_worker.exe to consume Redis Stream tasks.");
            }

            if (exit_code == 0) {
                std::unique_ptr<yolo11_server::InferenceService> embedded_worker_service;
                if (app_config.worker.enabled) {
                    spdlog::warn("Embedded workers are enabled inside yolo11_server. This is useful for local debug, but production should run yolo11_worker.exe separately.");
                    embedded_worker_service = std::make_unique<yolo11_server::InferenceService>(app_config);
                    if (!embedded_worker_service->start()) {
                        spdlog::error("Failed to start embedded InferenceService.");
                        exit_code = -1;
                    }
                }

                if (exit_code == 0) {
                    crow::SimpleApp app;
                    app.loglevel(crow::LogLevel::Warning);

                    yolo11_server::HttpController controller(app_config, sync_detector.get());
                    controller.registerRoutes(app);

                    spdlog::info("YOLO11 HTTP server started.");
                    spdlog::info("Role: HTTP producer / query");
                    spdlog::info("Queue backend: {}", app_config.redis.enabled ? "Redis Stream" : "Disabled");
                    spdlog::info("Sync detect enabled: {}", app_config.server.enable_sync_detect ? "true" : "false");
                    spdlog::info("Embedded worker pool size: {}", embedded_worker_service ? embedded_worker_service->workerCount() : 0);
                    spdlog::info("Health API: http://{}:{}/api/v1/health", app_config.server.host, app_config.server.port);
                    spdlog::info("Ready API: http://{}:{}/api/v1/ready", app_config.server.host, app_config.server.port);
                    spdlog::info("Workers API: http://{}:{}/api/v1/workers", app_config.server.host, app_config.server.port);
                    spdlog::info("Metrics API: http://{}:{}/api/v1/metrics", app_config.server.host, app_config.server.port);
                    spdlog::info("Async API: POST http://{}:{}/api/v1/detect/image/async", app_config.server.host, app_config.server.port);

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
        spdlog::error("Fatal error: {}", e.what());
        exit_code = -1;
    }
    catch (...) {
        spdlog::error("Fatal error: unknown exception");
        exit_code = -1;
    }

    if (sync_detector_initialized && sync_detector) {
        try {
            sync_detector->release();
        }
        catch (const std::exception& e) {
            spdlog::warn("sync_detector.release() failed: {}", e.what());
        }
        catch (...) {
            spdlog::warn("sync_detector.release() failed with unknown exception.");
        }
    }

    yolo11_server::shutdownLogger();

#ifdef _WIN32
    WSACleanup();
#endif

    return exit_code;
}
