#pragma once

#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "postprocess.h"

namespace yolo11 {

    struct ObbConfig {
        std::string engine_path;
        int gpu_id = 0;
        bool use_gpu_postprocess = false;
    };

    class Yolo11ObbDetector {
    public:
        Yolo11ObbDetector();
        ~Yolo11ObbDetector();

        bool init(const ObbConfig& config);

        std::vector<Detection> infer(const cv::Mat& image);

        std::vector<std::vector<Detection>> inferBatch(
            const std::vector<cv::Mat>& images
        );

        cv::Mat draw(
            const cv::Mat& image,
            const std::vector<Detection>& detections
        );

        void release();

    private:
        bool loadEngine(const std::string& engine_path);
        bool prepareBuffers();

        void processGpuObbResults(
            std::vector<std::vector<Detection>>& results,
            const std::vector<cv::Mat>& img_batch,
            int valid_batch_size
        );

    private:
        ObbConfig config_;

        void* runtime_ = nullptr;
        void* engine_ = nullptr;
        void* context_ = nullptr;
        void* stream_ = nullptr;

        float* input_device_ = nullptr;
        float* output_device_ = nullptr;
        float* output_host_ = nullptr;

        float* decode_host_ = nullptr;
        float* decode_device_ = nullptr;

        int model_bboxes_ = 0;
        bool initialized_ = false;
    };

}  // namespace yolo11#pragma once
