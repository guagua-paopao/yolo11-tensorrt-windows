#include "server/http_controller.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include "server/image_codec.h"
#include "postprocess.h"

namespace yolo11_server {

    namespace {

        // COCO 80 类类别名。
        // 如果你后面换成自己的训练模型，比如焊缝缺陷、工业零件检测，
        // 这里需要改成你自己的类别顺序。
        const std::vector<std::string>& cocoClassNames() {
            static const std::vector<std::string> names = {
                "person",
                "bicycle",
                "car",
                "motorcycle",
                "airplane",
                "bus",
                "train",
                "truck",
                "boat",
                "traffic light",
                "fire hydrant",
                "stop sign",
                "parking meter",
                "bench",
                "bird",
                "cat",
                "dog",
                "horse",
                "sheep",
                "cow",
                "elephant",
                "bear",
                "zebra",
                "giraffe",
                "backpack",
                "umbrella",
                "handbag",
                "tie",
                "suitcase",
                "frisbee",
                "skis",
                "snowboard",
                "sports ball",
                "kite",
                "baseball bat",
                "baseball glove",
                "skateboard",
                "surfboard",
                "tennis racket",
                "bottle",
                "wine glass",
                "cup",
                "fork",
                "knife",
                "spoon",
                "bowl",
                "banana",
                "apple",
                "sandwich",
                "orange",
                "broccoli",
                "carrot",
                "hot dog",
                "pizza",
                "donut",
                "cake",
                "chair",
                "couch",
                "potted plant",
                "bed",
                "dining table",
                "toilet",
                "tv",
                "laptop",
                "mouse",
                "remote",
                "keyboard",
                "cell phone",
                "microwave",
                "oven",
                "toaster",
                "sink",
                "refrigerator",
                "book",
                "clock",
                "vase",
                "scissors",
                "teddy bear",
                "hair drier",
                "toothbrush"
            };

            return names;
        }

        std::string classNameFromId(int class_id) {
            const auto& names = cocoClassNames();

            if (class_id >= 0 && class_id < static_cast<int>(names.size())) {
                return names[class_id];
            }

            std::ostringstream oss;
            oss << "class_" << class_id;
            return oss.str();
        }

        std::string toLowerString(std::string s) {
            std::transform(
                s.begin(),
                s.end(),
                s.begin(),
                [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                }
            );
            return s;
        }

        crow::response makeJsonResponse(int code, const nlohmann::json& body) {
            crow::response response(code, body.dump(4));
            response.set_header("Content-Type", "application/json; charset=utf-8");
            return response;
        }

        bool isTrueString(const std::string& value) {
            return value == "1" ||
                value == "true" ||
                value == "True" ||
                value == "TRUE" ||
                value == "yes" ||
                value == "on";
        }

        // 判断 cv::Rect 是否贴到了图像边界。
        // 注意：这不一定代表错误，很多目标本来就在图像边缘。
        // 这个字段主要用于调试。
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

