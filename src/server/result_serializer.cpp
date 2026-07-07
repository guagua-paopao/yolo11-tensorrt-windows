#include "server/result_serializer.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "config.h"

namespace yolo11_server {

    namespace {

        std::string toLowerString(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
                });
            return text;
        }

        int clampInt(int value, int low, int high) {
            return std::max(low, std::min(value, high));
        }

        cv::Rect getObbAxisAlignedRect(const cv::Mat& image, const Detection& detection) {
            if (image.empty()) {
                return {};
            }

            const float cx = detection.bbox[0];
            const float cy = detection.bbox[1];
            const float w = detection.bbox[2];
            const float h = detection.bbox[3];

            float l = cx - w / 2.0f;
            float r = cx + w / 2.0f;
            float t = cy - h / 2.0f;
            float b = cy + h / 2.0f;

            const float r_w = kObbInputW / (image.cols * 1.0f);
            const float r_h = kObbInputH / (image.rows * 1.0f);

            if (r_h > r_w) {
                t = t - (kObbInputH - r_w * image.rows) / 2.0f;
                b = b - (kObbInputH - r_w * image.rows) / 2.0f;
                l = l / r_w;
                r = r / r_w;
                t = t / r_w;
                b = b / r_w;
            }
            else {
                l = l - (kObbInputW - r_h * image.cols) / 2.0f;
                r = r - (kObbInputW - r_h * image.cols) / 2.0f;
                l = l / r_h;
                r = r / r_h;
                t = t / r_h;
                b = b / r_h;
            }

            l = std::max(0.0f, l);
            t = std::max(0.0f, t);

            const int x = clampInt(static_cast<int>(std::round(l)), 0, std::max(0, image.cols - 1));
            const int y = clampInt(static_cast<int>(std::round(t)), 0, std::max(0, image.rows - 1));
            const int width = std::max(0, std::min(static_cast<int>(std::round(r - l)), image.cols - x));
            const int height = std::max(0, std::min(static_cast<int>(std::round(b - t)), image.rows - y));

            return cv::Rect(x, y, width, height);
        }

        struct ObbGeometry {
            double cx = 0.0;
            double cy = 0.0;
            double w = 0.0;
            double h = 0.0;
            double angle_degrees = 0.0;
            std::vector<cv::Point> points;
            cv::Rect bbox_axis_aligned;
        };

        ObbGeometry computeObbGeometry(const cv::Mat& image, const Detection& detection) {
            ObbGeometry geometry;
            if (image.empty()) {
                return geometry;
            }

            cv::Rect rect = getObbAxisAlignedRect(image, detection);
            geometry.cx = rect.x + rect.width / 2.0;
            geometry.cy = rect.y + rect.height / 2.0;
            geometry.w = rect.width;
            geometry.h = rect.height;

            double angle = static_cast<double>(detection.angle) * 180.0 / CV_PI;
            double width = geometry.w;
            double height = geometry.h;

            // Keep the same width/height and angle convention as draw_bbox_obb in postprocess.cpp.
            if (height >= width) {
                std::swap(width, height);
                angle = std::fmod(angle + 90.0, 180.0);
            }
            if (angle < 0.0) {
                angle += 360.0;
            }
            if (angle > 180.0) {
                angle -= 180.0;
            }
            angle = std::fmod(angle, 180.0);
            if (angle < 0.0) {
                angle += 180.0;
            }

            geometry.w = width;
            geometry.h = height;
            geometry.angle_degrees = angle;

            const double rad = angle * CV_PI / 180.0;
            const double cos_value = std::cos(rad);
            const double sin_value = std::sin(rad);
            const double vec1x = width / 2.0 * cos_value;
            const double vec1y = width / 2.0 * sin_value;
            const double vec2x = -height / 2.0 * sin_value;
            const double vec2y = height / 2.0 * cos_value;

            geometry.points.resize(4);
            geometry.points[0] = cv::Point(static_cast<int>(std::round(geometry.cx + vec1x + vec2x)),
                static_cast<int>(std::round(geometry.cy + vec1y + vec2y)));
            geometry.points[1] = cv::Point(static_cast<int>(std::round(geometry.cx + vec1x - vec2x)),
                static_cast<int>(std::round(geometry.cy + vec1y - vec2y)));
            geometry.points[2] = cv::Point(static_cast<int>(std::round(geometry.cx - vec1x - vec2x)),
                static_cast<int>(std::round(geometry.cy - vec1y - vec2y)));
            geometry.points[3] = cv::Point(static_cast<int>(std::round(geometry.cx - vec1x + vec2x)),
                static_cast<int>(std::round(geometry.cy - vec1y + vec2y)));

            int min_x = image.cols - 1;
            int min_y = image.rows - 1;
            int max_x = 0;
            int max_y = 0;
            for (auto& point : geometry.points) {
                point.x = clampInt(point.x, 0, std::max(0, image.cols - 1));
                point.y = clampInt(point.y, 0, std::max(0, image.rows - 1));
                min_x = std::min(min_x, point.x);
                min_y = std::min(min_y, point.y);
                max_x = std::max(max_x, point.x);
                max_y = std::max(max_y, point.y);
            }

            geometry.bbox_axis_aligned = cv::Rect(min_x, min_y, std::max(0, max_x - min_x), std::max(0, max_y - min_y));
            return geometry;
        }

    }  // namespace

    bool ResultSerializer::rectTouchesImageBoundary(const cv::Rect& rect, const cv::Mat& image) {
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

    nlohmann::json ResultSerializer::detectionToJson(
        const Detection& detection,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
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
        item["class_name"] = label_map.className(class_id);
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

        return item;
    }

    nlohmann::json ResultSerializer::detectionsToJson(
        const std::vector<Detection>& detections,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& detection : detections) {
            arr.push_back(detectionToJson(detection, image, label_map, debug));
        }
        return arr;
    }

    nlohmann::json ResultSerializer::obbDetectionToJson(
        const Detection& detection,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
        const int class_id = static_cast<int>(detection.class_id);
        const ObbGeometry geometry = computeObbGeometry(image, detection);

        nlohmann::json points = nlohmann::json::array();
        for (const auto& point : geometry.points) {
            points.push_back(nlohmann::json::array({ point.x, point.y }));
        }

        nlohmann::json item;
        item["class_id"] = class_id;
        item["class_name"] = label_map.className(class_id);
        item["confidence"] = detection.conf;
        item["obb"] = {
            {"cx", geometry.cx},
            {"cy", geometry.cy},
            {"w", geometry.w},
            {"h", geometry.h},
            {"angle", geometry.angle_degrees},
            {"angle_unit", "degrees"},
            {"points", points}
        };
        item["bbox_axis_aligned"] = {
            {"x1", geometry.bbox_axis_aligned.x},
            {"y1", geometry.bbox_axis_aligned.y},
            {"x2", geometry.bbox_axis_aligned.x + geometry.bbox_axis_aligned.width},
            {"y2", geometry.bbox_axis_aligned.y + geometry.bbox_axis_aligned.height}
        };
        item["clipped"] = rectTouchesImageBoundary(geometry.bbox_axis_aligned, image);

        if (debug) {
            item["raw_model_obb"] = {
                {"cx", detection.bbox[0]},
                {"cy", detection.bbox[1]},
                {"w", detection.bbox[2]},
                {"h", detection.bbox[3]},
                {"angle_radians", detection.angle},
                {"angle_degrees", static_cast<double>(detection.angle) * 180.0 / CV_PI}
            };
        }

        return item;
    }

    nlohmann::json ResultSerializer::obbDetectionsToJson(
        const std::vector<Detection>& detections,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& detection : detections) {
            arr.push_back(obbDetectionToJson(detection, image, label_map, debug));
        }
        return arr;
    }

    nlohmann::json ResultSerializer::detectionsToJsonByModel(
        const std::vector<Detection>& detections,
        const cv::Mat& image,
        const LabelMap& label_map,
        const std::string& model_type,
        bool debug
    ) {
        const std::string lower = toLowerString(model_type);
        if (lower == "obb") {
            return obbDetectionsToJson(detections, image, label_map, debug);
        }
        return detectionsToJson(detections, image, label_map, debug);
    }

}  // namespace yolo11_server
