#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

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

    struct RedisMemoryStats {
        bool found = false;
        long long used_memory_bytes = 0;
        double used_memory_mb = 0.0;
        long long maxmemory_bytes = 0;
        double maxmemory_mb = 0.0;
        std::string used_memory_human;
        std::string maxmemory_human;
    };


    struct RedisRuntimeMetrics {
        bool found = false;
        long long done_count = 0;
        long long failed_count = 0;
        long long total_count = 0;
        long long recent_done_count = 0;
        int recent_window_seconds = 60;
        double qps_recent = 0.0;
        double avg_queue_wait_ms = 0.0;
        double avg_inference_ms = 0.0;
        double avg_total_ms = 0.0;
        long long last_finish_time_ms = 0;
        std::string last_task_id;
        std::map<std::string, long long> worker_done_count;
        std::map<std::string, long long> worker_failed_count;
    };

    struct WorkerHeartbeatRecord {
        bool found = false;
        bool alive = false;
        std::string consumer_name;
        std::string heartbeat_key;
        std::string pid;
        std::string host;
        int worker_id = 0;
        int gpu_id = 0;
        std::string model_type;
        std::string status;
        std::string current_task_id;
        long long processed_count = 0;
        long long failed_count = 0;
        long long start_time_ms = 0;
        long long last_heartbeat_ms = 0;
        long long last_heartbeat_age_ms = -1;
        std::string last_error;
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
        bool getRedisMemoryStats(RedisMemoryStats& stats, std::string& error) const;
        bool getRuntimeMetrics(RedisRuntimeMetrics& metrics, std::string& error) const;

        // Binary value helpers used for image bytes.
        bool setBinaryValue(const std::string& key, const std::string& value, std::string& error) const;
        bool setBinaryValueWithTtl(const std::string& key, const std::string& value, int ttl_seconds, std::string& error) const;
        bool getBinaryValue(const std::string& key, std::string& value, std::string& error) const;
        bool deleteKey(const std::string& key, std::string& error) const;

        // Worker heartbeat helpers.
        bool writeWorkerHeartbeat(const WorkerHeartbeatRecord& heartbeat, int ttl_seconds, std::string& error) const;
        bool getWorkerHeartbeats(
            const std::string& consumer_name_prefix,
            int expected_worker_num,
            std::vector<WorkerHeartbeatRecord>& workers,
            std::string& error
        ) const;

        std::string inputImageKey(const std::string& task_id) const;
        std::string resultImageKey(const std::string& task_id) const;
        std::string workerHeartbeatKey(const std::string& consumer_name) const;
        std::string metricsGlobalKey() const;
        std::string metricsWorkerDoneKey() const;
        std::string metricsWorkerFailedKey() const;
        std::string metricsRecentDoneKey() const;

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
