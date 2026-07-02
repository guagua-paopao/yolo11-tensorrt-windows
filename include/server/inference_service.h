#pragma once

#include <memory>
#include <vector>

#include "server/app_config.h"
#include "server/inference_worker.h"

namespace yolo11_server {

    // InferenceService manages the lifecycle of multiple InferenceWorker instances.
    class InferenceService {
    public:
        explicit InferenceService(const AppConfig& config);
        ~InferenceService();

        InferenceService(const InferenceService&) = delete;
        InferenceService& operator=(const InferenceService&) = delete;

        bool start();
        void stop();

        int workerCount() const;
        bool running() const;

    private:
        AppConfig config_;
        std::vector<std::unique_ptr<InferenceWorker>> workers_;
        bool running_ = false;
    };

}  // namespace yolo11_server
