#include "yolo11_cls_api.h"

#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <cuda_runtime_api.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

#include "config.h"
#include "cuda_utils.h"
#include "logging.h"

namespace yolo11 {

    namespace {
        static Logger gLogger;

#ifdef _WIN32
        static HMODULE gYoloPluginHandle = nullptr;
#endif

        bool loadTensorRtPlugins() {
#ifdef _WIN32
            if (gYoloPluginHandle == nullptr) {
                gYoloPluginHandle = LoadLibraryA("myplugins.dll");
                if (!gYoloPluginHandle) {
                    std::cerr << "Failed to load myplugins.dll, Windows error code: "
                        << GetLastError() << std::endl;
                    return false;
                }
            }
#endif
            initLibNvInferPlugins(&gLogger, "");
            return true;
        }

        std::vector<float> stableSoftmax(const float* logits, int n) {
            std::vector<float> probs;
            probs.resize(static_cast<size_t>(std::max(0, n)));
            if (logits == nullptr || n <= 0) {
                return probs;
            }

            float max_value = logits[0];
            for (int i = 1; i < n; ++i) {
                max_value = std::max(max_value, logits[i]);
            }

            double sum = 0.0;
            for (int i = 0; i < n; ++i) {
                const float v = std::exp(logits[i] - max_value);
                probs[static_cast<size_t>(i)] = v;
                sum += static_cast<double>(v);
            }

            if (sum <= 0.0) {
                return probs;
            }

            for (auto& p : probs) {
                p = static_cast<float>(static_cast<double>(p) / sum);
            }
            return probs;
        }

        std::vector<int> topKIndices(const std::vector<float>& values, int k) {
            std::vector<int> indices(values.size());
            std::iota(indices.begin(), indices.end(), 0);
            std::sort(indices.begin(), indices.end(), [&values](int a, int b) {
                return values[static_cast<size_t>(a)] > values[static_cast<size_t>(b)];
            });
            if (k < 0) {
                k = 0;
            }
            if (static_cast<size_t>(k) < indices.size()) {
                indices.resize(static_cast<size_t>(k));
            }
            return indices;
        }

        std::string labelForIndex(int index, const std::vector<std::string>& labels) {
            if (index >= 0 && index < static_cast<int>(labels.size())) {
                return labels[static_cast<size_t>(index)];
            }
            return "class_" + std::to_string(index);
        }
    }  // namespace

    Yolo11ClsDetector::Yolo11ClsDetector() = default;

    Yolo11ClsDetector::~Yolo11ClsDetector() {
        release();
    }

    bool Yolo11ClsDetector::init(const ClsConfig& config) {
        if (initialized_) {
            release();
        }

        config_ = config;
        if (config_.topk <= 0) {
            config_.topk = 5;
        }
        if (config_.topk > kClsNumClass) {
            config_.topk = kClsNumClass;
        }

        cudaError_t cuda_status = cudaSetDevice(config_.gpu_id);
        if (cuda_status != cudaSuccess) {
            std::cerr << "Failed to set CUDA device: " << cudaGetErrorString(cuda_status) << std::endl;
            return false;
        }

        if (!loadTensorRtPlugins()) {
            std::cerr << "Failed to initialize TensorRT plugins." << std::endl;
            return false;
        }

        if (!loadEngine(config_.engine_path)) {
            std::cerr << "Failed to load CLS TensorRT engine: " << config_.engine_path << std::endl;
            release();
            return false;
        }

        cudaStream_t stream = nullptr;
        cuda_status = cudaStreamCreate(&stream);
        if (cuda_status != cudaSuccess) {
            std::cerr << "Failed to create CUDA stream: " << cudaGetErrorString(cuda_status) << std::endl;
            release();
            return false;
        }
        stream_ = reinterpret_cast<void*>(stream);

        if (!prepareBuffers()) {
            std::cerr << "Failed to prepare CLS buffers." << std::endl;
            release();
            return false;
        }

        initialized_ = true;
        std::cout << "Yolo11ClsDetector initialized successfully." << std::endl;
        std::cout << "kClsInputW = " << kClsInputW << ", kClsInputH = " << kClsInputH << std::endl;
        std::cout << "kClsNumClass = " << kClsNumClass << ", topk = " << config_.topk << std::endl;
        return true;
    }

