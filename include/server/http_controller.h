#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include <crow.h>
#include <nlohmann/json.hpp>

#include "server/app_config.h"
#include "server/redis_task_queue.h"
#include "server/label_map.h"
#include "yolo11_detector_api.h"

namespace yolo11_server {

    // Production role: HTTP producer + result query.
    // Sync detection is optional and should usually be disabled in production.
    class HttpController {
    public:
        HttpController(const AppConfig& config, yolo11::Yolo11Detector* detector);
        ~HttpController() = default;

        HttpController(const HttpController&) = delete;
        HttpController& operator=(const HttpController&) = delete;

        void registerRoutes(crow::SimpleApp& app);

    private:
        crow::response handleHealth() const;
        crow::response handleReady(const crow::request& request) const;
        crow::response handleWorkers(const crow::request& request) const;
        crow::response handleMetrics(const crow::request& request) const;
        crow::response handleDetectImage(const crow::request& request);
        crow::response handleDetectImageAsync(const crow::request& request);
        crow::response handleDetectObbImageAsync(const crow::request& request);
        crow::response handleImageAsync(const crow::request& request, const std::string& expected_model_type);
        crow::response handleGetAsyncResult(const std::string& task_id) const;
        crow::response handleGetResultImageByTaskId(const std::string& task_id) const;
        crow::response handleGetResultImage(const std::string& filename) const;

        std::string extractImageBytes(const crow::request& request, std::string& error_message) const;
        bool validateBodySize(const crow::request& request, std::string& error_message) const;
        bool validateRedisImageSize(size_t bytes, const std::string& field_name, std::string& error_message) const;
        bool redisMemoryOverLimit(nlohmann::json* memory_json, std::string& error_message) const;
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
        yolo11::Yolo11Detector* detector_ = nullptr;
        RedisTaskQueue redis_queue_;
        LabelMap label_map_;
        bool redis_mode_ = false;
        bool sync_enabled_ = false;

        mutable std::mutex sync_detector_mutex_;
        mutable std::atomic<unsigned long long> request_counter_{ 0 };
        std::atomic<unsigned long long> task_counter_{ 0 };
    };

}  // namespace yolo11_server
