#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "postprocess.h"

namespace yolo11_server {

class ResultSerializer {
public:
    static nlohmann::json detectionToJson(const Detection& detection);
    static nlohmann::json detectionsToJson(const std::vector<Detection>& detections);
};

}  // namespace yolo11_server