    bool Yolo11ClsDetector::loadEngine(const std::string& engine_path) {
        std::ifstream file(engine_path, std::ios::binary);
        if (!file.good()) {
            std::cerr << "Cannot open CLS engine file: " << engine_path << std::endl;
            return false;
        }

        file.seekg(0, std::ifstream::end);
        const size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ifstream::beg);
        if (size == 0) {
            std::cerr << "CLS engine file is empty: " << engine_path << std::endl;
            return false;
        }

        std::vector<char> serialized_engine(size);
        file.read(serialized_engine.data(), static_cast<std::streamsize>(size));
        file.close();

        auto* runtime = nvinfer1::createInferRuntime(gLogger);
        if (!runtime) {
            std::cerr << "createInferRuntime failed for CLS." << std::endl;
            return false;
        }

        auto* engine = runtime->deserializeCudaEngine(serialized_engine.data(), size);
        if (!engine) {
            std::cerr << "deserializeCudaEngine failed for CLS." << std::endl;
            delete runtime;
            return false;
        }

        auto* context = engine->createExecutionContext();
        if (!context) {
            std::cerr << "createExecutionContext failed for CLS." << std::endl;
            delete engine;
            delete runtime;
            return false;
        }

        runtime_ = reinterpret_cast<void*>(runtime);
        engine_ = reinterpret_cast<void*>(engine);
        context_ = reinterpret_cast<void*>(context);
        return true;
    }

    bool Yolo11ClsDetector::prepareBuffers() {
        auto* engine = reinterpret_cast<nvinfer1::ICudaEngine*>(engine_);
        if (!engine) {
            std::cerr << "CLS engine is null in prepareBuffers." << std::endl;
            return false;
        }

        if (engine->getNbIOTensors() != 2) {
            std::cerr << "CLS model should have 1 input and 1 output." << std::endl;
            return false;
        }

        const auto input_mode = engine->getTensorIOMode(kInputTensorName);
        if (input_mode != nvinfer1::TensorIOMode::kINPUT) {
            std::cerr << kInputTensorName << " is not CLS input tensor." << std::endl;
            return false;
        }

        const auto output_mode = engine->getTensorIOMode(kOutputTensorName);
        if (output_mode != nvinfer1::TensorIOMode::kOUTPUT) {
            std::cerr << kOutputTensorName << " is not CLS output tensor." << std::endl;
            return false;
        }

        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&input_device_), kBatchSize * 3 * kClsInputH * kClsInputW * sizeof(float)));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&output_device_), kBatchSize * kClsNumClass * sizeof(float)));

        input_host_ = new float[kBatchSize * 3 * kClsInputH * kClsInputW];
        output_host_ = new float[kBatchSize * kClsNumClass];
        return true;
    }

    bool Yolo11ClsDetector::preprocess(const cv::Mat& image) {
        if (image.empty() || input_host_ == nullptr) {
            return false;
        }

        int h = image.rows;
        int w = image.cols;
        int m = std::min(h, w);
        if (m <= 0) {
            return false;
        }
        int top = (h - m) / 2;
        int left = (w - m) / 2;

        cv::Mat cropped = image(cv::Rect(left, top, m, m)).clone();
        cv::resize(cropped, cropped, cv::Size(kClsInputW, kClsInputH), 0, 0, cv::INTER_LINEAR);
        cv::cvtColor(cropped, cropped, cv::COLOR_BGR2RGB);
        cropped.convertTo(cropped, CV_32F, 1.0 / 255.0);

        std::vector<cv::Mat> channels(3);
        cv::split(cropped, channels);

        for (int c = 0; c < 3; ++c) {
            int i = 0;
            for (int row = 0; row < kClsInputH; ++row) {
                for (int col = 0; col < kClsInputW; ++col) {
                    input_host_[c * kClsInputH * kClsInputW + i] = channels[static_cast<size_t>(c)].at<float>(row, col);
                    ++i;
                }
            }
        }
        return true;
    }

    bool Yolo11ClsDetector::doInference() {
        auto* context = reinterpret_cast<nvinfer1::IExecutionContext*>(context_);
        auto stream = reinterpret_cast<cudaStream_t>(stream_);
        if (!context || !stream || !input_device_ || !output_device_ || !input_host_ || !output_host_) {
            return false;
        }

        CUDA_CHECK(cudaMemcpyAsync(input_device_, input_host_, kBatchSize * 3 * kClsInputH * kClsInputW * sizeof(float), cudaMemcpyHostToDevice, stream));
        context->setInputTensorAddress(kInputTensorName, input_device_);
        context->setOutputTensorAddress(kOutputTensorName, output_device_);
        const bool enqueue_ok = context->enqueueV3(stream);
        if (!enqueue_ok) {
            std::cerr << "CLS TensorRT enqueueV3 failed." << std::endl;
            return false;
        }
        CUDA_CHECK(cudaMemcpyAsync(output_host_, output_device_, kBatchSize * kClsNumClass * sizeof(float), cudaMemcpyDeviceToHost, stream));
        CUDA_CHECK(cudaStreamSynchronize(stream));
        return true;
    }

    std::vector<ClassificationResult> Yolo11ClsDetector::infer(const cv::Mat& image) {
        std::vector<ClassificationResult> results;
        if (!initialized_) {
            std::cerr << "CLS detector is not initialized." << std::endl;
            return results;
        }
        if (!preprocess(image)) {
            std::cerr << "CLS preprocess failed." << std::endl;
            return results;
        }
        if (!doInference()) {
            std::cerr << "CLS inference failed." << std::endl;
            return results;
        }

        const std::vector<float> probs = stableSoftmax(output_host_, kClsNumClass);
        const std::vector<int> top_indices = topKIndices(probs, config_.topk);
        for (int idx : top_indices) {
            ClassificationResult item;
            item.class_id = idx;
            item.confidence = probs[static_cast<size_t>(idx)];
            results.push_back(item);
        }
        return results;
    }

    cv::Mat Yolo11ClsDetector::draw(const cv::Mat& image, const std::vector<ClassificationResult>& results, const std::vector<std::string>& labels) {
        cv::Mat vis = image.clone();
        if (vis.empty()) {
            return vis;
        }

        std::string text = "CLS: no result";
        if (!results.empty()) {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(3);
            oss << "CLS: " << labelForIndex(results[0].class_id, labels) << " " << results[0].confidence;
            text = oss.str();
        }

        int baseline = 0;
        const double font_scale = 0.7;
        const int thickness = 2;
        cv::Size text_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline);
        int box_w = std::min(vis.cols, text_size.width + 20);
        int box_h = std::min(vis.rows, text_size.height + baseline + 20);
        cv::rectangle(vis, cv::Rect(0, 0, box_w, box_h), cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(vis, text, cv::Point(10, std::max(20, text_size.height + 8)), cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(255, 255, 255), thickness);
        return vis;
    }

    void Yolo11ClsDetector::release() noexcept {
        try {
            if (stream_) {
                auto stream = reinterpret_cast<cudaStream_t>(stream_);
                cudaStreamDestroy(stream);
                stream_ = nullptr;
            }
            if (input_device_) {
                cudaFree(input_device_);
                input_device_ = nullptr;
            }
            if (output_device_) {
                cudaFree(output_device_);
                output_device_ = nullptr;
            }

            delete[] input_host_;
            input_host_ = nullptr;
            delete[] output_host_;
            output_host_ = nullptr;

            auto* context = reinterpret_cast<nvinfer1::IExecutionContext*>(context_);
            auto* engine = reinterpret_cast<nvinfer1::ICudaEngine*>(engine_);
            auto* runtime = reinterpret_cast<nvinfer1::IRuntime*>(runtime_);
            delete context;
            delete engine;
            delete runtime;
            context_ = nullptr;
            engine_ = nullptr;
            runtime_ = nullptr;
            initialized_ = false;
        }
        catch (...) {
            stream_ = nullptr;
            input_device_ = nullptr;
            output_device_ = nullptr;
            input_host_ = nullptr;
            output_host_ = nullptr;
            context_ = nullptr;
            engine_ = nullptr;
            runtime_ = nullptr;
            initialized_ = false;
        }
    }

}  // namespace yolo11
