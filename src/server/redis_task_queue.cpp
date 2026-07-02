#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "server/redis_task_queue.h"

#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <hiredis/hiredis.h>

namespace yolo11_server {

    namespace {

        struct RedisContextDeleter {
            void operator()(redisContext* ctx) const {
                if (ctx != nullptr) {
                    redisFree(ctx);
                }
            }
        };

        struct RedisReplyDeleter {
            void operator()(redisReply* reply) const {
                if (reply != nullptr) {
                    freeReplyObject(reply);
                }
            }
        };

        using RedisContextPtr = std::unique_ptr<redisContext, RedisContextDeleter>;
        using RedisReplyPtr = std::unique_ptr<redisReply, RedisReplyDeleter>;

        std::string replyString(const redisReply* reply) {
            if (reply == nullptr || reply->str == nullptr || reply->len == 0) {
                return {};
            }
            return std::string(reply->str, reply->len);
        }

        std::string contextError(const redisContext* context, const std::string& fallback) {
            if (context != nullptr && context->errstr != nullptr && std::strlen(context->errstr) > 0) {
                return context->errstr;
            }
            return fallback;
        }

        bool replyIsError(const redisReply* reply, std::string& error, const redisContext* context) {
            if (reply == nullptr) {
                error = contextError(context, "empty redis reply");
                return true;
            }
            if (reply->type == REDIS_REPLY_ERROR) {
                error = replyString(reply);
                if (error.empty()) {
                    error = "redis command error";
                }
                return true;
            }
            return false;
        }

        bool containsText(const std::string& text, const std::string& token) {
            return text.find(token) != std::string::npos;
        }

        long long parseLongLong(const std::string& value, long long default_value = 0) {
            if (value.empty()) {
                return default_value;
            }
            try {
                return std::stoll(value);
            }
            catch (...) {
                return default_value;
            }
        }

        double parseDouble(const std::string& value, double default_value = 0.0) {
            if (value.empty()) {
                return default_value;
            }
            try {
                return std::stod(value);
            }
            catch (...) {
                return default_value;
            }
        }

        long long nowMs() {
            auto now = std::chrono::system_clock::now();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        }

