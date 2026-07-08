#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#endif

#include <opencv2/core/utils/logger.hpp>
#include <spdlog/spdlog.h>

#include "server/app_config.h"
#include "server/app_logger.h"
#include "server/video_inference_worker.h"

namespace {

    std::atomic<bool> g_stop_requested{ false };
    std::mutex g_stop_mutex;
    std::condition_variable g_stop_cv;

    void requestStop() {
        g_stop_requested.store(true);
        g_stop_cv.notify_all();
    }

    void waitForStop() {
        std::unique_lock<std::mutex> lock(g_stop_mutex);
        g_stop_cv.wait(lock, []() { return g_stop_requested.load(); });
    }

    [[noreturn]] void terminateHandler() noexcept {
        std::cerr << "Fatal video worker error: std::terminate called." << std::endl;
        std::cerr.flush();
#ifdef _WIN32
        ExitProcess(static_cast<UINT>(EXIT_FAILURE));
#else
        std::_Exit(EXIT_FAILURE);
#endif
    }

#ifdef _WIN32
    void configureWindowsProcessForService() {
        SetErrorMode(
            SEM_FAILCRITICALERRORS |
            SEM_NOGPFAULTERRORBOX |
            SEM_NOOPENFILEERRORBOX
        );
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    }
#endif

#ifndef _WIN32
    void signalHandler(int) {
        requestStop();
    }
#else
    BOOL WINAPI consoleCtrlHandler(DWORD ctrl_type) {
        switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            std::cout << "Stop signal received. Stopping YOLO11 video worker..." << std::endl;
            requestStop();
            return TRUE;
        default:
            return FALSE;
        }
    }
#endif

    bool readIntArg(int argc, char** argv, const std::string& name, int& value) {
        for (int i = 2; i + 1 < argc; ++i) {
            if (argv[i] != nullptr && name == argv[i]) {
                try {
                    value = std::stoi(argv[i + 1]);
                    return true;
                }
                catch (...) {
                    return false;
                }
            }
        }
        return false;
    }

    bool readStringArg(int argc, char** argv, const std::string& name, std::string& value) {
        for (int i = 2; i + 1 < argc; ++i) {
            if (argv[i] != nullptr && name == argv[i]) {
                value = argv[i + 1];
                return true;
            }
        }
        return false;
    }

    void printUsage() {
        std::cout << "Usage:\n"
            << "  yolo11_video_worker.exe <config.yaml> [--worker-num N] [--consumer-name video_worker_1] [--gpu-id 0]\n\n"
            << "Examples:\n"
            << "  yolo11_video_worker.exe D:/tensorrtx/yolo11/config/worker_video_phase13.yaml --consumer-name video_worker_1\n"
            << "  yolo11_video_worker.exe D:/tensorrtx/yolo11/config/worker_video_phase13.yaml --worker-num 1\n";
    }

}  // namespace

int main(int argc, char** argv) {
    std::set_terminate(terminateHandler);

#ifdef _WIN32
    configureWindowsProcessForService();

    WSADATA wsa_data;
    const int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_result != 0) {
        std::cerr << "WSAStartup failed: " << wsa_result << std::endl;
        return -1;
    }
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif

    int exit_code = 0;

    try {
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

        if (argc < 2) {
            printUsage();
#ifdef _WIN32
            WSACleanup();
#endif
            return -1;
        }

        std::string config_path = argv[1];
        if (config_path == "--help" || config_path == "-h") {
            printUsage();
#ifdef _WIN32
            WSACleanup();
#endif
            return 0;
        }

        yolo11_server::AppConfig app_config = yolo11_server::AppConfig::loadFromYaml(config_path);
        app_config.redis.enabled = true;
        app_config.worker.enabled = true;
        app_config.video.enabled = true;
        app_config.model.type = "detect";

        std::string logger_error;
        if (!yolo11_server::initializeLogger(app_config, "video_worker", logger_error)) {
            spdlog::warn("Failed to initialize spdlog logger: {}", logger_error);
        }

        int worker_num_override = 0;
        if (readIntArg(argc, argv, "--worker-num", worker_num_override) && worker_num_override > 0) {
            app_config.worker.worker_num = worker_num_override;
        }

        int gpu_id_override = 0;
        if (readIntArg(argc, argv, "--gpu-id", gpu_id_override)) {
            app_config.model.gpu_id = gpu_id_override;
        }

        std::string consumer_name_override;
        const bool single_consumer_mode = readStringArg(argc, argv, "--consumer-name", consumer_name_override)
            && !consumer_name_override.empty();

        spdlog::info("YOLO11 video worker process starting.");
        spdlog::info("Config path: {}", config_path);
        spdlog::info("Redis: {}:{}, stream={}, group={}",
            app_config.redis.host, app_config.redis.port, app_config.redis.stream_key, app_config.redis.consumer_group);
        spdlog::info("Engine: {}", app_config.model.engine_path);
        spdlog::info("GPU id: {}", app_config.model.gpu_id);
        spdlog::info("Video input_dir={}, output_dir={}", app_config.video.input_dir, app_config.video.output_dir);

        std::vector<std::unique_ptr<yolo11_server::VideoInferenceWorker>> workers;
        if (single_consumer_mode) {
            spdlog::info("Video worker mode: single consumer, consumer_name={}", consumer_name_override);
            auto worker = std::make_unique<yolo11_server::VideoInferenceWorker>(1, app_config, consumer_name_override);
            if (!worker->start()) {
                spdlog::error("Failed to start video worker: {}", consumer_name_override);
#ifdef _WIN32
                WSACleanup();
#endif
                return -1;
            }
            workers.push_back(std::move(worker));
        }
        else {
            spdlog::info("Video worker mode: internal worker pool, worker_num={}", app_config.worker.worker_num);
            for (int i = 1; i <= app_config.worker.worker_num; ++i) {
                const std::string consumer_name = app_config.worker.consumer_name_prefix + std::to_string(i);
                auto worker = std::make_unique<yolo11_server::VideoInferenceWorker>(i, app_config, consumer_name);
                if (!worker->start()) {
                    spdlog::error("Failed to start video worker: {}", consumer_name);
#ifdef _WIN32
                    WSACleanup();
#endif
                    return -1;
                }
                workers.push_back(std::move(worker));
            }
        }

        waitForStop();
        spdlog::info("Stopping video workers...");
        for (auto& worker : workers) {
            if (worker) {
                worker->stop();
            }
        }
        spdlog::info("YOLO11 video worker process stopped.");
    }
    catch (const std::exception& e) {
        spdlog::error("Fatal video worker error: {}", e.what());
        exit_code = -1;
    }
    catch (...) {
        spdlog::error("Fatal video worker error: unknown exception");
        exit_code = -1;
    }

    yolo11_server::shutdownLogger();

#ifdef _WIN32
    WSACleanup();
#endif

    return exit_code;
}
