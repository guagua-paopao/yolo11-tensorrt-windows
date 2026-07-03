#pragma once

#include <string>
#include <opencv2/opencv.hpp>

namespace yolo11_server {

class ImageCodec {
public:
    static cv::Mat decodeImageBytes(const std::string& image_bytes);
    static std::string encodeJpegBytes(const cv::Mat& image, int jpeg_quality = 90);
};

}  // namespace yolo11_server
