#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include <crow.h>

#include "server/app_config.h"
#include "yolo11_detector_api.h"

namespace yolo11_server {

    class HttpController {
    public:
        HttpController(const AppConfig& config, yolo11::Yolo11Detector& detector);

        void registerRoutes(crow::SimpleApp& app);

    private:
        crow::response handleHealth() const;
        crow::response handleDetectImage(const crow::request& request);
        crow::response handleGetResultImage(const std::string& filename) const;

        std::string extractImageBytes(
            const crow::request& request,
            std::string& error_message
        ) const;

        bool isTrueParam(const char* value) const;

        std::string makeResultImageFilename(
            unsigned long long request_id
        ) const;

        std::string makeResultImagePath(
            const std::string& filename
        ) const;

        bool isSafeImageFilename(
            const std::string& filename
        ) const;

        std::string guessImageContentType(
            const std::string& filename
        ) const;

    private:
        const AppConfig& config_;
        yolo11::Yolo11Detector& detector_;

        // Phase 1 uses one shared Detector instance.
        // This mutex serializes TensorRT access and avoids sharing context/stream concurrently.
        std::mutex detector_mutex_;

        mutable std::atomic<unsigned long long> request_counter_{ 0 };
    };

}  // namespace yolo11_server