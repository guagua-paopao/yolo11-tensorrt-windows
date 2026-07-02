#pragma once

#include <mutex>
#include <string>

#include "server/app_config.h"

struct redisContext;

namespace yolo11_server {

    struct RedisTask {
        std::string stream_id;
        std::string task_id;

        // Production path: image bytes are stored in Redis, so server/worker can be separated.
        std::string input_image_key;
        std::string result_image_key;

        // Backward-compatible fallback for all-in-one/local-file mode.
        std::string input_image_path;

        long long create_time_ms = 0;
    };

    struct RedisTaskStatus {
        bool found = false;
        std::string task_id;
        std::string status;
        std::string error;
        std::string result_json_text;
        std::string input_image_key;
        std::string result_image_key;
        std::string result_image_path;
        std::string result_image_filename;
        std::string worker_id;
        std::string consumer_name;
        long long create_time_ms = 0;
        long long start_time_ms = 0;
        long long finish_time_ms = 0;
        long long queue_wait_ms = 0;
        double infer_ms = 0.0;
        long long total_ms = 0;
    };

    struct RedisStreamStats {
        long long stream_len = -1;
        long long pending = -1;
    };

    class RedisTaskQueue {
    public:
        explicit RedisTaskQueue(const RedisSection& config);
        ~RedisTaskQueue();

        RedisTaskQueue(const RedisTaskQueue&) = delete;
        RedisTaskQueue& operator=(const RedisTaskQueue&) = delete;

        bool connect(std::string& error) const;
        void disconnect() const;
        bool ping(std::string& error) const;

        bool submitTask(const RedisTask& task, std::string& error) const;

        bool markRunning(
            const std::string& task_id,
            long long start_time_ms,
            int worker_id,
            const std::string& consumer_name,
            std::string& error
        ) const;

        bool markDone(
            const std::string& task_id,
            const std::string& result_json_text,
            const std::string& result_image_key,
            const std::string& result_image_path,
            const std::string& result_image_filename,
            long long finish_time_ms,
            long long queue_wait_ms,
            double infer_ms,
            long long total_ms,
            int worker_id,
            const std::string& consumer_name,
            std::string& error
        ) const;

        bool markFailed(
            const std::string& task_id,
            const std::string& error_message,
            long long finish_time_ms,
            long long queue_wait_ms,
            long long total_ms,
            int worker_id,
            const std::string& consumer_name,
            std::string& error
        ) const;

        bool getTaskStatus(const std::string& task_id, RedisTaskStatus& status, std::string& error) const;

        bool popTask(RedisTask& task, std::string& error) const;
        bool claimPendingTask(RedisTask& task, std::string& error) const;
        bool ackTask(const std::string& stream_id, std::string& error) const;

        bool getStreamStats(RedisStreamStats& stats, std::string& error) const;

        // Binary value helpers used for image bytes.
        bool setBinaryValue(const std::string& key, const std::string& value, std::string& error) const;
        bool getBinaryValue(const std::string& key, std::string& value, std::string& error) const;

        std::string inputImageKey(const std::string& task_id) const;
        std::string resultImageKey(const std::string& task_id) const;

    private:
        std::string statusKey(const std::string& task_id) const;
        std::string resultKey(const std::string& task_id) const;
        std::string metaKey(const std::string& task_id) const;

    private:
        RedisSection config_;

        // One hiredis connection per RedisTaskQueue instance.
        // Protected because HttpController can be called by multiple Crow threads.
        mutable std::mutex context_mutex_;
        mutable redisContext* context_ = nullptr;
    };

}  // namespace yolo11_server
