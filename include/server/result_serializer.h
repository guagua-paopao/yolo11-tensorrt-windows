#pragma once

#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include "postprocess.h"
#include "server/label_map.h"

namespace yolo11_server {

class ResultSerializer {
public:
    static nlohmann::json detectionToJson(
        const Detection& detection,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug = false
    );

    static nlohmann::json detectionsToJson(
        const std::vector<Detection>& detections,
        const cv::Mat& image,
        const LabelMap& label_map,
        bool debug = false
    );

private:
    static bool rectTouchesImageBoundary(const cv::Rect& rect, const cv::Mat& image);
};

}  // namespace yolo11_server
