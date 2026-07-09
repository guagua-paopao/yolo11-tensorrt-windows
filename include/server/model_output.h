#pragma once

#include <string>
#include <vector>

#include "postprocess.h"

namespace yolo11_server {

    // Phase 17: unified model output container.
    // Detect/OBB fill detections; CLS fills classifications.
    // Pose/Seg can extend this container later without forcing every model
    // into std::vector<Detection>.
    struct ClassificationItem {
        int class_id = -1;
        std::string class_name;
        float confidence = 0.0f;
    };

    struct ModelOutput {
        std::string model_type = "detect";
        std::vector<Detection> detections;
        std::vector<ClassificationItem> classifications;

        bool empty() const {
            return detections.empty() && classifications.empty();
        }
    };

}  // namespace yolo11_server
