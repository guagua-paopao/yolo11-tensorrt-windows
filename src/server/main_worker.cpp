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

#include "server/app_config.h"
#include "server/inference_service.h"
#include "server/inference_worker.h"

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
        g_stop_cv.wait(lock, []() {
            return g_stop_requested.load();
            });
    }

    [[noreturn]] void terminateHandler() noexcept {
        std::cerr << "Fatal worker error: std::terminate called." << std::endl;
        std::cerr.flush();
#ifdef _WIN32
        // Worker processes should not block deployment scripts with a CRT dialog.
        // Use a hard process exit after flushing logs.
        ExitProcess(static_cast<UINT>(EXIT_FAILURE));
#else
        std::_Exit(EXIT_FAILURE);
#endif
    }

#ifdef _WIN32
    void configureWindowsProcessForService() {
        // Avoid Windows error-reporting popup dialogs in unattended worker processes.
        SetErrorMode(
            SEM_FAILCRITICALERRORS |
            SEM_NOGPFAULTERRORBOX |
            SEM_NOOPENFILEERRORBOX
        );

        // Avoid the Microsoft Visual C++ Runtime Library abort dialog.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

#ifdef _DEBUG
        // In Debug builds, route CRT warnings/errors/asserts to stderr instead of modal dialogs.
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
            std::cout << "Stop signal received. Stopping YOLO11 worker..." << std::endl;
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
            << "  yolo11_worker.exe <config.yaml> [--worker-num N] [--consumer-name worker_1] [--gpu-id 0]\n\n"
            << "Examples:\n"
            << "  yolo11_worker.exe D:/tensorrtx/yolo11/config/server.yaml --worker-num 3\n"
            << "  yolo11_worker.exe D:/tensorrtx/yolo11/config/server.yaml --consumer-name worker_1\n";
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

        yolo11_server::AppConfig app_config =
            yolo11_server::AppConfig::loadFromYaml(config_path);

        app_config.redis.enabled = true;
        app_config.worker.enabled = true;

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

        if (app_config.model.type != "detect") {
            std::cerr << "yolo11_worker currently supports model.type=detect only. Current model.type="
                << app_config.model.type << std::endl;
#ifdef _WIN32
            WSACleanup();
#endif
            return -1;
        }

        std::cout << "YOLO11 worker process starting." << std::endl;
        std::cout << "Config path: " << config_path << std::endl;
        std::cout << "Redis: " << app_config.redis.host << ":" << app_config.redis.port
            << ", stream=" << app_config.redis.stream_key
            << ", group=" << app_config.redis.consumer_group << std::endl;
        std::cout << "Engine: " << app_config.model.engine_path << std::endl;
        std::cout << "GPU id: " << app_config.model.gpu_id << std::endl;

        if (single_consumer_mode) {
            std::cout << "Worker mode: single consumer, consumer_name="
                << consumer_name_override << std::endl;

            yolo11_server::InferenceWorker worker(1, app_config, consumer_name_override);
            if (!worker.start()) {
                std::cerr << "Failed to start worker: " << consumer_name_override << std::endl;
#ifdef _WIN32
                WSACleanup();
#endif
                return -1;
            }

            waitForStop();
            std::cout << "Stopping worker: " << consumer_name_override << std::endl;
            worker.stop();
        }
        else {
            std::cout << "Worker mode: internal worker pool, worker_num="
                << app_config.worker.worker_num << std::endl;

            yolo11_server::InferenceService service(app_config);
            if (!service.start()) {
                std::cerr << "Failed to start worker service." << std::endl;
#ifdef _WIN32
                WSACleanup();
#endif
                return -1;
            }

            waitForStop();
            std::cout << "Stopping worker service." << std::endl;
            service.stop();
        }

        std::cout << "YOLO11 worker process stopped." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal worker error: " << e.what() << std::endl;
        exit_code = -1;
    }
    catch (...) {
        std::cerr << "Fatal worker error: unknown exception" << std::endl;
        exit_code = -1;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return exit_code;
}
