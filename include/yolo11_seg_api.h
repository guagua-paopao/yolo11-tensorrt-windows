#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <opencv2/opencv.hpp>

#include "postprocess.h"

namespace yolo11 {

    struct SegConfig {
        std::string engine_path;
        std::string labels_path = "./labels/coco.txt";
        int gpu_id = 0;

        // The original YOLO11 segmentation demo only supports CPU mask postprocess.
        // Keep this field for config compatibility, but init() will force it to false.
        bool use_gpu_postprocess = false;
    };

    struct SegmentationResult {
        Detection detection;

        // Letterboxed model-input mask, size kInputW x kInputH, CV_32FC1, value range 0..1.
        // ResultSerializer maps it back to original image pixels when creating JSON.
        cv::Mat mask;
    };

    class Yolo11SegDetector {
    public:
        Yolo11SegDetector();
        ~Yolo11SegDetector();

        bool init(const SegConfig& config);

        std::vector<SegmentationResult> infer(const cv::Mat& image);
        std::vector<std::vector<SegmentationResult>> inferBatch(const std::vector<cv::Mat>& images);

        cv::Mat draw(const cv::Mat& image, const std::vector<SegmentationResult>& segmentations);

        void release() noexcept;

    private:
        bool loadEngine(const std::string& engine_path);
        bool prepareBuffers();

    private:
        SegConfig config_;
        std::unordered_map<int, std::string> labels_map_;

        void* runtime_ = nullptr;
        void* engine_ = nullptr;
        void* context_ = nullptr;
        void* stream_ = nullptr;

        float* input_device_ = nullptr;
        float* output_device_ = nullptr;
        float* proto_device_ = nullptr;
        float* output_host_ = nullptr;
        float* proto_host_ = nullptr;

        int model_bboxes_ = 0;
        bool initialized_ = false;
    };

}  // namespace yolo11