        // 将 Detection 结果序列化为原图像素坐标。
        // 关键点：这里不再假设 detection.bbox 是 xywh。
        // 当前项目的后处理和画框函数使用 get_rect(img, bbox)
        // 将模型/letterbox 坐标还原到原图坐标。
        // 为了让 JSON 坐标和结果图画框完全一致，这里直接复用 get_rect()。
        nlohmann::json detectionsToJsonForImage(
            const std::vector<Detection>& detections,
            const cv::Mat& image,
            bool debug
        ) {
            nlohmann::json arr = nlohmann::json::array();

            for (const auto& detection : detections) {
                // get_rect 的参数是 cv::Mat& 和 float bbox[4]，所以这里做两个临时拷贝。
                // get_rect 当前不会修改 bbox，但函数签名不是 const。
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

                // 为了兼容你之前的字段名，先继续叫 clipped。
                // 真实含义：最终还原后的 rect 是否碰到图像边界。
                item["clipped"] = rectTouchesImageBoundary(rect, image);

                // debug=true 或 debug=1 时才返回模型原始坐标。
                // 正式接口默认不返回，避免前端混淆两套坐标。
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

    }  // namespace

    HttpController::HttpController(const AppConfig& config, yolo11::Yolo11Detector& detector)
        : config_(config), detector_(detector) {
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

        // 访问检测结果图：
        // http://127.0.0.1:8080/api/v1/image/result_xxx.jpg
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
        body["phase"] = "phase1_sync_http";
        body["model_type"] = config_.model.type;
        body["engine_path"] = config_.model.engine_path;
        body["gpu_id"] = config_.model.gpu_id;
        body["use_gpu_postprocess"] = config_.model.use_gpu_postprocess;

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
            std::lock_guard<std::mutex> lock(detector_mutex_);

            detections = detector_.infer(image);

            if (draw || config_.output.save_result_image) {
                result_image = detector_.draw(image, detections);
            }
        }

        auto t1 = std::chrono::steady_clock::now();

        const double infer_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();

        nlohmann::json body;
        body["success"] = true;
        body["request_id"] = request_id;
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

        // 使用 get_rect() 将模型输出 bbox 还原为原图坐标，
        // 保证 JSON 坐标和结果图中的画框一致。
        body["detections"] = detectionsToJsonForImage(
            detections,
            image,
            debug
        );

        if (config_.output.save_result_image && !result_image.empty()) {
            try {
                std::filesystem::create_directories(config_.output.output_dir);

                const std::string output_filename = makeResultImageFilename(request_id);
                const std::string output_path = makeResultImagePath(output_filename);

                bool ok = cv::imwrite(output_path, result_image);

                if (ok) {
                    const std::string relative_url =
                        "/api/v1/image/" + output_filename;

                    body["saved_result_image"] = output_path;
                    body["result_image_filename"] = output_filename;
                    body["result_image_url"] = relative_url;

                    const std::string host = request.get_header_value("Host");
                    if (!host.empty()) {
                        body["result_image_url_full"] =
                            "http://" + host + relative_url;
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

    crow::response HttpController::handleGetResultImage(const std::string& filename) const {
        if (!isSafeImageFilename(filename)) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = "invalid image filename";
            return makeJsonResponse(400, body);
        }

        const std::string image_path = makeResultImagePath(filename);

        std::ifstream file(image_path, std::ios::binary);

        if (!file.is_open()) {
            nlohmann::json body;
            body["success"] = false;
            body["error"] = "image not found";
            body["filename"] = filename;
            return makeJsonResponse(404, body);
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();

        crow::response response;
        response.code = 200;
        response.body = buffer.str();
        response.set_header("Content-Type", guessImageContentType(filename));
        response.set_header("Cache-Control", "no-store");

        return response;
    }

    std::string HttpController::extractImageBytes(
        const crow::request& request,
        std::string& error_message
    ) const {
        const std::string content_type = request.get_header_value("Content-Type");

        // 1) multipart/form-data: file=@xxx.jpg
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
                error_message =
                    std::string("failed to parse multipart/form-data: ") + e.what();
                return {};
            }
        }

        // 2) raw body: Content-Type: image/jpeg or image/png
        if (content_type.find("image/jpeg") != std::string::npos ||
            content_type.find("image/jpg") != std::string::npos ||
            content_type.find("image/png") != std::string::npos ||
            content_type.find("application/octet-stream") != std::string::npos) {
            return request.body;
        }

        // 3) fallback: still try to decode body if user did not set Content-Type correctly.
        if (!request.body.empty()) {
            return request.body;
        }

        error_message =
            "request body is empty. Use multipart field name 'file' or raw jpg/png bytes.";
        return {};
    }

    bool HttpController::isTrueParam(const char* value) const {
        if (value == nullptr) {
            return false;
        }

        return isTrueString(std::string(value));
    }

    std::string HttpController::makeResultImageFilename(
        unsigned long long request_id
    ) const {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

        std::ostringstream oss;
        oss << "result_" << ms << "_" << request_id << ".jpg";

        return oss.str();
    }

    std::string HttpController::makeResultImagePath(
        const std::string& filename
    ) const {
        std::filesystem::path output_dir(config_.output.output_dir);
        std::filesystem::path image_path = output_dir / filename;

        return image_path.string();
    }

    bool HttpController::isSafeImageFilename(
        const std::string& filename
    ) const {
        if (filename.empty()) {
            return false;
        }

        // 禁止路径穿越，比如 ../xxx.jpg
        if (filename.find("..") != std::string::npos) {
            return false;
        }

        // 禁止带路径分隔符，只允许访问 output_dir 下的单个文件名。
        if (filename.find('/') != std::string::npos ||
            filename.find('\\') != std::string::npos) {
            return false;
        }

        const std::string lower = toLowerString(filename);

        const bool is_jpg =
            lower.size() >= 4 &&
            lower.substr(lower.size() - 4) == ".jpg";

        const bool is_jpeg =
            lower.size() >= 5 &&
            lower.substr(lower.size() - 5) == ".jpeg";

        const bool is_png =
            lower.size() >= 4 &&
            lower.substr(lower.size() - 4) == ".png";

        return is_jpg || is_jpeg || is_png;
    }

    std::string HttpController::guessImageContentType(
        const std::string& filename
    ) const {
        const std::string lower = toLowerString(filename);

        if (lower.size() >= 4 &&
            lower.substr(lower.size() - 4) == ".png") {
            return "image/png";
        }

        if (lower.size() >= 5 &&
            lower.substr(lower.size() - 5) == ".jpeg") {
            return "image/jpeg";
        }

        return "image/jpeg";
    }

}  // namespace yolo11_server