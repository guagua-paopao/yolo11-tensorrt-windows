#include "server/inference_worker.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include "postprocess.h"

namespace yolo11_server {

    namespace {

        RedisSection makeWorkerRedisConfig(const RedisSection& base, const std::string& consumer_name) {
            RedisSection config = base;
            config.consumer_name = consumer_name;
            return config;
        }

        const std::vector<std::string>& cocoClassNames() {
            static const std::vector<std::string> names = {
                "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
                "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog",
                "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
                "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
                "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
                "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich",
                "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
                "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote",
                "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator", "book",
                "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
            };
            return names;
        }

        std::string classNameFromId(int class_id) {
            const auto& names = cocoClassNames();
            if (class_id >= 0 && class_id < static_cast<int>(names.size())) {
                return names[class_id];
            }
            return "class_" + std::to_string(class_id);
        }

        bool rectTouchesImageBoundary(const cv::Rect& rect, const cv::Mat& image) {
            if (image.empty()) {
                return true;
            }
            if (rect.x <= 0 || rect.y <= 0) {
                return true;
            }
            if (rect.x + rect.width >= image.cols) {
                return true;
            }
            if (rect.y + rect.height >= image.rows) {
                return true;
            }
            return false;
        }

        nlohmann::json detectionsToJsonForImage(
            const std::vector<Detection>& detections,
            const cv::Mat& image
        ) {
            nlohmann::json arr = nlohmann::json::array();

            for (const auto& detection : detections) {
                cv::Mat image_for_rect = image;
                float bbox_for_rect[4] = {
                    detection.bbox[0],
                    detection.bbox[1],
                    detection.bbox[2],
                    detection.bbox[3]
                };

                cv::Rect rect = ::get_rect(image_for_rect, bbox_for_rect);

                const int x1 = rect.x;
                const int y1 = rect.y;
                const int w = rect.width;
                const int h = rect.height;
                const int x2 = rect.x + rect.width;
                const int y2 = rect.y + rect.height;
                const int class_id = static_cast<int>(detection.class_id);

                nlohmann::json item;
                item["class_id"] = class_id;
                item["class_name"] = classNameFromId(class_id);
                item["confidence"] = detection.conf;
                item["bbox"] = {
                    {"x", x1},
                    {"y", y1},
                    {"w", w},
                    {"h", h},
                    {"x1", x1},
                    {"y1", y1},
                    {"x2", x2},
                    {"y2", y2}
                };
                item["clipped"] = rectTouchesImageBoundary(rect, image);

                arr.push_back(item);
            }

            return arr;
        }

    }  // namespace

    InferenceWorker::InferenceWorker(int worker_id, const AppConfig& config, const std::string& consumer_name)
        : worker_id_(worker_id),
        config_(config),
        redis_queue_(makeWorkerRedisConfig(config.redis, consumer_name)) {
        config_.redis.consumer_name = consumer_name;
    }

    InferenceWorker::~InferenceWorker() {
        stop();
        if (detector_initialized_) {
            detector_.release();
            detector_initialized_ = false;
        }
    }

    bool InferenceWorker::start() {
        if (running_.load()) {
            return true;
        }

        if (!config_.redis.enabled) {
            std::cerr << "InferenceWorker requires redis.enabled=true." << std::endl;
            return false;
        }

        std::string error;
        if (!redis_queue_.connect(error)) {
            std::cerr << "Worker " << worker_id_ << " failed to connect Redis: "
                << error << std::endl;
            return false;
        }

        yolo11::DetectorConfig detector_config;
        detector_config.engine_path = config_.model.engine_path;
        detector_config.gpu_id = config_.model.gpu_id;
        detector_config.use_gpu_postprocess = config_.model.use_gpu_postprocess;

        std::cout << "Worker " << worker_id_
            << " loading TensorRT engine: " << detector_config.engine_path
            << ", consumer=" << config_.redis.consumer_name
            << std::endl;

        // Initialize detector sequentially before starting the worker thread.
        // This avoids races during TensorRT plugin / engine initialization.
        if (!detector_.init(detector_config)) {
            std::cerr << "Worker " << worker_id_ << " failed to initialize detector." << std::endl;
            return false;
        }
        detector_initialized_ = true;

        running_ = true;
        thread_ = std::thread([this]() {
            loop();
            });

        return true;
    }

