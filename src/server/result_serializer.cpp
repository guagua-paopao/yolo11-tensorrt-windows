#include "server/result_serializer.h"

#include <algorithm>
#include <cmath>
#include <cctype>
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


        const std::vector<std::string>& cocoKeypointNames() {
            static const std::vector<std::string> names = {
                "nose", "left_eye", "right_eye", "left_ear", "right_ear",
                "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
                "left_wrist", "right_wrist", "left_hip", "right_hip",
                "left_knee", "right_knee", "left_ankle", "right_ankle"
            };
            return names;
        }

        const std::vector<std::pair<int, int>>& cocoSkeletonPairs() {
            static const std::vector<std::pair<int, int>> pairs = {
                {0, 1}, {0, 2},  {0, 5}, {0, 6},  {1, 2},   {1, 3},   {2, 4},
                {5, 6}, {5, 7},  {5, 11}, {6, 8}, {6, 12}, {7, 9},   {8, 10},
                {11, 12}, {11, 13}, {12, 14}, {13, 15}, {14, 16}
            };
            return pairs;
        }

        struct PosePointOnImage {
            double x = 0.0;
            double y = 0.0;
            double confidence = 0.0;
            bool valid = false;
            bool in_image = false;
        };

        PosePointOnImage mapPosePointToOriginalImage(const cv::Mat& image, const Detection& detection, int point_index) {
            PosePointOnImage mapped;
            if (image.empty() || point_index < 0 || point_index >= kNumberOfPoints) {
                return mapped;
            }

            const int base = point_index * 3;
            const double raw_x = detection.keypoints[base];
            const double raw_y = detection.keypoints[base + 1];
            const double score = detection.keypoints[base + 2];
            mapped.confidence = score;
            if (raw_x < 0.0 || raw_y < 0.0 || score <= 0.0) {
                return mapped;
            }

            const double r_w = kInputW / (image.cols * 1.0);
            const double r_h = kInputH / (image.rows * 1.0);
            double x = raw_x;
            double y = raw_y;
            if (r_h > r_w) {
                x = raw_x / r_w;
                y = (raw_y - (kInputH - r_w * image.rows) / 2.0) / r_w;
            }
            else {
                x = (raw_x - (kInputW - r_h * image.cols) / 2.0) / r_h;
                y = raw_y / r_h;
            }

            mapped.in_image = (x >= 0.0 && y >= 0.0 && x < image.cols && y < image.rows);
            mapped.x = std::max(0.0, std::min(x, static_cast<double>(std::max(0, image.cols - 1))));
            mapped.y = std::max(0.0, std::min(y, static_cast<double>(std::max(0, image.rows - 1))));
            mapped.valid = score > kConfThreshKeypoints && mapped.in_image;
            return mapped;
        }

        nlohmann::json skeletonToJson() {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& pair : cocoSkeletonPairs()) {
                arr.push_back({ {"from", pair.first}, {"to", pair.second} });
            }
            return arr;
        }

        cv::Mat scaleSegMaskToOriginalImage(const cv::Mat& mask, const cv::Mat& image) {
            if (mask.empty() || image.empty()) {
                return {};
            }

            cv::Mat mask_float;
            if (mask.type() != CV_32FC1) {
                mask.convertTo(mask_float, CV_32FC1);
            }
            else {
                mask_float = mask;
            }

            cv::Mat model_mask;
            if (mask_float.cols != kInputW || mask_float.rows != kInputH) {
                cv::resize(mask_float, model_mask, cv::Size(kInputW, kInputH));
            }
            else {
                model_mask = mask_float;
            }

            int x = 0;
            int y = 0;
            int w = kInputW;
            int h = kInputH;
            const float r_w = kInputW / (image.cols * 1.0f);
            const float r_h = kInputH / (image.rows * 1.0f);
            if (r_h > r_w) {
                w = kInputW;
                h = static_cast<int>(r_w * image.rows);
                x = 0;
                y = (kInputH - h) / 2;
            }
            else {
                w = static_cast<int>(r_h * image.cols);
                h = kInputH;
                x = (kInputW - w) / 2;
                y = 0;
            }

            cv::Rect roi(x, y, std::max(1, w), std::max(1, h));
            roi &= cv::Rect(0, 0, model_mask.cols, model_mask.rows);
            if (roi.empty()) {
                return {};
            }

            cv::Mat cropped = model_mask(roi).clone();
            cv::Mat restored;
            cv::resize(cropped, restored, image.size(), 0, 0, cv::INTER_LINEAR);
            return restored;
        }

        nlohmann::json pointArrayToJson(const std::vector<cv::Point>& points) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& p : points) {
                arr.push_back(nlohmann::json::array({ p.x, p.y }));
            }
            return arr;
        }

        nlohmann::json maskMetadataToJson(const cv::Mat& mask_in_original_image) {
            nlohmann::json meta;
            meta["coordinate_system"] = "original_image_pixels";
            meta["threshold"] = 0.5;
            meta["width"] = mask_in_original_image.cols;
            meta["height"] = mask_in_original_image.rows;

            if (mask_in_original_image.empty()) {
                meta["area_pixels"] = 0;
                meta["has_polygon"] = false;
                meta["polygon"] = nlohmann::json::array();
                meta["polygons"] = nlohmann::json::array();
                return meta;
            }

            cv::Mat binary;
            cv::threshold(mask_in_original_image, binary, 0.5, 255.0, cv::THRESH_BINARY);
            binary.convertTo(binary, CV_8UC1);

            const int area = cv::countNonZero(binary);
            meta["area_pixels"] = area;

            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            if (contours.empty()) {
                meta["has_polygon"] = false;
                meta["polygon"] = nlohmann::json::array();
                meta["polygons"] = nlohmann::json::array();
                return meta;
            }

            std::sort(contours.begin(), contours.end(), [](const auto& a, const auto& b) {
                return std::fabs(cv::contourArea(a)) > std::fabs(cv::contourArea(b));
            });

            nlohmann::json polygons = nlohmann::json::array();
            const size_t max_contours = std::min<size_t>(contours.size(), 3);
            for (size_t i = 0; i < max_contours; ++i) {
                const double peri = cv::arcLength(contours[i], true);
                const double epsilon = std::max(2.0, peri * 0.01);
                std::vector<cv::Point> approx;
                cv::approxPolyDP(contours[i], approx, epsilon, true);
                if (approx.size() > 256) {
                    approx.resize(256);
                }
                polygons.push_back(pointArrayToJson(approx));
            }

            cv::Rect bbox = cv::boundingRect(contours[0]);
            meta["bbox"] = {
                {"x1", bbox.x},
                {"y1", bbox.y},
                {"x2", bbox.x + bbox.width},
                {"y2", bbox.y + bbox.height},
                {"w", bbox.width},
                {"h", bbox.height}
            };
            meta["has_polygon"] = true;
            meta["polygon"] = polygons.empty() ? nlohmann::json::array() : polygons[0];
            meta["polygons"] = polygons;
            return meta;
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


    nlohmann::json ResultSerializer::poseDetectionToJson(
        const Detection& detection,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
        nlohmann::json item = detectionToJson(detection, image, label_map, debug);
        item["pose_format"] = "bbox_keypoints_skeleton";
        item["keypoint_coordinate_system"] = "original_image_pixels";
        item["keypoint_format"] = "coco17_xy_conf";

        const auto& names = cocoKeypointNames();
        nlohmann::json keypoints = nlohmann::json::array();
        int valid_count = 0;
        for (int i = 0; i < kNumberOfPoints; ++i) {
            const PosePointOnImage mapped = mapPosePointToOriginalImage(image, detection, i);
            if (mapped.valid) {
                ++valid_count;
            }
            nlohmann::json kp;
            kp["id"] = i;
            kp["name"] = i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : ("keypoint_" + std::to_string(i));
            kp["x"] = mapped.x;
            kp["y"] = mapped.y;
            kp["confidence"] = mapped.confidence;
            kp["visible"] = mapped.valid;
            kp["in_image"] = mapped.in_image;
            keypoints.push_back(kp);
        }

        item["num_keypoints"] = kNumberOfPoints;
        item["valid_keypoints"] = valid_count;
        item["keypoints"] = keypoints;
        item["skeleton"] = skeletonToJson();

        if (debug) {
            nlohmann::json raw = nlohmann::json::array();
            for (int i = 0; i < kNumberOfPoints; ++i) {
                const int base = i * 3;
                raw.push_back({
                    {"id", i},
                    {"x", detection.keypoints[base]},
                    {"y", detection.keypoints[base + 1]},
                    {"confidence", detection.keypoints[base + 2]}
                });
            }
            item["raw_model_keypoints"] = raw;
        }
        return item;
    }

    nlohmann::json ResultSerializer::poseDetectionsToJson(
        const std::vector<Detection>& detections,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& detection : detections) {
            arr.push_back(poseDetectionToJson(detection, image, label_map, debug));
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
        if (lower == "pose") {
            return poseDetectionsToJson(detections, image, label_map, debug);
        }
        return detectionsToJson(detections, image, label_map, debug);
    }


    nlohmann::json ResultSerializer::classificationsToJson(
        const std::vector<ClassificationItem>& classifications,
        const LabelMap& label_map
    ) {
        nlohmann::json arr = nlohmann::json::array();
        int rank = 1;
        for (const auto& item : classifications) {
            nlohmann::json cls;
            cls["rank"] = rank++;
            cls["class_id"] = item.class_id;
            cls["class_name"] = item.class_name.empty() ? label_map.className(item.class_id) : item.class_name;
            cls["confidence"] = item.confidence;
            arr.push_back(cls);
        }
        return arr;
    }

    nlohmann::json ResultSerializer::segmentationToJson(
        const SegmentationItem& segmentation,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
        nlohmann::json item = detectionToJson(segmentation.detection, image, label_map, debug);
        item["segmentation_format"] = "bbox_polygon_mask_metadata";
        item["mask_coordinate_system"] = "original_image_pixels";
        item["mask_format"] = "thresholded_polygon_metadata";

        const cv::Mat original_mask = scaleSegMaskToOriginalImage(segmentation.mask, image);
        item["mask"] = maskMetadataToJson(original_mask);
        const auto& mask_json = item["mask"];
        item["polygon"] = mask_json.contains("polygon") ? mask_json["polygon"] : nlohmann::json::array();
        item["polygons"] = mask_json.contains("polygons") ? mask_json["polygons"] : nlohmann::json::array();

        if (debug) {
            item["raw_model_mask"] = {
                {"width", segmentation.mask.cols},
                {"height", segmentation.mask.rows},
                {"type", segmentation.mask.type()}
            };
        }
        return item;
    }

    nlohmann::json ResultSerializer::segmentationsToJson(
        const std::vector<SegmentationItem>& segmentations,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : segmentations) {
            arr.push_back(segmentationToJson(item, image, label_map, debug));
        }
        return arr;
    }

    nlohmann::json ResultSerializer::outputToJsonByModel(
        const ModelOutput& output,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug
    ) {
        const std::string lower = toLowerString(output.model_type);
        if (lower == "cls") {
            return classificationsToJson(output.classifications, label_map);
        }
        if (lower == "seg") {
            return segmentationsToJson(output.segmentations, image, label_map, debug);
        }
        return detectionsToJsonByModel(output.detections, image, label_map, lower, debug);
    }

}  // namespace yolo11_server
