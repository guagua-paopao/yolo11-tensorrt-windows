#include "server/image_codec.h"

#include <vector>

namespace yolo11_server {

cv::Mat ImageCodec::decodeImageBytes(const std::string& image_bytes) {
    if (image_bytes.empty()) {
        return {};
    }

    std::vector<unsigned char> buffer(image_bytes.begin(), image_bytes.end());
    return cv::imdecode(buffer, cv::IMREAD_COLOR);
}

std::string ImageCodec::encodeJpegBytes(const cv::Mat& image, int jpeg_quality) {
    if (image.empty()) {
        return {};
    }

    if (jpeg_quality < 1) {
        jpeg_quality = 1;
    }
    if (jpeg_quality > 100) {
        jpeg_quality = 100;
    }

    std::vector<unsigned char> buffer;
    std::vector<int> params = {
        cv::IMWRITE_JPEG_QUALITY,
        jpeg_quality
    };

    if (!cv::imencode(".jpg", image, buffer, params)) {
        return {};
    }

    return std::string(buffer.begin(), buffer.end());
}

}  // namespace yolo11_server
