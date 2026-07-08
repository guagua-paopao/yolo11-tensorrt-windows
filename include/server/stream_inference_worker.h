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

    // Phase 14.0: live stream / camera / RTSP worker.
    // It consumes only stream-start commands from Redis Stream, then keeps a single
    // VideoCapture loop running until stop_requested=true or a read/open error happens.
    class StreamInferenceWorker {
    public:
        StreamInferenceWorker(int worker_id, const AppConfig& config, const std::string& consumer_name);
        ~StreamInferenceWorker() noexcept;

        StreamInferenceWorker(const StreamInferenceWorker&) = delete;
        StreamInferenceWorker& operator=(const StreamInferenceWorker&) = delete;

        bool start();
        void stop() noexcept;
        bool running() const;
        std::string consumerName() const;

    private:
        void loop();
        void processStreamTask(const RedisTask& task);
        bool initModelRunner();
        void releaseRunnerNoexcept() noexcept;

        void heartbeatLoop() noexcept;
        void writeHeartbeatNoexcept() noexcept;
        void setWorkerStatus(const std::string& status, const std::string& current_stream_id = std::string());
        void setLastError(const std::string& error_message);

        static long long nowMs();
        static std::string toLower(std::string value);
        static bool isIntegerString(const std::string& value);

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
        std::string current_stream_id_;
        std::string last_error_;
        long long start_time_ms_ = 0;
        std::atomic<long long> processed_count_{ 0 };
        std::atomic<long long> failed_count_{ 0 };
    };

}  // namespace yolo11_server
