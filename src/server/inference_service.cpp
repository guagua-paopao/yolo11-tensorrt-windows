#include "server/inference_service.h"

#include <iostream>
#include <string>

namespace yolo11_server {

    InferenceService::InferenceService(const AppConfig& config)
        : config_(config) {
    }

    InferenceService::~InferenceService() {
        stop();
    }

    bool InferenceService::start() {
        if (running_) {
            return true;
        }

        if (!config_.redis.enabled) {
            std::cout << "InferenceService not started: redis.enabled=false." << std::endl;
            running_ = false;
            return true;
        }

        if (config_.model.type != "detect") {
            std::cerr << "InferenceService currently supports model.type=detect only. Current model.type="
                << config_.model.type << std::endl;
            return false;
        }

        workers_.clear();

        std::cout << "Starting InferenceService: worker_num=" << config_.worker.worker_num
            << ", stream=" << config_.redis.stream_key
            << ", group=" << config_.redis.consumer_group
            << std::endl;

        for (int i = 0; i < config_.worker.worker_num; ++i) {
            const int worker_id = i + 1;
            const std::string consumer_name = config_.worker.consumer_name_prefix + std::to_string(worker_id);

            auto worker = std::make_unique<InferenceWorker>(worker_id, config_, consumer_name);
            if (!worker->start()) {
                std::cerr << "Failed to start InferenceWorker " << worker_id << ". Stop all workers." << std::endl;
                stop();
                return false;
            }

            workers_.push_back(std::move(worker));
        }

        running_ = true;
        std::cout << "InferenceService started. workers=" << workers_.size() << std::endl;
        return true;
    }

    void InferenceService::stop() {
        if (workers_.empty() && !running_) {
            return;
        }

        std::cout << "Stopping InferenceService..." << std::endl;
        for (auto& worker : workers_) {
            if (worker) {
                worker->stop();
            }
        }
        workers_.clear();
        running_ = false;
        std::cout << "InferenceService stopped." << std::endl;
    }

    int InferenceService::workerCount() const {
        return static_cast<int>(workers_.size());
    }

    bool InferenceService::running() const {
        return running_;
    }

}  // namespace yolo11_server
