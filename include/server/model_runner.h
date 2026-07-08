#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "postprocess.h"
#include "server/app_config.h"
#include "yolo11_detector_api.h"
#include "yolo11_obb_api.h"

namespace yolo11_server {

    // Phase 10.5: model runner abstraction.
    // InferenceWorker owns one IModelRunner and no longer needs to know whether
    // the underlying model is detect or OBB.
    class IModelRunner {
    public:
        virtual ~IModelRunner() = default;

        virtual std::string modelType() const = 0;
        virtual bool init(const AppConfig& config, std::string& error) = 0;
        virtual std::vector<Detection> infer(const cv::Mat& image) = 0;
        virtual cv::Mat draw(const cv::Mat& image, const std::vector<Detection>& detections) = 0;
        virtual void release() noexcept = 0;
    };

    class DetectModelRunner final : public IModelRunner {
    public:
        std::string modelType() const override;
        bool init(const AppConfig& config, std::string& error) override;
        std::vector<Detection> infer(const cv::Mat& image) override;
        cv::Mat draw(const cv::Mat& image, const std::vector<Detection>& detections) override;
        void release() noexcept override;

    private:
        std::unique_ptr<yolo11::Yolo11Detector> detector_;
    };

    class ObbModelRunner final : public IModelRunner {
    public:
        std::string modelType() const override;
        bool init(const AppConfig& config, std::string& error) override;
        std::vector<Detection> infer(const cv::Mat& image) override;
        cv::Mat draw(const cv::Mat& image, const std::vector<Detection>& detections) override;
        void release() noexcept override;

    private:
        std::unique_ptr<yolo11::Yolo11ObbDetector> detector_;
    };

    std::unique_ptr<IModelRunner> createModelRunner(const std::string& model_type);

}  // namespace yolo11_server
