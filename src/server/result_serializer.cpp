#include "server/result_serializer.h"

namespace yolo11_server {

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

}  // namespace yolo11_server
