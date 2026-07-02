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
            "SETEX %s %d %b",
            key.c_str(),
            config_.ttl_seconds,
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
            config_.ttl_seconds,
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

        if (!expireKey(context_, meta_key, config_.ttl_seconds, error)) {
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
            config_.ttl_seconds,
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

        return expireKey(context_, meta_key, config_.ttl_seconds, error);
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
            config_.ttl_seconds,
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

        if (!expireKey(context_, meta_key, config_.ttl_seconds, error)) {
            return false;
        }

        RedisReplyPtr status_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "SETEX %s %d %s",
            status_key.c_str(),
            config_.ttl_seconds,
            "done"
        );
        if (replyIsError(status_reply.get(), error, context_)) {
            return false;
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

        if (!expireKey(context_, meta_key, config_.ttl_seconds, error)) {
            return false;
        }

        RedisReplyPtr status_reply = commandWithReconnectLocked(
            config_,
            context_,
            error,
            10000,
            "SETEX %s %d %s",
            status_key.c_str(),
            config_.ttl_seconds,
            "failed"
        );
        if (replyIsError(status_reply.get(), error, context_)) {
            return false;
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

    std::string RedisTaskQueue::inputImageKey(const std::string& task_id) const {
        return "yolo:image:" + task_id + ":input";
    }

    std::string RedisTaskQueue::resultImageKey(const std::string& task_id) const {
        return "yolo:image:" + task_id + ":result";
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
