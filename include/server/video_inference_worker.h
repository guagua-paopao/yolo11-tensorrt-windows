#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "server/app_config.h"
#include "server/model_runner.h"
#include "server/redis_task_queue.h"

namespace yolo11_server {

    // Phase 13.0: one video worker consumes long video-file tasks from a dedicated Redis Stream.
    // Video bytes are intentionally stored as local files, not Redis binary keys.
    class VideoInferenceWorker {
    public:
        VideoInferenceWorker(int worker_id, const AppConfig& config, const std::string& consumer_name);
        ~VideoInferenceWorker() noexcept;

        VideoInferenceWorker(const VideoInferenceWorker&) = delete;
        VideoInferenceWorker& operator=(const VideoInferenceWorker&) = delete;

        bool start();
        void stop() noexcept;
        bool running() const;
        std::string consumerName() const;

    private:
        void loop();
        void processVideoTask(const RedisTask& task);
        bool initModelRunner();
        void releaseRunnerNoexcept() noexcept;

        void heartbeatLoop() noexcept;
        void writeHeartbeatNoexcept() noexcept;
        void setWorkerStatus(const std::string& status, const std::string& current_task_id = std::string());
        void setLastError(const std::string& error_message);

        static long long nowMs();
        static int fourccFromString(const std::string& value);

    private:
        int worker_id_ = 0;
        AppConfig config_;
        RedisTaskQueue redis_queue_;
        std::unique_ptr<IModelRunner> runner_;

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
