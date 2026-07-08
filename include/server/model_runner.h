#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "postprocess.h"
#include "server/app_config.h"
#include "server/label_map.h"
#include "server/model_output.h"
#include "yolo11_detector_api.h"
#include "yolo11_obb_api.h"
#include "yolo11_cls_api.h"
#include "yolo11_pose_api.h"
#include "yolo11_seg_api.h"

namespace yolo11_server {

    // Phase 17: unified model runner abstraction.
    // The runner returns ModelOutput so non-bbox models such as CLS do not need
    // to be forced into std::vector<Detection>.
    class IModelRunner {
    public:
        virtual ~IModelRunner() = default;

        virtual std::string modelType() const = 0;
        virtual bool init(const AppConfig& config, std::string& error) = 0;
        virtual ModelOutput infer(const cv::Mat& image) = 0;
        virtual cv::Mat draw(const cv::Mat& image, const ModelOutput& output) = 0;
        virtual void release() noexcept = 0;
    };

    class DetectModelRunner final : public IModelRunner {
    public:
        std::string modelType() const override;
        bool init(const AppConfig& config, std::string& error) override;
        ModelOutput infer(const cv::Mat& image) override;
        cv::Mat draw(const cv::Mat& image, const ModelOutput& output) override;
        void release() noexcept override;

    private:
        std::unique_ptr<yolo11::Yolo11Detector> detector_;
    };

    class ObbModelRunner final : public IModelRunner {
    public:
        std::string modelType() const override;
        bool init(const AppConfig& config, std::string& error) override;
        ModelOutput infer(const cv::Mat& image) override;
        cv::Mat draw(const cv::Mat& image, const ModelOutput& output) override;
        void release() noexcept override;

    private:
        std::unique_ptr<yolo11::Yolo11ObbDetector> detector_;
    };

    class ClsModelRunner final : public IModelRunner {
    public:
        std::string modelType() const override;
        bool init(const AppConfig& config, std::string& error) override;
        ModelOutput infer(const cv::Mat& image) override;
        cv::Mat draw(const cv::Mat& image, const ModelOutput& output) override;
        void release() noexcept override;

    private:
        std::unique_ptr<yolo11::Yolo11ClsDetector> detector_;
        LabelMap label_map_;
        std::vector<std::string> labels_for_draw_;
    };

    class PoseModelRunner final : public IModelRunner {
    public:
        std::string modelType() const override;
        bool init(const AppConfig& config, std::string& error) override;
        ModelOutput infer(const cv::Mat& image) override;
        cv::Mat draw(const cv::Mat& image, const ModelOutput& output) override;
        void release() noexcept override;

    private:
        std::unique_ptr<yolo11::Yolo11PoseDetector> detector_;
    };


    class SegModelRunner final : public IModelRunner {
    public:
        std::string modelType() const override;
        bool init(const AppConfig& config, std::string& error) override;
        ModelOutput infer(const cv::Mat& image) override;
        cv::Mat draw(const cv::Mat& image, const ModelOutput& output) override;
        void release() noexcept override;

    private:
        std::unique_ptr<yolo11::Yolo11SegDetector> detector_;
    };

    std::unique_ptr<IModelRunner> createModelRunner(const std::string& model_type);

}  // namespace yolo11_server
