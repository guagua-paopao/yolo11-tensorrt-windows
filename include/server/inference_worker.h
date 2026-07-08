#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "server/app_config.h"
#include "server/redis_task_queue.h"
#include "server/label_map.h"
#include "server/model_runner.h"

namespace yolo11_server {

    // One InferenceWorker = one Redis consumer + one independent TensorRT detector.
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
        bool initModelRunner();
        bool isObbModel() const;
        bool isClsModel() const;
        bool isPoseModel() const;
        bool isSegModel() const;

        void heartbeatLoop() noexcept;
        void writeHeartbeatNoexcept() noexcept;
        void setWorkerStatus(const std::string& status, const std::string& current_task_id = std::string());
        void setLastError(const std::string& error_message);

        std::string makeResultImageFilename(const std::string& task_id) const;
        std::string makeResultImagePath(const std::string& filename) const;
        static long long nowMs();

    private:
        int worker_id_ = 0;
        AppConfig config_;
        RedisTaskQueue redis_queue_;
        std::unique_ptr<IModelRunner> runner_;
        LabelMap label_map_;

        std::thread thread_;
        std::thread heartbeat_thread_;
        std::atomic<bool> running_{ false };
        bool runner_initialized_ = false;

        mutable std::mutex state_mutex_;
        std::string worker_status_ = "starting";
        std::string current_task_id_;
        std::string last_error_;
        long long start_time_ms_ = 0;
        std::atomic<long long> processed_count_{ 0 };
        std::atomic<long long> failed_count_{ 0 };
    };

}  // namespace yolo11_server
