#include "server/model_runner.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <stdexcept>
#include <string>

namespace yolo11_server {

    namespace {
        std::string toLowerString(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }
    }  // namespace

    std::string DetectModelRunner::modelType() const {
        return "detect";
    }

    bool DetectModelRunner::init(const AppConfig& config, std::string& error) {
        try {
            yolo11::DetectorConfig detector_config;
            detector_config.engine_path = config.model.engine_path;
            detector_config.gpu_id = config.model.gpu_id;
            detector_config.use_gpu_postprocess = config.model.use_gpu_postprocess;

            detector_ = std::make_unique<yolo11::Yolo11Detector>();
            if (!detector_->init(detector_config)) {
                error = "Yolo11Detector::init returned false";
                detector_.reset();
                return false;
            }
            return true;
        }
        catch (const std::exception& e) {
            error = e.what();
            detector_.reset();
            return false;
        }
        catch (...) {
            error = "unknown exception while initializing DetectModelRunner";
            detector_.reset();
            return false;
        }
    }

    std::vector<Detection> DetectModelRunner::infer(const cv::Mat& image) {
        if (!detector_) {
            throw std::runtime_error("DetectModelRunner is not initialized");
        }
        return detector_->infer(image);
    }

    cv::Mat DetectModelRunner::draw(const cv::Mat& image, const std::vector<Detection>& detections) {
        if (!detector_) {
            throw std::runtime_error("DetectModelRunner is not initialized");
        }
        return detector_->draw(image, detections);
    }

    void DetectModelRunner::release() noexcept {
        try {
            if (detector_) {
                detector_->release();
                detector_.reset();
            }
        }
        catch (...) {
            detector_.reset();
        }
    }

    std::string ObbModelRunner::modelType() const {
        return "obb";
    }

    bool ObbModelRunner::init(const AppConfig& config, std::string& error) {
        try {
            yolo11::ObbConfig detector_config;
            detector_config.engine_path = config.model.engine_path;
            detector_config.gpu_id = config.model.gpu_id;
            detector_config.use_gpu_postprocess = config.model.use_gpu_postprocess;

            detector_ = std::make_unique<yolo11::Yolo11ObbDetector>();
            if (!detector_->init(detector_config)) {
                error = "Yolo11ObbDetector::init returned false";
                detector_.reset();
                return false;
            }
            return true;
        }
        catch (const std::exception& e) {
            error = e.what();
            detector_.reset();
            return false;
        }
        catch (...) {
            error = "unknown exception while initializing ObbModelRunner";
            detector_.reset();
            return false;
        }
    }

    std::vector<Detection> ObbModelRunner::infer(const cv::Mat& image) {
        if (!detector_) {
            throw std::runtime_error("ObbModelRunner is not initialized");
        }
        return detector_->infer(image);
    }

    cv::Mat ObbModelRunner::draw(const cv::Mat& image, const std::vector<Detection>& detections) {
        if (!detector_) {
            throw std::runtime_error("ObbModelRunner is not initialized");
        }
        return detector_->draw(image, detections);
    }

    void ObbModelRunner::release() noexcept {
        try {
            if (detector_) {
                detector_->release();
                detector_.reset();
            }
        }
        catch (...) {
            detector_.reset();
        }
    }

    std::unique_ptr<IModelRunner> createModelRunner(const std::string& model_type) {
        const std::string lower = toLowerString(model_type);
        if (lower == "detect") {
            return std::make_unique<DetectModelRunner>();
        }
        if (lower == "obb") {
            return std::make_unique<ObbModelRunner>();
        }
        return nullptr;
    }

}  // namespace yolo11_server
