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

    ModelOutput DetectModelRunner::infer(const cv::Mat& image) {
        if (!detector_) {
            throw std::runtime_error("DetectModelRunner is not initialized");
        }
        ModelOutput output;
        output.model_type = "detect";
        output.detections = detector_->infer(image);
        return output;
    }

    cv::Mat DetectModelRunner::draw(const cv::Mat& image, const ModelOutput& output) {
        if (!detector_) {
            throw std::runtime_error("DetectModelRunner is not initialized");
        }
        return detector_->draw(image, output.detections);
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

    ModelOutput ObbModelRunner::infer(const cv::Mat& image) {
        if (!detector_) {
            throw std::runtime_error("ObbModelRunner is not initialized");
        }
        ModelOutput output;
        output.model_type = "obb";
        output.detections = detector_->infer(image);
        return output;
    }

    cv::Mat ObbModelRunner::draw(const cv::Mat& image, const ModelOutput& output) {
        if (!detector_) {
            throw std::runtime_error("ObbModelRunner is not initialized");
        }
        return detector_->draw(image, output.detections);
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

    std::string ClsModelRunner::modelType() const {
        return "cls";
    }

    bool ClsModelRunner::init(const AppConfig& config, std::string& error) {
        try {
            yolo11::ClsConfig detector_config;
            detector_config.engine_path = config.model.engine_path;
            detector_config.gpu_id = config.model.gpu_id;
            detector_config.topk = config.model.cls_topk;

            std::string label_error;
            if (label_map_.loadFromFile(config.model.labels_path, label_error)) {
                labels_for_draw_.clear();
                for (size_t i = 0; i < label_map_.size(); ++i) {
                    labels_for_draw_.push_back(label_map_.className(static_cast<int>(i)));
                }
            }
            else {
                // Labels are strongly recommended, but the runner can still return class_N.
                labels_for_draw_.clear();
            }

            detector_ = std::make_unique<yolo11::Yolo11ClsDetector>();
            if (!detector_->init(detector_config)) {
                error = "Yolo11ClsDetector::init returned false";
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
            error = "unknown exception while initializing ClsModelRunner";
            detector_.reset();
            return false;
        }
    }

    ModelOutput ClsModelRunner::infer(const cv::Mat& image) {
        if (!detector_) {
            throw std::runtime_error("ClsModelRunner is not initialized");
        }

        ModelOutput output;
        output.model_type = "cls";
        const auto cls_results = detector_->infer(image);
        for (const auto& item : cls_results) {
            ClassificationItem out;
            out.class_id = item.class_id;
            out.confidence = item.confidence;
            out.class_name = label_map_.className(item.class_id);
            output.classifications.push_back(out);
        }
        return output;
    }

    cv::Mat ClsModelRunner::draw(const cv::Mat& image, const ModelOutput& output) {
        if (!detector_) {
            throw std::runtime_error("ClsModelRunner is not initialized");
        }
        std::vector<yolo11::ClassificationResult> cls_results;
        for (const auto& item : output.classifications) {
            yolo11::ClassificationResult result;
            result.class_id = item.class_id;
            result.confidence = item.confidence;
            cls_results.push_back(result);
        }
        return detector_->draw(image, cls_results, labels_for_draw_);
    }

    void ClsModelRunner::release() noexcept {
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


    std::string PoseModelRunner::modelType() const {
        return "pose";
    }

    bool PoseModelRunner::init(const AppConfig& config, std::string& error) {
        try {
            yolo11::PoseConfig detector_config;
            detector_config.engine_path = config.model.engine_path;
            detector_config.gpu_id = config.model.gpu_id;
            detector_config.use_gpu_postprocess = config.model.use_gpu_postprocess;

            detector_ = std::make_unique<yolo11::Yolo11PoseDetector>();
            if (!detector_->init(detector_config)) {
                error = "Yolo11PoseDetector::init returned false";
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
            error = "unknown exception while initializing PoseModelRunner";
            detector_.reset();
            return false;
        }
    }

    ModelOutput PoseModelRunner::infer(const cv::Mat& image) {
        if (!detector_) {
            throw std::runtime_error("PoseModelRunner is not initialized");
        }
        ModelOutput output;
        output.model_type = "pose";
        output.detections = detector_->infer(image);
        return output;
    }

    cv::Mat PoseModelRunner::draw(const cv::Mat& image, const ModelOutput& output) {
        if (!detector_) {
            throw std::runtime_error("PoseModelRunner is not initialized");
        }
        return detector_->draw(image, output.detections);
    }

    void PoseModelRunner::release() noexcept {
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
        if (lower == "cls") {
            return std::make_unique<ClsModelRunner>();
        }
        if (lower == "pose") {
            return std::make_unique<PoseModelRunner>();
        }
        return nullptr;
    }

}  // namespace yolo11_server