        std::map<std::string, std::string> parseInfoText(const std::string& text) {
            std::map<std::string, std::string> values;
            std::istringstream iss(text);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty() || line[0] == '#') {
                    continue;
                }
                const auto pos = line.find(':');
                if (pos == std::string::npos) {
                    continue;
                }
                values[line.substr(0, pos)] = line.substr(pos + 1);
            }
            return values;
        }

        std::map<std::string, std::string> parseHashReply(const redisReply* reply) {
            std::map<std::string, std::string> values;
            if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
                return values;
            }
            for (size_t i = 0; i + 1 < reply->elements; i += 2) {
                const std::string key = replyString(reply->element[i]);
                const std::string value = replyString(reply->element[i + 1]);
                values[key] = value;
            }
            return values;
        }

        bool setCommandTimeout(redisContext* context, int command_timeout_ms, std::string& error) {
            if (context == nullptr) {
                error = "redis context is null";
                return false;
            }

            command_timeout_ms = std::max(command_timeout_ms, 1000);

            struct timeval command_timeout;
            command_timeout.tv_sec = command_timeout_ms / 1000;
            command_timeout.tv_usec = (command_timeout_ms % 1000) * 1000;

            if (redisSetTimeout(context, command_timeout) != REDIS_OK) {
                error = contextError(context, "redisSetTimeout failed");
                return false;
            }
            return true;
        }

        bool connectContextOnce(
            const RedisSection& config,
            RedisContextPtr& context,
            std::string& error,
            int command_timeout_ms
        ) {
            struct timeval connect_timeout;
            connect_timeout.tv_sec = 5;
            connect_timeout.tv_usec = 0;

            redisContext* raw_context = redisConnectWithTimeout(
                config.host.c_str(),
                config.port,
                connect_timeout
            );

            if (raw_context == nullptr) {
                error = "redisConnectWithTimeout returned nullptr";
                return false;
            }

            context.reset(raw_context);

            if (context->err) {
                error = contextError(context.get(), "redis connection error");
                return false;
            }

            if (!setCommandTimeout(context.get(), command_timeout_ms, error)) {
                return false;
            }

            if (!config.password.empty()) {
                RedisReplyPtr auth_reply(static_cast<redisReply*>(
                    redisCommand(context.get(), "AUTH %s", config.password.c_str())
                    ));
                if (replyIsError(auth_reply.get(), error, context.get())) {
                    error = "AUTH failed: " + error;
                    return false;
                }
            }

            RedisReplyPtr select_reply(static_cast<redisReply*>(
                redisCommand(context.get(), "SELECT %d", config.db)
                ));
            if (replyIsError(select_reply.get(), error, context.get())) {
                error = "SELECT failed: " + error;
                return false;
            }

            return true;
        }

        bool connectContext(
            const RedisSection& config,
            RedisContextPtr& context,
            std::string& error,
            int command_timeout_ms = 10000
        ) {
            std::string last_error;

            for (int attempt = 1; attempt <= 3; ++attempt) {
                context.reset();
                error.clear();

                if (connectContextOnce(config, context, error, command_timeout_ms)) {
                    return true;
                }

                last_error = error;
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
            }

            error = last_error.empty() ? "failed to connect redis after retries" : last_error;
            return false;
        }

        bool ensureConsumerGroup(redisContext* context, const RedisSection& config, std::string& error) {
            RedisReplyPtr reply(static_cast<redisReply*>(
                redisCommand(
                    context,
                    "XGROUP CREATE %s %s 0 MKSTREAM",
                    config.stream_key.c_str(),
                    config.consumer_group.c_str()
                )
                ));

            if (reply == nullptr) {
                error = contextError(context, "empty redis reply when creating consumer group");
                return false;
            }

            if (reply->type == REDIS_REPLY_ERROR) {
                const std::string message = replyString(reply.get());
                if (containsText(message, "BUSYGROUP")) {
                    return true;
                }
                error = message;
                return false;
            }

            return true;
        }

        bool expireKey(redisContext* context, const std::string& key, int ttl_seconds, std::string& error) {
            RedisReplyPtr reply(static_cast<redisReply*>(
                redisCommand(context, "EXPIRE %s %d", key.c_str(), ttl_seconds)
                ));
            return !replyIsError(reply.get(), error, context);
        }

        int taskTtlSeconds(const RedisSection& config) {
            return config.task_ttl_seconds > 0 ? config.task_ttl_seconds : config.ttl_seconds;
        }

        bool hashHasAnyField(const std::map<std::string, std::string>& values) {
            return !values.empty();
        }

        bool fillTaskFromStreamEntry(redisReply* entry_reply, RedisTask& task, std::string& error) {
            task = RedisTask{};

            if (entry_reply == nullptr || entry_reply->type != REDIS_REPLY_ARRAY || entry_reply->elements < 2) {
                error = "invalid redis stream entry reply";
                return false;
            }

            task.stream_id = replyString(entry_reply->element[0]);

            redisReply* fields_reply = entry_reply->element[1];
            if (fields_reply == nullptr || fields_reply->type != REDIS_REPLY_ARRAY) {
                error = "invalid redis stream fields reply";
                return false;
            }

            std::map<std::string, std::string> fields;
            for (size_t i = 0; i + 1 < fields_reply->elements; i += 2) {
                fields[replyString(fields_reply->element[i])] = replyString(fields_reply->element[i + 1]);
            }

            task.task_id = fields["task_id"];
            task.input_image_path = fields["input_image_path"];
            task.input_image_key = fields["input_image_key"];
            task.result_image_key = fields["result_image_key"];
            task.create_time_ms = parseLongLong(fields["create_time_ms"]);

            if (task.stream_id.empty() || task.task_id.empty()) {
                error = "invalid redis stream message: missing stream_id/task_id";
                return false;
            }
            if (task.input_image_key.empty() && task.input_image_path.empty()) {
                error = "invalid redis stream message: missing input_image_key/input_image_path";
                return false;
            }

            return true;
        }

        void freeRawContext(redisContext*& context) {
            if (context != nullptr) {
                redisFree(context);
                context = nullptr;
            }
        }

        bool ensureRawContextLocked(
            const RedisSection& config,
            redisContext*& context,
            std::string& error,
            int command_timeout_ms
        ) {
            if (context != nullptr && !context->err) {
                return setCommandTimeout(context, command_timeout_ms, error);
            }

            freeRawContext(context);

            RedisContextPtr new_context;
            if (!connectContext(config, new_context, error, command_timeout_ms)) {
                return false;
            }

            context = new_context.release();
            return true;
        }

        RedisReplyPtr commandWithReconnectLocked(
            const RedisSection& config,
            redisContext*& context,
            std::string& error,
            int command_timeout_ms,
            const char* format,
            ...
        ) {
            std::string last_error;

            for (int attempt = 1; attempt <= 2; ++attempt) {
                error.clear();

                if (!ensureRawContextLocked(config, context, error, command_timeout_ms)) {
                    last_error = error;
                    freeRawContext(context);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));
                    continue;
                }

                va_list args;
                va_start(args, format);
                redisReply* raw_reply = static_cast<redisReply*>(redisvCommand(context, format, args));
                va_end(args);

                if (raw_reply != nullptr) {
                    return RedisReplyPtr(raw_reply);
                }

                last_error = contextError(context, "empty redis reply");
                freeRawContext(context);
                std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));
            }

            error = last_error.empty() ? "redis command failed after reconnect" : last_error;
            return RedisReplyPtr(nullptr);
        }

    }  // namespace

    RedisTaskQueue::RedisTaskQueue(const RedisSection& config)
        : config_(config) {
    }

    RedisTaskQueue::~RedisTaskQueue() {
        disconnect();
    }

    void RedisTaskQueue::disconnect() const {
        std::lock_guard<std::mutex> lock(context_mutex_);
        freeRawContext(context_);
    }

    bool RedisTaskQueue::connect(std::string& error) const {
        std::lock_guard<std::mutex> lock(context_mutex_);

        if (!ensureRawContextLocked(config_, context_, error, 10000)) {
            return false;
        }

        RedisReplyPtr ping_reply = commandWithReconnectLocked(config_, context_, error, 10000, "PING");
        if (replyIsError(ping_reply.get(), error, context_)) {
            error = "PING failed: " + error;
            return false;
        }

        if (!ensureConsumerGroup(context_, config_, error)) {
            error = "XGROUP CREATE failed: " + error;
            return false;
        }

        return true;
    }

    bool RedisTaskQueue::ping(std::string& error) const {
        std::lock_guard<std::mutex> lock(context_mutex_);

        RedisReplyPtr reply = commandWithReconnectLocked(config_, context_, error, 10000, "PING");
        return !replyIsError(reply.get(), error, context_);
    }

    bool RedisTaskQueue::setBinaryValue(const std::string& key, const std::string& value, std::string& error) const {
        return setBinaryValueWithTtl(key, value, config_.ttl_seconds, error);
    }

    bool RedisTaskQueue::setBinaryValueWithTtl(const std::string& key, const std::string& value, int ttl_seconds, std::string& error) const {
        if (key.empty()) {
            error = "empty redis binary key";
            return false;
        }
        if (ttl_seconds <= 0) {
            ttl_seconds = config_.ttl_seconds;
        }

        std::lock_guard<std::mutex> lock(context_mutex_);
        RedisReplyPtr reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "SETEX %s %d %b",
            key.c_str(),
            ttl_seconds,
            value.data(),
            value.size()
        );
        return !replyIsError(reply.get(), error, context_);
    }

    bool RedisTaskQueue::getBinaryValue(const std::string& key, std::string& value, std::string& error) const {
        value.clear();
        if (key.empty()) {
            error = "empty redis binary key";
            return false;
        }

        std::lock_guard<std::mutex> lock(context_mutex_);
        RedisReplyPtr reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "GET %s",
            key.c_str()
        );
        if (replyIsError(reply.get(), error, context_)) {
            return false;
        }
        if (reply->type == REDIS_REPLY_NIL) {
            error = "redis key not found: " + key;
            return false;
        }
        if (reply->type != REDIS_REPLY_STRING) {
            error = "redis key is not a binary/string value: " + key;
            return false;
        }
        value.assign(reply->str, reply->len);
        return true;
    }

    bool RedisTaskQueue::submitTask(const RedisTask& task, std::string& error) const {
        std::lock_guard<std::mutex> lock(context_mutex_);

        const std::string status_key = statusKey(task.task_id);
        const std::string meta_key = metaKey(task.task_id);
        const char* empty = "";

        RedisReplyPtr status_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "SETEX %s %d %s",
            status_key.c_str(),
            taskTtlSeconds(config_),
            "queued"
        );
        if (replyIsError(status_reply.get(), error, context_)) {
            return false;
        }

        RedisReplyPtr meta_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HSET %s task_id %s input_image_path %s input_image_key %s result_image_key %s create_time_ms %lld start_time_ms %lld finish_time_ms %lld queue_wait_ms %lld infer_ms %s total_ms %lld worker_id %s consumer_name %s error %s result_image_path %s result_image_filename %s",
            meta_key.c_str(),
            task.task_id.c_str(),
            task.input_image_path.c_str(),
            task.input_image_key.c_str(),
            task.result_image_key.c_str(),
            task.create_time_ms,
            0LL,
            0LL,
            0LL,
            "0",
            0LL,
            empty,
            empty,
            empty,
            empty,
            empty
        );
        if (replyIsError(meta_reply.get(), error, context_)) {
            return false;
        }

        if (!expireKey(context_, meta_key, taskTtlSeconds(config_), error)) {
            return false;
        }

        RedisReplyPtr stream_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "XADD %s * task_id %s input_image_key %s result_image_key %s input_image_path %s create_time_ms %lld",
            config_.stream_key.c_str(),
            task.task_id.c_str(),
            task.input_image_key.c_str(),
            task.result_image_key.c_str(),
            task.input_image_path.c_str(),
            task.create_time_ms
        );
        if (replyIsError(stream_reply.get(), error, context_)) {
            return false;
        }

        if (config_.stream_max_len > 0) {
            RedisReplyPtr trim_reply = commandWithReconnectLocked(
                config_,
                context_,
                error,
                10000,
                "XTRIM %s MAXLEN ~ %lld",
                config_.stream_key.c_str(),
                config_.stream_max_len
            );
            if (replyIsError(trim_reply.get(), error, context_)) {
                error = "XTRIM failed: " + error;
                return false;
            }
        }

        return true;
    }

    bool RedisTaskQueue::markRunning(
        const std::string& task_id,
        long long start_time_ms,
        int worker_id,
        const std::string& consumer_name,
        std::string& error
    ) const {
        std::lock_guard<std::mutex> lock(context_mutex_);

        const std::string status_key = statusKey(task_id);
        const std::string meta_key = metaKey(task_id);

        RedisReplyPtr status_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "SETEX %s %d %s",
            status_key.c_str(),
            taskTtlSeconds(config_),
            "running"
        );
        if (replyIsError(status_reply.get(), error, context_)) {
            return false;
        }

        RedisReplyPtr meta_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HSET %s start_time_ms %lld worker_id %d consumer_name %s",
            meta_key.c_str(),
            start_time_ms,
            worker_id,
            consumer_name.c_str()
        );
        if (replyIsError(meta_reply.get(), error, context_)) {
            return false;
        }

        return expireKey(context_, meta_key, taskTtlSeconds(config_), error);
    }

    bool RedisTaskQueue::markDone(
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
    ) const {
        std::lock_guard<std::mutex> lock(context_mutex_);

        const std::string status_key = statusKey(task_id);
        const std::string result_key = resultKey(task_id);
        const std::string meta_key = metaKey(task_id);
        const char* empty = "";

        RedisReplyPtr result_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "SETEX %s %d %s",
            result_key.c_str(),
            taskTtlSeconds(config_),
            result_json_text.c_str()
        );
        if (replyIsError(result_reply.get(), error, context_)) {
            return false;
        }

        RedisReplyPtr meta_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HSET %s finish_time_ms %lld result_image_key %s result_image_path %s result_image_filename %s queue_wait_ms %lld infer_ms %.6f total_ms %lld worker_id %d consumer_name %s error %s",
            meta_key.c_str(),
            finish_time_ms,
            result_image_key.c_str(),
            result_image_path.c_str(),
            result_image_filename.c_str(),
            queue_wait_ms,
            infer_ms,
            total_ms,
            worker_id,
            consumer_name.c_str(),
            empty
        );
        if (replyIsError(meta_reply.get(), error, context_)) {
            return false;
        }

        if (!expireKey(context_, meta_key, taskTtlSeconds(config_), error)) {
            return false;
        }

        RedisReplyPtr status_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "SETEX %s %d %s",
            status_key.c_str(),
            taskTtlSeconds(config_),
            "done"
        );
        if (replyIsError(status_reply.get(), error, context_)) {
            return false;
        }

        if (config_.metrics_enabled) {
            std::string metrics_error;
            const std::string global_key = metricsGlobalKey();
            const std::string worker_done_key = metricsWorkerDoneKey();
            const std::string recent_key = metricsRecentDoneKey();
            const std::string recent_member = std::to_string(finish_time_ms) + ":" + task_id;

            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HINCRBY %s done_count 1", global_key.c_str());
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HINCRBYFLOAT %s total_queue_wait_ms %.6f", global_key.c_str(), static_cast<double>(queue_wait_ms));
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HINCRBYFLOAT %s total_inference_ms %.6f", global_key.c_str(), infer_ms);
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HINCRBYFLOAT %s total_total_ms %.6f", global_key.c_str(), static_cast<double>(total_ms));
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HSET %s last_finish_time_ms %lld last_task_id %s",
                global_key.c_str(), finish_time_ms, task_id.c_str());
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HINCRBY %s %s 1", worker_done_key.c_str(), consumer_name.c_str());
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "ZADD %s %lld %s", recent_key.c_str(), finish_time_ms, recent_member.c_str());
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "EXPIRE %s %d", recent_key.c_str(), config_.metrics_ttl_seconds);
        }

        return true;
    }

    bool RedisTaskQueue::markFailed(
        const std::string& task_id,
        const std::string& error_message,
        long long finish_time_ms,
        long long queue_wait_ms,
        long long total_ms,
        int worker_id,
        const std::string& consumer_name,
        std::string& error
    ) const {
        std::lock_guard<std::mutex> lock(context_mutex_);

        const std::string status_key = statusKey(task_id);
        const std::string meta_key = metaKey(task_id);

        RedisReplyPtr meta_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HSET %s finish_time_ms %lld queue_wait_ms %lld total_ms %lld worker_id %d consumer_name %s error %s",
            meta_key.c_str(),
            finish_time_ms,
            queue_wait_ms,
            total_ms,
            worker_id,
            consumer_name.c_str(),
            error_message.c_str()
        );
        if (replyIsError(meta_reply.get(), error, context_)) {
            return false;
        }

        if (!expireKey(context_, meta_key, taskTtlSeconds(config_), error)) {
            return false;
        }

        RedisReplyPtr status_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "SETEX %s %d %s",
            status_key.c_str(),
            taskTtlSeconds(config_),
            "failed"
        );
        if (replyIsError(status_reply.get(), error, context_)) {
            return false;
        }

        if (config_.metrics_enabled) {
            std::string metrics_error;
            const std::string global_key = metricsGlobalKey();
            const std::string worker_failed_key = metricsWorkerFailedKey();
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HINCRBY %s failed_count 1", global_key.c_str());
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HSET %s last_finish_time_ms %lld last_task_id %s",
                global_key.c_str(), finish_time_ms, task_id.c_str());
            commandWithReconnectLocked(config_, context_, metrics_error, 10000,
                "HINCRBY %s %s 1", worker_failed_key.c_str(), consumer_name.c_str());
        }

        return true;
    }

    bool RedisTaskQueue::getTaskStatus(const std::string& task_id, RedisTaskStatus& status, std::string& error) const {
        status = RedisTaskStatus{};
        status.task_id = task_id;

        std::lock_guard<std::mutex> lock(context_mutex_);

        const std::string status_key = statusKey(task_id);
        const std::string result_key = resultKey(task_id);
        const std::string meta_key = metaKey(task_id);

        RedisReplyPtr status_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "GET %s",
            status_key.c_str()
        );
        if (replyIsError(status_reply.get(), error, context_)) {
            return false;
        }
        if (status_reply->type == REDIS_REPLY_NIL) {
            status.found = false;
            return true;
        }

        status.found = true;
        status.status = replyString(status_reply.get());

        RedisReplyPtr meta_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HGETALL %s",
            meta_key.c_str()
        );
        if (!replyIsError(meta_reply.get(), error, context_)) {
            const auto values = parseHashReply(meta_reply.get());
            auto getValue = [&values](const std::string& key) -> std::string {
                auto it = values.find(key);
                return it == values.end() ? std::string{} : it->second;
                };
            status.error = getValue("error");
            status.input_image_key = getValue("input_image_key");
            status.result_image_key = getValue("result_image_key");
            status.result_image_path = getValue("result_image_path");
            status.result_image_filename = getValue("result_image_filename");
            status.worker_id = getValue("worker_id");
            status.consumer_name = getValue("consumer_name");
            status.create_time_ms = parseLongLong(getValue("create_time_ms"));
            status.start_time_ms = parseLongLong(getValue("start_time_ms"));
            status.finish_time_ms = parseLongLong(getValue("finish_time_ms"));
            status.queue_wait_ms = parseLongLong(getValue("queue_wait_ms"));
            status.infer_ms = parseDouble(getValue("infer_ms"));
            status.total_ms = parseLongLong(getValue("total_ms"));
        }

        if (status.status == "done") {
            RedisReplyPtr result_reply = commandWithReconnectLocked(
                config_,
                context_,
                error,
                10000,
                "GET %s",
                result_key.c_str()
            );
            if (replyIsError(result_reply.get(), error, context_)) {
                return false;
            }
            if (result_reply->type != REDIS_REPLY_NIL) {
                status.result_json_text = replyString(result_reply.get());
            }
        }

        return true;
    }

    bool RedisTaskQueue::popTask(RedisTask& task, std::string& error) const {
        task = RedisTask{};
        error.clear();

        std::lock_guard<std::mutex> lock(context_mutex_);

        const int pop_timeout_ms = std::max(config_.block_ms + 5000, 8000);
        RedisReplyPtr reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            pop_timeout_ms,
            "XREADGROUP GROUP %s %s COUNT 1 BLOCK %d STREAMS %s >",
            config_.consumer_group.c_str(),
            config_.consumer_name.c_str(),
            config_.block_ms,
            config_.stream_key.c_str()
        );

        if (reply == nullptr) {
            error = contextError(context_, "empty redis reply when reading stream");
            return false;
        }

        if (reply->type == REDIS_REPLY_NIL) {
            error.clear();
            return false;
        }

        if (reply->type == REDIS_REPLY_ERROR) {
            error = replyString(reply.get());
            if (containsText(error, "NOGROUP")) {
                std::string group_error;
                if (ensureConsumerGroup(context_, config_, group_error)) {
                    error.clear();
                }
            }
            return false;
        }

        if (reply->type != REDIS_REPLY_ARRAY || reply->elements == 0) {
            error.clear();
            return false;
        }

        redisReply* stream_reply = reply->element[0];
        if (stream_reply == nullptr || stream_reply->type != REDIS_REPLY_ARRAY || stream_reply->elements < 2) {
            error = "invalid redis stream reply";
            return false;
        }

        redisReply* entries_reply = stream_reply->element[1];
        if (entries_reply == nullptr || entries_reply->type != REDIS_REPLY_ARRAY || entries_reply->elements == 0) {
            error.clear();
            return false;
        }

        return fillTaskFromStreamEntry(entries_reply->element[0], task, error);
    }

    bool RedisTaskQueue::claimPendingTask(RedisTask& task, std::string& error) const {
        task = RedisTask{};
        error.clear();

        if (!config_.enable_pending_reclaim) {
            return false;
        }

        std::lock_guard<std::mutex> lock(context_mutex_);

        RedisReplyPtr reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "XAUTOCLAIM %s %s %s %lld 0-0 COUNT 1",
            config_.stream_key.c_str(),
            config_.consumer_group.c_str(),
            config_.consumer_name.c_str(),
            config_.pending_min_idle_ms
        );

        if (reply == nullptr) {
            error = contextError(context_, "empty redis reply when XAUTOCLAIM");
            return false;
        }

        if (reply->type == REDIS_REPLY_ERROR) {
            error = replyString(reply.get());
            if (containsText(error, "NOGROUP")) {
                std::string group_error;
                if (ensureConsumerGroup(context_, config_, group_error)) {
                    error.clear();
                }
            }
            return false;
        }

        if (reply->type != REDIS_REPLY_ARRAY || reply->elements < 2) {
            error = "invalid XAUTOCLAIM reply";
            return false;
        }

        redisReply* entries_reply = reply->element[1];
        if (entries_reply == nullptr || entries_reply->type != REDIS_REPLY_ARRAY || entries_reply->elements == 0) {
            error.clear();
            return false;
        }

        return fillTaskFromStreamEntry(entries_reply->element[0], task, error);
    }

    bool RedisTaskQueue::ackTask(const std::string& stream_id, std::string& error) const {
        if (stream_id.empty()) {
            return true;
        }

        std::lock_guard<std::mutex> lock(context_mutex_);

        RedisReplyPtr reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "XACK %s %s %s",
            config_.stream_key.c_str(),
            config_.consumer_group.c_str(),
            stream_id.c_str()
        );

        return !replyIsError(reply.get(), error, context_);
    }

    bool RedisTaskQueue::getStreamStats(RedisStreamStats& stats, std::string& error) const {
        stats = RedisStreamStats{};

        std::lock_guard<std::mutex> lock(context_mutex_);

        RedisReplyPtr xlen_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "XLEN %s",
            config_.stream_key.c_str()
        );
        if (replyIsError(xlen_reply.get(), error, context_)) {
            error = "XLEN failed: " + error;
            return false;
        }
        if (xlen_reply->type == REDIS_REPLY_INTEGER) {
            stats.stream_len = xlen_reply->integer;
        }

        RedisReplyPtr pending_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "XPENDING %s %s",
            config_.stream_key.c_str(),
            config_.consumer_group.c_str()
        );
        if (replyIsError(pending_reply.get(), error, context_)) {
            error = "XPENDING failed: " + error;
            return false;
        }
        if (pending_reply->type == REDIS_REPLY_ARRAY && pending_reply->elements >= 1) {
            redisReply* total_reply = pending_reply->element[0];
            if (total_reply != nullptr && total_reply->type == REDIS_REPLY_INTEGER) {
                stats.pending = total_reply->integer;
            }
        }

        return true;
    }


    bool RedisTaskQueue::getRuntimeMetrics(RedisRuntimeMetrics& metrics, std::string& error) const {
        metrics = RedisRuntimeMetrics{};
        metrics.recent_window_seconds = config_.metrics_recent_window_seconds;

        std::lock_guard<std::mutex> lock(context_mutex_);

        const std::string global_key = metricsGlobalKey();
        const std::string worker_done_key = metricsWorkerDoneKey();
        const std::string worker_failed_key = metricsWorkerFailedKey();
        const std::string recent_key = metricsRecentDoneKey();

        RedisReplyPtr global_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HGETALL %s",
            global_key.c_str()
        );
        if (replyIsError(global_reply.get(), error, context_)) {
            error = "HGETALL metrics global failed: " + error;
            return false;
        }

        const auto global_values = parseHashReply(global_reply.get());
        metrics.found = hashHasAnyField(global_values);
        auto getGlobal = [&global_values](const std::string& key) -> std::string {
            auto it = global_values.find(key);
            return it == global_values.end() ? std::string{} : it->second;
        };

        metrics.done_count = parseLongLong(getGlobal("done_count"));
        metrics.failed_count = parseLongLong(getGlobal("failed_count"));
        metrics.total_count = metrics.done_count + metrics.failed_count;
        metrics.last_finish_time_ms = parseLongLong(getGlobal("last_finish_time_ms"));
        metrics.last_task_id = getGlobal("last_task_id");

        const double total_queue = parseDouble(getGlobal("total_queue_wait_ms"));
        const double total_infer = parseDouble(getGlobal("total_inference_ms"));
        const double total_total = parseDouble(getGlobal("total_total_ms"));
        if (metrics.done_count > 0) {
            metrics.avg_queue_wait_ms = total_queue / static_cast<double>(metrics.done_count);
            metrics.avg_inference_ms = total_infer / static_cast<double>(metrics.done_count);
            metrics.avg_total_ms = total_total / static_cast<double>(metrics.done_count);
        }

        const long long now_ms = nowMs();
        const long long min_score = now_ms - static_cast<long long>(metrics.recent_window_seconds) * 1000LL;
        RedisReplyPtr trim_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "ZREMRANGEBYSCORE %s 0 %lld",
            recent_key.c_str(),
            min_score
        );
        if (replyIsError(trim_reply.get(), error, context_)) {
            error = "ZREMRANGEBYSCORE metrics failed: " + error;
            return false;
        }

        RedisReplyPtr recent_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "ZCARD %s",
            recent_key.c_str()
        );
        if (replyIsError(recent_reply.get(), error, context_)) {
            error = "ZCARD metrics failed: " + error;
            return false;
        }
        if (recent_reply->type == REDIS_REPLY_INTEGER) {
            metrics.recent_done_count = recent_reply->integer;
        }
        if (metrics.recent_window_seconds > 0) {
            metrics.qps_recent = static_cast<double>(metrics.recent_done_count) /
                static_cast<double>(metrics.recent_window_seconds);
        }

        RedisReplyPtr worker_done_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HGETALL %s",
            worker_done_key.c_str()
        );
        if (replyIsError(worker_done_reply.get(), error, context_)) {
            error = "HGETALL worker done metrics failed: " + error;
            return false;
        }
        const auto worker_done_values = parseHashReply(worker_done_reply.get());
        for (const auto& kv : worker_done_values) {
            metrics.worker_done_count[kv.first] = parseLongLong(kv.second);
        }

        RedisReplyPtr worker_failed_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HGETALL %s",
            worker_failed_key.c_str()
        );
        if (replyIsError(worker_failed_reply.get(), error, context_)) {
            error = "HGETALL worker failed metrics failed: " + error;
            return false;
        }
        const auto worker_failed_values = parseHashReply(worker_failed_reply.get());
        for (const auto& kv : worker_failed_values) {
            metrics.worker_failed_count[kv.first] = parseLongLong(kv.second);
        }

        return true;
    }

    bool RedisTaskQueue::deleteKey(const std::string& key, std::string& error) const {
        if (key.empty()) {
            return true;
        }

        std::lock_guard<std::mutex> lock(context_mutex_);
        RedisReplyPtr reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "DEL %s",
            key.c_str()
        );
        return !replyIsError(reply.get(), error, context_);
    }

    bool RedisTaskQueue::getRedisMemoryStats(RedisMemoryStats& stats, std::string& error) const {
        stats = RedisMemoryStats{};

        std::lock_guard<std::mutex> lock(context_mutex_);
        RedisReplyPtr reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "INFO memory"
        );
        if (replyIsError(reply.get(), error, context_)) {
            error = "INFO memory failed: " + error;
            return false;
        }
        if (reply->type != REDIS_REPLY_STRING) {
            error = "INFO memory returned non-string reply";
            return false;
        }

        const auto values = parseInfoText(replyString(reply.get()));
        auto getValue = [&values](const std::string& key) -> std::string {
            auto it = values.find(key);
            return it == values.end() ? std::string{} : it->second;
        };

        stats.found = true;
        stats.used_memory_bytes = parseLongLong(getValue("used_memory"));
        stats.used_memory_mb = static_cast<double>(stats.used_memory_bytes) / 1024.0 / 1024.0;
        stats.used_memory_human = getValue("used_memory_human");
        stats.maxmemory_bytes = parseLongLong(getValue("maxmemory"));
        stats.maxmemory_mb = static_cast<double>(stats.maxmemory_bytes) / 1024.0 / 1024.0;
        stats.maxmemory_human = getValue("maxmemory_human");
        return true;
    }

    bool RedisTaskQueue::writeWorkerHeartbeat(const WorkerHeartbeatRecord& heartbeat, int ttl_seconds, std::string& error) const {
        if (heartbeat.consumer_name.empty()) {
            error = "empty heartbeat consumer_name";
            return false;
        }
        if (ttl_seconds <= 0) {
            ttl_seconds = 15;
        }

        const std::string key = workerHeartbeatKey(heartbeat.consumer_name);

        std::lock_guard<std::mutex> lock(context_mutex_);
        RedisReplyPtr hset_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "HSET %s consumer_name %s pid %s host %s worker_id %d gpu_id %d model_type %s status %s current_task_id %s processed_count %lld failed_count %lld start_time_ms %lld last_heartbeat_ms %lld last_error %s",
            key.c_str(),
            heartbeat.consumer_name.c_str(),
            heartbeat.pid.c_str(),
            heartbeat.host.c_str(),
            heartbeat.worker_id,
            heartbeat.gpu_id,
            heartbeat.model_type.c_str(),
            heartbeat.status.c_str(),
            heartbeat.current_task_id.c_str(),
            heartbeat.processed_count,
            heartbeat.failed_count,
            heartbeat.start_time_ms,
            heartbeat.last_heartbeat_ms,
            heartbeat.last_error.c_str()
        );
        if (replyIsError(hset_reply.get(), error, context_)) {
            return false;
        }

        return expireKey(context_, key, ttl_seconds, error);
    }

    bool RedisTaskQueue::getWorkerHeartbeats(
        const std::string& consumer_name_prefix,
        int expected_worker_num,
        std::vector<WorkerHeartbeatRecord>& workers,
        std::string& error
    ) const {
        workers.clear();
        if (expected_worker_num <= 0) {
            return true;
        }

        std::lock_guard<std::mutex> lock(context_mutex_);
        const long long now_ms = nowMs();

        for (int i = 1; i <= expected_worker_num; ++i) {
            WorkerHeartbeatRecord record;
            record.consumer_name = consumer_name_prefix + std::to_string(i);
            record.heartbeat_key = workerHeartbeatKey(record.consumer_name);

            RedisReplyPtr reply = commandWithReconnectLocked(
                config_,
                context_,
                error,
                10000,
                "HGETALL %s",
                record.heartbeat_key.c_str()
            );
            if (replyIsError(reply.get(), error, context_)) {
                return false;
            }

            const auto values = parseHashReply(reply.get());
            record.found = hashHasAnyField(values);
            record.alive = record.found;

            if (record.found) {
                auto getValue = [&values](const std::string& key) -> std::string {
                    auto it = values.find(key);
                    return it == values.end() ? std::string{} : it->second;
                };
                const std::string consumer = getValue("consumer_name");
                if (!consumer.empty()) {
                    record.consumer_name = consumer;
                }
                record.pid = getValue("pid");
                record.host = getValue("host");
                record.worker_id = static_cast<int>(parseLongLong(getValue("worker_id")));
                record.gpu_id = static_cast<int>(parseLongLong(getValue("gpu_id")));
                record.model_type = getValue("model_type");
                record.status = getValue("status");
                record.current_task_id = getValue("current_task_id");
                record.processed_count = parseLongLong(getValue("processed_count"));
                record.failed_count = parseLongLong(getValue("failed_count"));
                record.start_time_ms = parseLongLong(getValue("start_time_ms"));
                record.last_heartbeat_ms = parseLongLong(getValue("last_heartbeat_ms"));
                record.last_error = getValue("last_error");
                if (record.last_heartbeat_ms > 0) {
                    record.last_heartbeat_age_ms = std::max(0LL, now_ms - record.last_heartbeat_ms);
                }
            }

            workers.push_back(record);
        }

        return true;
    }

    std::string RedisTaskQueue::inputImageKey(const std::string& task_id) const {
        return "yolo:image:" + task_id + ":input";
    }

    std::string RedisTaskQueue::resultImageKey(const std::string& task_id) const {
        return "yolo:image:" + task_id + ":result";
    }

    std::string RedisTaskQueue::workerHeartbeatKey(const std::string& consumer_name) const {
        return "yolo:worker:" + consumer_name + ":heartbeat";
    }


    std::string RedisTaskQueue::metricsGlobalKey() const {
        return "yolo:metrics:global";
    }

    std::string RedisTaskQueue::metricsWorkerDoneKey() const {
        return "yolo:metrics:worker:done";
    }

    std::string RedisTaskQueue::metricsWorkerFailedKey() const {
        return "yolo:metrics:worker:failed";
    }

    std::string RedisTaskQueue::metricsRecentDoneKey() const {
        return "yolo:metrics:recent:done";
    }

    std::string RedisTaskQueue::statusKey(const std::string& task_id) const {
        return "yolo:task:" + task_id + ":status";
    }

    std::string RedisTaskQueue::resultKey(const std::string& task_id) const {
        return "yolo:task:" + task_id + ":result";
    }

    std::string RedisTaskQueue::metaKey(const std::string& task_id) const {
        return "yolo:task:" + task_id + ":meta";
    }

}  // namespace yolo11_server
