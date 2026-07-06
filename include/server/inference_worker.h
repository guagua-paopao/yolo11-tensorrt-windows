#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "server/app_config.h"
#include "server/redis_task_queue.h"
#include "yolo11_detector_api.h"

namespace yolo11_server {

    // One InferenceWorker = one Redis consumer + one independent Yolo11Detector.
    // Do not share TensorRT execution context / CUDA stream / CUDA buffer across worker threads.
    class InferenceWorker {
    public:
        InferenceWorker(int worker_id, const AppConfig& config, const std::string& consumer_name);
        ~InferenceWorker() noexcept;

        InferenceWorker(const InferenceWorker&) = delete;
        InferenceWorker& operator=(const InferenceWorker&) = delete;

        bool start();
        void stop() noexcept;

        bool running() const;
        std::string consumerName() const;

    private:
        void loop();
        void processRedisTask(const RedisTask& task);
        void releaseDetectorNoexcept() noexcept;

        std::string makeResultImageFilename(const std::string& task_id) const;
        std::string makeResultImagePath(const std::string& filename) const;
        static long long nowMs();

    private:
        int worker_id_ = 0;
        AppConfig config_;
        RedisTaskQueue redis_queue_;
        yolo11::Yolo11Detector detector_;

        std::thread thread_;
        std::atomic<bool> running_{ false };
        bool detector_initialized_ = false;
    };

}  // namespace yolo11_server
