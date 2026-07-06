#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include <crow.h>

#include "server/app_config.h"
#include "server/redis_task_queue.h"
#include "yolo11_detector_api.h"

namespace yolo11_server {

    // Phase 4: HttpController only handles the HTTP layer.
    // 1. The sync API uses the detector created in main_server.
    // 2. The async API saves input images, submits Redis Stream tasks, and queries Redis results.
    // 3. Redis consumption and async TensorRT inference are handled by InferenceService/InferenceWorker.
    class HttpController {
    public:
        HttpController(const AppConfig& config, yolo11::Yolo11Detector& detector);
        ~HttpController() = default;

        HttpController(const HttpController&) = delete;
        HttpController& operator=(const HttpController&) = delete;

        void registerRoutes(crow::SimpleApp& app);

    private:
        struct AsyncTaskRecord {
            std::string task_id;
            std::string status = "queued";
            std::string error;

            std::string input_image_path;
            std::string result_image_filename;
            std::string result_image_path;
            std::string result_json_text;

            long long create_time_ms = 0;
            long long start_time_ms = 0;
            long long finish_time_ms = 0;
        };

    private:
        crow::response handleHealth() const;
        crow::response handleDetectImage(const crow::request& request);
        crow::response handleDetectImageAsync(const crow::request& request);
        crow::response handleGetAsyncResult(const std::string& task_id) const;
        crow::response handleGetResultImage(const std::string& filename) const;

        std::string extractImageBytes(const crow::request& request, std::string& error_message) const;
        bool isTrueParam(const char* value) const;

        std::string makeTaskId();
        std::string makeResultImageFilename(unsigned long long request_id) const;
        std::string makeInputImagePath(const std::string& task_id) const;
        std::string makeResultImagePath(const std::string& filename) const;

        bool isSafeImageFilename(const std::string& filename) const;
        std::string guessImageContentType(const std::string& filename) const;

        static long long nowMs();

    private:
        const AppConfig& config_;
        yolo11::Yolo11Detector& detector_;
        RedisTaskQueue redis_queue_;
        bool redis_mode_ = false;

        mutable std::mutex sync_detector_mutex_;
        mutable std::atomic<unsigned long long> request_counter_{ 0 };
        std::atomic<unsigned long long> task_counter_{ 0 };
    };

}  // namespace yolo11_server