    void InferenceWorker::stop() {
        if (!running_.load()) {
            return;
        }

        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool InferenceWorker::running() const {
        return running_.load();
    }

    std::string InferenceWorker::consumerName() const {
        return config_.redis.consumer_name;
    }

    void InferenceWorker::loop() {
        std::cout << "InferenceWorker started: id=" << worker_id_
            << ", consumer=" << config_.redis.consumer_name
            << ", backend=redis_stream"
            << std::endl;

        while (running_.load()) {
            RedisTask task;
            std::string error;
            if (!redis_queue_.popTask(task, error)) {
                if (!error.empty()) {
                    std::cerr << "Worker " << worker_id_
                        << " Redis popTask failed: " << error << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                continue;
            }

            processRedisTask(task);
        }

        std::cout << "InferenceWorker stopped: id=" << worker_id_
            << ", consumer=" << config_.redis.consumer_name
            << std::endl;
    }

    void InferenceWorker::processRedisTask(const RedisTask& task) {
        std::string redis_error;
        if (!redis_queue_.markRunning(task.task_id, nowMs(), redis_error)) {
            std::cerr << "Worker " << worker_id_
                << " Redis markRunning failed: task_id=" << task.task_id
                << ", error=" << redis_error << std::endl;
        }

        try {
            cv::Mat image = cv::imread(task.input_image_path, cv::IMREAD_COLOR);
            if (image.empty()) {
                throw std::runtime_error("worker failed to read input image: " + task.input_image_path);
            }

            std::vector<Detection> detections;
            cv::Mat result_image;
            auto t0 = std::chrono::steady_clock::now();

            // Phase 4: no global detector mutex is used here.
            // This worker owns its detector/context/stream/buffer.
            detections = detector_.infer(image);
            if (config_.output.save_result_image) {
                result_image = detector_.draw(image, detections);
            }

            auto t1 = std::chrono::steady_clock::now();
            const double infer_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::string result_image_filename;
            std::string result_image_path;
            std::string result_image_url;

            if (config_.output.save_result_image && !result_image.empty()) {
                std::filesystem::create_directories(config_.output.output_dir);
                result_image_filename = makeResultImageFilename(task.task_id);
                result_image_path = makeResultImagePath(result_image_filename);
                std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, config_.output.jpeg_quality };
                if (cv::imwrite(result_image_path, result_image, params)) {
                    result_image_url = "/api/v1/image/" + result_image_filename;
                }
                else {
                    result_image_filename.clear();
                    result_image_path.clear();
                }
            }

            nlohmann::json body;
            body["success"] = true;
            body["task_id"] = task.task_id;
            body["status"] = "done";
            body["queue_backend"] = "redis_stream";
            body["worker_id"] = worker_id_;
            body["consumer_name"] = config_.redis.consumer_name;
            body["model_type"] = config_.model.type;
            body["image"] = {
                {"width", image.cols},
                {"height", image.rows},
                {"channels", image.channels()}
            };
            body["bbox_coordinate_system"] = "original_image_pixels";
            body["bbox_format"] = "xywh_and_xyxy";
            body["num_detections"] = detections.size();
            body["inference_ms"] = infer_ms;
            body["detections"] = detectionsToJsonForImage(detections, image);
            if (!result_image_url.empty()) {
                body["result_image_filename"] = result_image_filename;
                body["result_image_url"] = result_image_url;
            }

            const long long finish_time_ms = nowMs();
            const std::string result_json_text = body.dump(4);

            redis_error.clear();
            if (!redis_queue_.markDone(
                task.task_id,
                result_json_text,
                result_image_path,
                result_image_filename,
                finish_time_ms,
                redis_error)) {
                std::cerr << "Worker " << worker_id_
                    << " Redis markDone failed: task_id=" << task.task_id
                    << ", error=" << redis_error << std::endl;
            }

            redis_error.clear();
            if (!redis_queue_.ackTask(task.stream_id, redis_error)) {
                std::cerr << "Worker " << worker_id_
                    << " Redis ackTask failed: stream_id=" << task.stream_id
                    << ", error=" << redis_error << std::endl;
            }

            std::cout << "Worker " << worker_id_
                << " task done: task_id=" << task.task_id
                << ", detections=" << detections.size()
                << ", infer_ms=" << infer_ms
                << ", consumer=" << config_.redis.consumer_name
                << std::endl;
        }
        catch (const std::exception& e) {
            redis_error.clear();
            if (!redis_queue_.markFailed(task.task_id, e.what(), nowMs(), redis_error)) {
                std::cerr << "Worker " << worker_id_
                    << " Redis markFailed failed: task_id=" << task.task_id
                    << ", error=" << redis_error << std::endl;
            }

            redis_error.clear();
            if (!redis_queue_.ackTask(task.stream_id, redis_error)) {
                std::cerr << "Worker " << worker_id_
                    << " Redis ackTask failed after failed task: stream_id=" << task.stream_id
                    << ", error=" << redis_error << std::endl;
            }

            std::cerr << "Worker " << worker_id_
                << " task failed: task_id=" << task.task_id
                << ", error=" << e.what() << std::endl;
        }
    }

    std::string InferenceWorker::makeResultImageFilename(const std::string& task_id) const {
        return task_id + "_result.jpg";
    }

    std::string InferenceWorker::makeResultImagePath(const std::string& filename) const {
        std::filesystem::path output_dir(config_.output.output_dir);
        return (output_dir / filename).string();
    }

    long long InferenceWorker::nowMs() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

}  // namespace yolo11_server
