#pragma once

#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

namespace yolo11 {

    struct ClsConfig {
        std::string engine_path;
        int gpu_id = 0;
        int topk = 5;
    };

    struct ClassificationResult {
        int class_id = -1;
        float confidence = 0.0f;
    };

    class Yolo11ClsDetector {
    public:
        Yolo11ClsDetector();
        ~Yolo11ClsDetector();

        bool init(const ClsConfig& config);
        std::vector<ClassificationResult> infer(const cv::Mat& image);
        cv::Mat draw(const cv::Mat& image, const std::vector<ClassificationResult>& results, const std::vector<std::string>& labels = {});
        void release() noexcept;

    private:
        bool loadEngine(const std::string& engine_path);
        bool prepareBuffers();
        bool preprocess(const cv::Mat& image);
        bool doInference();

    private:
        ClsConfig config_;

        void* runtime_ = nullptr;
        void* engine_ = nullptr;
        void* context_ = nullptr;
        void* stream_ = nullptr;

        float* input_device_ = nullptr;
        float* output_device_ = nullptr;
        float* input_host_ = nullptr;
        float* output_host_ = nullptr;

        bool initialized_ = false;
    };

}  // namespace yolo11
