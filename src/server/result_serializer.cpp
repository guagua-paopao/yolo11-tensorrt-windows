#include "server/result_serializer.h"

namespace yolo11_server {

nlohmann::json ResultSerializer::detectionToJson(const Detection& detection) {
    const float x = detection.bbox[0];
    const float y = detection.bbox[1];
    const float w = detection.bbox[2];
    const float h = detection.bbox[3];

    nlohmann::json item;
    item["class_id"] = static_cast<int>(detection.class_id);
    item["confidence"] = detection.conf;
    item["bbox"] = {
        {"x", x},
        {"y", y},
        {"w", w},
        {"h", h},
        {"x1", x},
        {"y1", y},
        {"x2", x + w},
        {"y2", y + h}
    };
    return item;
}

nlohmann::json ResultSerializer::detectionsToJson(const std::vector<Detection>& detections) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& det : detections) {
        arr.push_back(detectionToJson(det));
    }
    return arr;
}

}  // namespace yolo11_server
