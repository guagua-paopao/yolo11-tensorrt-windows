#include "server/http_controller.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include "server/image_codec.h"
#include "postprocess.h"

namespace yolo11_server {

    namespace {

        std::mutex g_detector_mutex;

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

        std::string toLowerString(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
                });
            return s;
        }

        crow::response makeJsonResponse(int code, const nlohmann::json& body) {
            crow::response response(code, body.dump(4));
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        }

        bool isTrueString(const std::string& value) {
            const std::string v = toLowerString(value);
            return v == "1" || v == "true" || v == "yes" || v == "on";
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
            const cv::Mat& image,
            bool debug
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

                if (debug) {
                    item["raw_model_bbox"] = {
                        {"x1", detection.bbox[0]},
                        {"y1", detection.bbox[1]},
                        {"x2", detection.bbox[2]},
                        {"y2", detection.bbox[3]}
                    };
                }

                arr.push_back(item);
            }

            return arr;
        }

        std::string readWholeFileBinary(const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return {};
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        bool writeBytesToFile(const std::string& path, const std::string& bytes) {
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return false;
            }
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return file.good();
        }

    }  // namespace

    HttpController::HttpController(const AppConfig& config, yolo11::Yolo11Detector& detector)
        : config_(config), detector_(detector) {
        std::filesystem::create_directories(config_.output.input_dir);
        std::filesystem::create_directories(config_.output.output_dir);
        startWorker();
    }

    HttpController::~HttpController() {
        stopWorker();
    }

    void HttpController::registerRoutes(crow::SimpleApp& app) {
        CROW_ROUTE(app, "/api/v1/health")
            .methods(crow::HTTPMethod::GET)
            ([this]() {
            return handleHealth();
                });

        CROW_ROUTE(app, "/api/v1/detect/image")
            .methods(crow::HTTPMethod::POST)
            ([this](const crow::request& request) {
            return handleDetectImage(request);
                });

        CROW_ROUTE(app, "/api/v1/detect/image/async")
            .methods(crow::HTTPMethod::POST)
            ([this](const crow::request& request) {
            return handleDetectImageAsync(request);
                });

        CROW_ROUTE(app, "/api/v1/result/<string>")
            .methods(crow::HTTPMethod::GET)
            ([this](const std::string& task_id) {
            return handleGetAsyncResult(task_id);
                });

        CROW_ROUTE(app, "/api/v1/image/<string>")
            .methods(crow::HTTPMethod::GET)
            ([this](const std::string& filename) {
            return handleGetResultImage(filename);
                });
    }

    crow::response HttpController::handleHealth() const {
        nlohmann::json body;
        body["success"] = true;
        body["status"] = "ok";
        body["service"] = "yolo11_server";
        body["phase"] = "phase1_5_async_local_queue";
        body["model_type"] = config_.model.type;
        body["engine_path"] = config_.model.engine_path;
        body["gpu_id"] = config_.model.gpu_id;
        body["use_gpu_postprocess"] = config_.model.use_gpu_postprocess;
        body["async_worker"] = worker_running_.load() ? "running" : "stopped";

        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            body["task_count"] = tasks_.size();
            body["pending_count"] = pending_task_ids_.size();
        }

        return makeJsonResponse(200, body);
    }

    crow::response HttpController::handleDetectImage(const crow::request& request) {
        const auto request_id = ++request_counter_;

        std::string error_message;
        std::string image_bytes = extractImageBytes(request, error_message);

        if (image_bytes.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = error_message.empty() ? "empty image body" : error_message;
            return makeJsonResponse(400, body);
        }

        cv::Mat image = ImageCodec::decodeImageBytes(image_bytes);
        if (image.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = "failed to decode image. Please upload a valid jpg/png image.";
            return makeJsonResponse(400, body);
        }

        const int image_width = image.cols;
        const int image_height = image.rows;
        const int image_channels = image.channels();
        const bool draw = isTrueParam(request.url_params.get("draw"));
        const bool debug = isTrueParam(request.url_params.get("debug"));

        std::vector<Detection> detections;
        cv::Mat result_image;

        auto t0 = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(g_detector_mutex);
            detections = detector_.infer(image);
            if (draw || config_.output.save_result_image) {
                result_image = detector_.draw(image, detections);
            }
        }
        auto t1 = std::chrono::steady_clock::now();

        const double infer_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        nlohmann::json body;
        body["success"] = true;
        body["request_id"] = request_id;
        body["status"] = "done";
        body["model_type"] = config_.model.type;
        body["image"] = {
            {"width", image_width},
            {"height", image_height},
            {"channels", image_channels}
        };
        body["bbox_coordinate_system"] = "original_image_pixels";
        body["bbox_format"] = "xywh_and_xyxy";
        body["num_detections"] = detections.size();
        body["inference_ms"] = infer_ms;

        if (debug) {
            body["debug"] = true;
            body["debug_note"] = "raw_model_bbox is returned only when debug=true or debug=1.";
        }

        body["detections"] = detectionsToJsonForImage(detections, image, debug);

        if (config_.output.save_result_image && !result_image.empty()) {
            try {
                std::filesystem::create_directories(config_.output.output_dir);
                const std::string output_filename = makeResultImageFilename(request_id);
                const std::string output_path = makeResultImagePath(output_filename);
                std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, config_.output.jpeg_quality };
                bool ok = cv::imwrite(output_path, result_image, params);
                if (ok) {
                    const std::string relative_url = "/api/v1/image/" + output_filename;
                    body["saved_result_image"] = output_path;
                    body["result_image_filename"] = output_filename;
                    body["result_image_url"] = relative_url;
                    const std::string host = request.get_header_value("Host");
                    if (!host.empty()) {
                        body["result_image_url_full"] = "http://" + host + relative_url;
                    }
                }
                else {
                    body["saved_result_image"] = "";
                    body["save_warning"] = "cv::imwrite failed";
                }
            }
            catch (const std::exception& e) {
                body["saved_result_image"] = "";
                body["save_warning"] = e.what();
            }
        }

        return makeJsonResponse(200, body);
    }

    crow::response HttpController::handleDetectImageAsync(const crow::request& request) {
        std::string error_message;
        std::string image_bytes = extractImageBytes(request, error_message);

        if (image_bytes.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = error_message.empty() ? "empty image body" : error_message;
            return makeJsonResponse(400, body);
        }

        cv::Mat image = ImageCodec::decodeImageBytes(image_bytes);
        if (image.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = "failed to decode image. Please upload a valid jpg/png image.";
            return makeJsonResponse(400, body);
        }

        const std::string task_id = makeTaskId();
        const std::string input_path = makeInputImagePath(task_id);
        std::filesystem::create_directories(config_.output.input_dir);

        // Save raw upload bytes to local temp. Redis binary storage can replace this later.
        if (!writeBytesToFile(input_path, image_bytes)) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = "failed to save input image";
            return makeJsonResponse(500, body);
        }

        AsyncTaskRecord task;
        task.task_id = task_id;
        task.status = "queued";
        task.input_image_path = input_path;
        task.create_time_ms = nowMs();

        pushTask(task);

        nlohmann::json body;
        body["success"] = true;
        body["task_id"] = task_id;
        body["status"] = "queued";
        body["result_url"] = "/api/v1/result/" + task_id;

        return makeJsonResponse(202, body);
    }

    crow::response HttpController::handleGetAsyncResult(const std::string& task_id) const {
        AsyncTaskRecord task;

        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            auto it = tasks_.find(task_id);
            if (it == tasks_.end()) {
                nlohmann::json body;
                body["success"] = false;
                body["error"] = "task not found or expired";
                body["task_id"] = task_id;
                return makeJsonResponse(404, body);
            }
            task = it->second;
        }

        if (task.status == "done" && !task.result_json_text.empty()) {
            crow::response response(200, task.result_json_text);
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        }

        nlohmann::json body;
        body["success"] = task.status != "failed";
        body["task_id"] = task.task_id;
        body["status"] = task.status;
        body["create_time_ms"] = task.create_time_ms;
        body["start_time_ms"] = task.start_time_ms;
        body["finish_time_ms"] = task.finish_time_ms;

        if (!task.error.empty()) {
            body["error"] = task.error;
        }
        if (!task.result_image_filename.empty()) {
            body["result_image_filename"] = task.result_image_filename;
            body["result_image_url"] = "/api/v1/image/" + task.result_image_filename;
        }

        return makeJsonResponse(200, body);
    }

    crow::response HttpController::handleGetResultImage(const std::string& filename) const {
        if (!isSafeImageFilename(filename)) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = "invalid image filename";
            return makeJsonResponse(400, body);
        }

        const std::string image_path = makeResultImagePath(filename);
        std::string bytes = readWholeFileBinary(image_path);

        if (bytes.empty()) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = "image not found";
            body["filename"] = filename;
            return makeJsonResponse(404, body);
        }

        crow::response response;
        response.code = 200;
        response.body = bytes;
        response.set_header("Content-Type", guessImageContentType(filename));
        response.set_header("Cache-Control", "no-store");
        return response;
    }

    std::string HttpController::extractImageBytes(
        const crow::request& request,
        std::string& error_message
    ) const {
        const std::string content_type = request.get_header_value("Content-Type");

        if (content_type.find("multipart/form-data") != std::string::npos) {
            try {
                crow::multipart::message multipart_message(request);
                auto part = multipart_message.get_part_by_name("file");
                if (part.body.empty()) {
                    error_message = "multipart field 'file' is empty or not found";
                    return {};
                }
                return part.body;
            }
            catch (const std::exception& e) {
                error_message = std::string("failed to parse multipart/form-data: ") + e.what();
                return {};
            }
        }

        if (content_type.find("image/jpeg") != std::string::npos ||
            content_type.find("image/jpg") != std::string::npos ||
            content_type.find("image/png") != std::string::npos ||
            content_type.find("application/octet-stream") != std::string::npos) {
            return request.body;
        }

        if (!request.body.empty()) {
            return request.body;
        }

        error_message = "request body is empty. Use multipart field name 'file' or raw jpg/png bytes.";
        return {};
    }

    bool HttpController::isTrueParam(const char* value) const {
        if (value == nullptr) {
            return false;
        }
        return isTrueString(std::string(value));
    }

    std::string HttpController::makeTaskId() {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_value{};

#ifdef _WIN32
        localtime_s(&tm_value, &now_time_t);
#else
        localtime_r(&now_time_t, &tm_value);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_value, "%Y%m%d_%H%M%S")
            << "_" << ++task_counter_;
        return oss.str();
    }

    std::string HttpController::makeResultImageFilename(unsigned long long request_id) const {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::ostringstream oss;
        oss << "result_" << ms << "_" << request_id << ".jpg";
        return oss.str();
    }

    std::string HttpController::makeResultImageFilename(const std::string& task_id) const {
        return task_id + "_result.jpg";
    }

    std::string HttpController::makeInputImagePath(const std::string& task_id) const {
        std::filesystem::path input_dir(config_.output.input_dir);
        return (input_dir / (task_id + ".jpg")).string();
    }

    std::string HttpController::makeResultImagePath(const std::string& filename) const {
        std::filesystem::path output_dir(config_.output.output_dir);
        std::filesystem::path image_path = output_dir / filename;
        return image_path.string();
    }

    bool HttpController::isSafeImageFilename(const std::string& filename) const {
        if (filename.empty()) {
            return false;
        }
        if (filename.find("..") != std::string::npos) {
            return false;
        }
        if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
            return false;
        }

        const std::string lower = toLowerString(filename);
        const bool is_jpg = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".jpg";
        const bool is_jpeg = lower.size() >= 5 && lower.substr(lower.size() - 5) == ".jpeg";
        const bool is_png = lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png";
        return is_jpg || is_jpeg || is_png;
    }

    std::string HttpController::guessImageContentType(const std::string& filename) const {
        const std::string lower = toLowerString(filename);
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png") {
            return "image/png";
        }
        return "image/jpeg";
    }

    void HttpController::startWorker() {
        if (worker_running_.load()) {
            return;
        }
        worker_running_ = true;
        worker_thread_ = std::thread([this]() {
            workerLoop();
            });
    }

    void HttpController::stopWorker() {
        if (!worker_running_.load()) {
            return;
        }
        worker_running_ = false;
        task_cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    void HttpController::pushTask(const AsyncTaskRecord& task) {
        {
            std::lock_guard<std::mutex> lock(task_mutex_);
            tasks_[task.task_id] = task;
            pending_task_ids_.push(task.task_id);
        }
        task_cv_.notify_one();
    }

    bool HttpController::popTask(std::string& task_id) {
        std::unique_lock<std::mutex> lock(task_mutex_);
        task_cv_.wait(lock, [this]() {
            return !worker_running_.load() || !pending_task_ids_.empty();
            });

        if (!worker_running_.load() && pending_task_ids_.empty()) {
            return false;
        }

        task_id = pending_task_ids_.front();
        pending_task_ids_.pop();
        return true;
    }

    void HttpController::workerLoop() {
        std::cout << "Async inference worker started." << std::endl;

        while (worker_running_.load()) {
            std::string task_id;
            if (!popTask(task_id)) {
                break;
            }

            AsyncTaskRecord task;
            {
                std::lock_guard<std::mutex> lock(task_mutex_);
                auto it = tasks_.find(task_id);
                if (it == tasks_.end()) {
                    continue;
                }
                it->second.status = "running";
                it->second.start_time_ms = nowMs();
                task = it->second;
            }

            try {
                cv::Mat image = cv::imread(task.input_image_path, cv::IMREAD_COLOR);
                if (image.empty()) {
                    throw std::runtime_error("worker failed to read input image");
                }

                std::vector<Detection> detections;
                cv::Mat result_image;
                auto t0 = std::chrono::steady_clock::now();

                {
                    std::lock_guard<std::mutex> lock(g_detector_mutex);
                    detections = detector_.infer(image);
                    if (config_.output.save_result_image) {
                        result_image = detector_.draw(image, detections);
                    }
                }

                auto t1 = std::chrono::steady_clock::now();
                const double infer_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

                std::string result_image_url;
                if (config_.output.save_result_image && !result_image.empty()) {
                    std::filesystem::create_directories(config_.output.output_dir);
                    task.result_image_filename = makeResultImageFilename(task.task_id);
                    task.result_image_path = makeResultImagePath(task.result_image_filename);
                    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, config_.output.jpeg_quality };
                    if (cv::imwrite(task.result_image_path, result_image, params)) {
                        result_image_url = "/api/v1/image/" + task.result_image_filename;
                    }
                }

                nlohmann::json body;
                body["success"] = true;
                body["task_id"] = task.task_id;
                body["status"] = "done";
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
                body["detections"] = detectionsToJsonForImage(detections, image, false);
                if (!result_image_url.empty()) {
                    body["result_image_filename"] = task.result_image_filename;
                    body["result_image_url"] = result_image_url;
                }

                task.status = "done";
                task.finish_time_ms = nowMs();
                task.result_json_text = body.dump(4);

                {
                    std::lock_guard<std::mutex> lock(task_mutex_);
                    tasks_[task.task_id] = task;
                }

                std::cout << "Task done: " << task.task_id
                    << ", detections=" << detections.size()
                    << ", infer_ms=" << infer_ms
                    << std::endl;
            }
            catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(task_mutex_);
                auto it = tasks_.find(task_id);
                if (it != tasks_.end()) {
                    it->second.status = "failed";
                    it->second.error = e.what();
                    it->second.finish_time_ms = nowMs();
                }
                std::cerr << "Task failed: " << task_id << ", error=" << e.what() << std::endl;
            }
        }

        std::cout << "Async inference worker stopped." << std::endl;
    }

    long long HttpController::nowMs() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

}  // namespace yolo11_server
