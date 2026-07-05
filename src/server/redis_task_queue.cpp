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
            if (context != nullptr && context->err != 0 && context->errstr != nullptr && context->errstr[0] != '\0') {
                return context->errstr;
            }
            return fallback;
        }

        bool replyIsError(const redisReply* reply, std::string& error, const redisContext* context = nullptr) {
            if (reply == nullptr) {
                error = contextError(context, "empty redis reply");
                return true;
            }
            if (reply->type == REDIS_REPLY_ERROR) {
                error = replyString(reply);
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

    }  // namespace

    RedisTaskQueue::RedisTaskQueue(const RedisSection& config)
        : config_(config) {
    }

    bool RedisTaskQueue::connect(std::string& error) const {
        RedisContextPtr context;
        if (!connectContext(config_, context, error, 10000)) {
            return false;
        }

        RedisReplyPtr ping_reply(static_cast<redisReply*>(redisCommand(context.get(), "PING")));
        if (replyIsError(ping_reply.get(), error, context.get())) {
            error = "PING failed: " + error;
            return false;
        }

        if (!ensureConsumerGroup(context.get(), config_, error)) {
            error = "XGROUP CREATE failed: " + error;
            return false;
        }

        return true;
    }

    bool RedisTaskQueue::ping(std::string& error) const {
        RedisContextPtr context;
        if (!connectContext(config_, context, error, 10000)) {
            return false;
        }

        RedisReplyPtr reply(static_cast<redisReply*>(redisCommand(context.get(), "PING")));
        return !replyIsError(reply.get(), error, context.get());
    }

    bool RedisTaskQueue::submitTask(const RedisTask& task, std::string& error) const {
        RedisContextPtr context;
        if (!connectContext(config_, context, error, 10000)) {
            return false;
        }

        const std::string status_key = statusKey(task.task_id);
        const std::string meta_key = metaKey(task.task_id);
        const char* empty = "";

        RedisReplyPtr status_reply(static_cast<redisReply*>(
            redisCommand(context.get(), "SETEX %s %d %s", status_key.c_str(), config_.ttl_seconds, "queued")
            ));
        if (replyIsError(status_reply.get(), error, context.get())) {
            return false;
        }

        RedisReplyPtr meta_reply(static_cast<redisReply*>(
            redisCommand(
                context.get(),
                "HSET %s task_id %s input_image_path %s create_time_ms %lld start_time_ms %lld finish_time_ms %lld error %s result_image_path %s result_image_filename %s",
                meta_key.c_str(),
                task.task_id.c_str(),
                task.input_image_path.c_str(),
                task.create_time_ms,
                0LL,
                0LL,
                empty,
                empty,
                empty
            )
            ));
        if (replyIsError(meta_reply.get(), error, context.get())) {
            return false;
        }

        if (!expireKey(context.get(), meta_key, config_.ttl_seconds, error)) {
            return false;
        }

        RedisReplyPtr stream_reply(static_cast<redisReply*>(
            redisCommand(
                context.get(),
                "XADD %s * task_id %s input_image_path %s create_time_ms %lld",
                config_.stream_key.c_str(),
                task.task_id.c_str(),
                task.input_image_path.c_str(),
                task.create_time_ms
            )
            ));
        if (replyIsError(stream_reply.get(), error, context.get())) {
            return false;
        }

        return true;
    }

    bool RedisTaskQueue::markRunning(const std::string& task_id, long long start_time_ms, std::string& error) const {
        RedisContextPtr context;
        if (!connectContext(config_, context, error, 10000)) {
            return false;
        }

        const std::string status_key = statusKey(task_id);
        const std::string meta_key = metaKey(task_id);

        RedisReplyPtr status_reply(static_cast<redisReply*>(
            redisCommand(context.get(), "SETEX %s %d %s", status_key.c_str(), config_.ttl_seconds, "running")
            ));
        if (replyIsError(status_reply.get(), error, context.get())) {
            return false;
        }

        RedisReplyPtr meta_reply(static_cast<redisReply*>(
            redisCommand(context.get(), "HSET %s start_time_ms %lld", meta_key.c_str(), start_time_ms)
            ));
        if (replyIsError(meta_reply.get(), error, context.get())) {
            return false;
        }

        return expireKey(context.get(), meta_key, config_.ttl_seconds, error);
    }

    bool RedisTaskQueue::markDone(
        const std::string& task_id,
        const std::string& result_json_text,
        const std::string& result_image_path,
        const std::string& result_image_filename,
        long long finish_time_ms,
        std::string& error
    ) const {
        RedisContextPtr context;
        if (!connectContext(config_, context, error, 10000)) {
            return false;
        }

        const std::string status_key = statusKey(task_id);
        const std::string result_key = resultKey(task_id);
        const std::string meta_key = metaKey(task_id);
        const char* empty = "";

        RedisReplyPtr result_reply(static_cast<redisReply*>(
            redisCommand(context.get(), "SETEX %s %d %s", result_key.c_str(), config_.ttl_seconds, result_json_text.c_str())
            ));
        if (replyIsError(result_reply.get(), error, context.get())) {
            return false;
        }

        RedisReplyPtr meta_reply(static_cast<redisReply*>(
            redisCommand(
                context.get(),
                "HSET %s finish_time_ms %lld result_image_path %s result_image_filename %s error %s",
                meta_key.c_str(),
                finish_time_ms,
                result_image_path.c_str(),
                result_image_filename.c_str(),
                empty
            )
            ));
        if (replyIsError(meta_reply.get(), error, context.get())) {
            return false;
        }

        if (!expireKey(context.get(), meta_key, config_.ttl_seconds, error)) {
            return false;
        }

        RedisReplyPtr status_reply(static_cast<redisReply*>(
            redisCommand(context.get(), "SETEX %s %d %s", status_key.c_str(), config_.ttl_seconds, "done")
            ));
        if (replyIsError(status_reply.get(), error, context.get())) {
            return false;
        }

        return true;
    }

    bool RedisTaskQueue::markFailed(
        const std::string& task_id,
        const std::string& error_message,
        long long finish_time_ms,
        std::string& error
    ) const {
        RedisContextPtr context;
        if (!connectContext(config_, context, error, 10000)) {
            return false;
        }

        const std::string status_key = statusKey(task_id);
        const std::string meta_key = metaKey(task_id);

        RedisReplyPtr meta_reply(static_cast<redisReply*>(
            redisCommand(
                context.get(),
                "HSET %s finish_time_ms %lld error %s",
                meta_key.c_str(),
                finish_time_ms,
                error_message.c_str()
            )
            ));
        if (replyIsError(meta_reply.get(), error, context.get())) {
            return false;
        }

        if (!expireKey(context.get(), meta_key, config_.ttl_seconds, error)) {
            return false;
        }

        RedisReplyPtr status_reply(static_cast<redisReply*>(
            redisCommand(context.get(), "SETEX %s %d %s", status_key.c_str(), config_.ttl_seconds, "failed")
            ));
        if (replyIsError(status_reply.get(), error, context.get())) {
            return false;
        }

        return true;
    }

    bool RedisTaskQueue::getTaskStatus(const std::string& task_id, RedisTaskStatus& status, std::string& error) const {
        status = RedisTaskStatus{};
        status.task_id = task_id;

        RedisContextPtr context;
        if (!connectContext(config_, context, error, 10000)) {
            return false;
        }

        const std::string status_key = statusKey(task_id);
        const std::string result_key = resultKey(task_id);
        const std::string meta_key = metaKey(task_id);

        RedisReplyPtr status_reply(static_cast<redisReply*>(
            redisCommand(context.get(), "GET %s", status_key.c_str())
            ));
        if (replyIsError(status_reply.get(), error, context.get())) {
            return false;
        }
        if (status_reply->type == REDIS_REPLY_NIL) {
            status.found = false;
            return true;
        }

        status.found = true;
        status.status = replyString(status_reply.get());

        RedisReplyPtr meta_reply(static_cast<redisReply*>(
            redisCommand(context.get(), "HGETALL %s", meta_key.c_str())
            ));
        if (!replyIsError(meta_reply.get(), error, context.get())) {
            const auto values = parseHashReply(meta_reply.get());
            auto getValue = [&values](const std::string& key) -> std::string {
                auto it = values.find(key);
                return it == values.end() ? std::string{} : it->second;
                };
            status.error = getValue("error");
            status.result_image_path = getValue("result_image_path");
            status.result_image_filename = getValue("result_image_filename");
            status.create_time_ms = parseLongLong(getValue("create_time_ms"));
            status.start_time_ms = parseLongLong(getValue("start_time_ms"));
            status.finish_time_ms = parseLongLong(getValue("finish_time_ms"));
        }

        if (status.status == "done") {
            RedisReplyPtr result_reply(static_cast<redisReply*>(
                redisCommand(context.get(), "GET %s", result_key.c_str())
                ));
            if (replyIsError(result_reply.get(), error, context.get())) {
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

        RedisContextPtr context;
        const int pop_timeout_ms = std::max(config_.block_ms + 5000, 8000);
        if (!connectContext(config_, context, error, pop_timeout_ms)) {
            return false;
        }

        RedisReplyPtr reply(static_cast<redisReply*>(
            redisCommand(
                context.get(),
                "XREADGROUP GROUP %s %s COUNT 1 BLOCK %d STREAMS %s >",
                config_.consumer_group.c_str(),
                config_.consumer_name.c_str(),
                config_.block_ms,
                config_.stream_key.c_str()
            )
            ));

        if (reply == nullptr) {
            error = contextError(context.get(), "empty redis reply when reading stream");
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
                if (ensureConsumerGroup(context.get(), config_, group_error)) {
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

        redisReply* entry_reply = entries_reply->element[0];
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
        task.create_time_ms = parseLongLong(fields["create_time_ms"]);

        if (task.stream_id.empty() || task.task_id.empty() || task.input_image_path.empty()) {
            error = "invalid redis stream message: missing stream_id/task_id/input_image_path";
            return false;
        }

        return true;
    }

    bool RedisTaskQueue::ackTask(const std::string& stream_id, std::string& error) const {
        if (stream_id.empty()) {
            return true;
        }

        RedisContextPtr context;
        if (!connectContext(config_, context, error, 10000)) {
            return false;
        }

        RedisReplyPtr reply(static_cast<redisReply*>(
            redisCommand(
                context.get(),
                "XACK %s %s %s",
                config_.stream_key.c_str(),
                config_.consumer_group.c_str(),
                stream_id.c_str()
            )
            ));

        return !replyIsError(reply.get(), error, context.get());
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
