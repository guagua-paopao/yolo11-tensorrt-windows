#include "yolo11_seg_api.h"

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
#include <vector>

#include "config.h"
#include "cuda_utils.h"
#include "logging.h"
#include "model.h"
#include "preprocess.h"
#include "utils.h"

namespace yolo11 {

    namespace {
        static Logger gLogger;

#ifdef _WIN32
        static HMODULE gYoloPluginHandle = nullptr;
#endif

        constexpr int kSegDetOutputSize = kMaxNumOutputBbox * sizeof(Detection) / sizeof(float) + 1;
        constexpr int kSegProtoOutputSize = 32 * (kInputH / 4) * (kInputW / 4);

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

        cv::Rect getDownscaleRect(float bbox[4], float scale) {
            float left = bbox[0];
            float top = bbox[1];
            float right = bbox[0] + bbox[2];
            float bottom = bbox[1] + bbox[3];

            left = std::max(0.0f, left);
            top = std::max(0.0f, top);
            right = std::min(static_cast<float>(kInputW), right);
            bottom = std::min(static_cast<float>(kInputH), bottom);

            left /= scale;
            top /= scale;
            right /= scale;
            bottom /= scale;

            const int x = std::max(0, static_cast<int>(left));
            const int y = std::max(0, static_cast<int>(top));
            const int w = std::max(0, static_cast<int>(right - left));
            const int h = std::max(0, static_cast<int>(bottom - top));
            return cv::Rect(x, y, w, h);
        }

        std::vector<cv::Mat> processMasks(const float* proto, int proto_size, const std::vector<Detection>& dets) {
            std::vector<cv::Mat> masks;
            if (proto == nullptr || proto_size <= 0) {
                return masks;
            }

            for (const auto& det : dets) {
                cv::Mat mask_mat = cv::Mat::zeros(kInputH / 4, kInputW / 4, CV_32FC1);
                cv::Rect r = getDownscaleRect(const_cast<float*>(det.bbox), 4.0f);
                r &= cv::Rect(0, 0, mask_mat.cols, mask_mat.rows);

                for (int x = r.x; x < r.x + r.width; ++x) {
                    for (int y = r.y; y < r.y + r.height; ++y) {
                        float e = 0.0f;
                        for (int j = 0; j < 32; ++j) {
                            e += det.mask[j] * proto[j * proto_size / 32 + y * mask_mat.cols + x];
                        }
                        e = 1.0f / (1.0f + std::exp(-e));
                        mask_mat.at<float>(y, x) = e;
                    }
                }

                cv::resize(mask_mat, mask_mat, cv::Size(kInputW, kInputH));
                masks.push_back(mask_mat);
            }
            return masks;
        }

        void ensureDefaultLabels(std::unordered_map<int, std::string>& labels_map) {
            if (!labels_map.empty()) {
                return;
            }
            for (int i = 0; i < kNumClass; ++i) {
                labels_map[i] = "class_" + std::to_string(i);
            }
        }
    }  // namespace

    Yolo11SegDetector::Yolo11SegDetector() = default;

    Yolo11SegDetector::~Yolo11SegDetector() {
        release();
    }

    bool Yolo11SegDetector::init(const SegConfig& config) {
        if (initialized_) {
            release();
        }

        config_ = config;
        if (config_.use_gpu_postprocess) {
            std::cerr << "Seg GPU postprocess is not supported yet; fallback to CPU postprocess." << std::endl;
            config_.use_gpu_postprocess = false;
        }

        cudaError_t cuda_status = cudaSetDevice(config_.gpu_id);
        if (cuda_status != cudaSuccess) {
            std::cerr << "Failed to set CUDA device for SEG: " << cudaGetErrorString(cuda_status) << std::endl;
            return false;
        }

        if (!loadTensorRtPlugins()) {
            std::cerr << "Failed to initialize TensorRT plugins for SEG." << std::endl;
            return false;
        }

        labels_map_.clear();
        if (!config_.labels_path.empty()) {
            read_labels(config_.labels_path, labels_map_);
        }
        ensureDefaultLabels(labels_map_);

        if (!loadEngine(config_.engine_path)) {
            std::cerr << "Failed to load SEG TensorRT engine: " << config_.engine_path << std::endl;
            release();
            return false;
        }

        auto* engine = reinterpret_cast<nvinfer1::ICudaEngine*>(engine_);
        if (!engine) {
            std::cerr << "SEG engine pointer is null after loading engine." << std::endl;
            release();
            return false;
        }

        const auto out_dims = engine->getTensorShape(kOutputTensorName);
        if (out_dims.nbDims < 2) {
            std::cerr << "Invalid SEG output tensor shape." << std::endl;
            release();
            return false;
        }
        model_bboxes_ = static_cast<int>(out_dims.d[1]);

        cudaStream_t stream = nullptr;
        cuda_status = cudaStreamCreate(&stream);
        if (cuda_status != cudaSuccess) {
            std::cerr << "Failed to create CUDA stream for SEG: " << cudaGetErrorString(cuda_status) << std::endl;
            release();
            return false;
        }
        stream_ = reinterpret_cast<void*>(stream);

        cuda_preprocess_init(kMaxInputImageSize);

        if (!prepareBuffers()) {
            std::cerr << "Failed to prepare SEG buffers." << std::endl;
            release();
            return false;
        }

        initialized_ = true;
        std::cout << "Yolo11SegDetector initialized successfully." << std::endl;
        std::cout << "kInputW = " << kInputW << ", kInputH = " << kInputH << std::endl;
        std::cout << "kNumClass = " << kNumClass << ", model_bboxes = " << model_bboxes_ << std::endl;
        return true;
    }

    bool Yolo11SegDetector::loadEngine(const std::string& engine_path) {
        std::ifstream file(engine_path, std::ios::binary);
        if (!file.good()) {
            std::cerr << "Cannot open SEG engine file: " << engine_path << std::endl;
            return false;
        }

        file.seekg(0, std::ifstream::end);
        const size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ifstream::beg);
        if (size == 0) {
            std::cerr << "SEG engine file is empty: " << engine_path << std::endl;
            return false;
        }

        std::vector<char> serialized_engine(size);
        file.read(serialized_engine.data(), static_cast<std::streamsize>(size));
        file.close();

        auto* runtime = nvinfer1::createInferRuntime(gLogger);
        if (!runtime) {
            std::cerr << "createInferRuntime failed for SEG." << std::endl;
            return false;
        }

        auto* engine = runtime->deserializeCudaEngine(serialized_engine.data(), size);
        if (!engine) {
            std::cerr << "deserializeCudaEngine failed for SEG." << std::endl;
            delete runtime;
            return false;
        }

        auto* context = engine->createExecutionContext();
        if (!context) {
            std::cerr << "createExecutionContext failed for SEG." << std::endl;
            delete engine;
            delete runtime;
            return false;
        }

        runtime_ = reinterpret_cast<void*>(runtime);
        engine_ = reinterpret_cast<void*>(engine);
        context_ = reinterpret_cast<void*>(context);
        return true;
    }

    bool Yolo11SegDetector::prepareBuffers() {
        auto* engine = reinterpret_cast<nvinfer1::ICudaEngine*>(engine_);
        if (!engine) {
            std::cerr << "SEG engine is null in prepareBuffers." << std::endl;
            return false;
        }

        if (engine->getNbIOTensors() != 3) {
            std::cerr << "SEG model should have 1 input and 2 outputs." << std::endl;
            return false;
        }

        const auto input_mode = engine->getTensorIOMode(kInputTensorName);
        const auto output_mode = engine->getTensorIOMode(kOutputTensorName);
        const auto proto_mode = engine->getTensorIOMode(kProtoTensorName);
        if (input_mode != nvinfer1::TensorIOMode::kINPUT) {
            std::cerr << kInputTensorName << " is not SEG input tensor." << std::endl;
            return false;
        }
        if (output_mode != nvinfer1::TensorIOMode::kOUTPUT) {
            std::cerr << kOutputTensorName << " is not SEG output tensor." << std::endl;
            return false;
        }
        if (proto_mode != nvinfer1::TensorIOMode::kOUTPUT) {
            std::cerr << kProtoTensorName << " is not SEG proto output tensor." << std::endl;
            return false;
        }

        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&input_device_), kBatchSize * 3 * kInputH * kInputW * sizeof(float)));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&output_device_), kBatchSize * kSegDetOutputSize * sizeof(float)));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&proto_device_), kBatchSize * kSegProtoOutputSize * sizeof(float)));

        output_host_ = new float[kBatchSize * kSegDetOutputSize];
        proto_host_ = new float[kBatchSize * kSegProtoOutputSize];
        return true;
    }

    std::vector<SegmentationResult> Yolo11SegDetector::infer(const cv::Mat& image) {
        std::vector<cv::Mat> batch;
        batch.push_back(image);
        auto batch_results = inferBatch(batch);
        if (batch_results.empty()) {
            return {};
        }
        return batch_results[0];
    }

    std::vector<std::vector<SegmentationResult>> Yolo11SegDetector::inferBatch(const std::vector<cv::Mat>& images) {
        std::vector<std::vector<SegmentationResult>> results;
        if (!initialized_) {
            std::cerr << "SEG detector is not initialized." << std::endl;
            return results;
        }
        if (images.empty()) {
            return results;
        }
        if (static_cast<int>(images.size()) > kBatchSize) {
            std::cerr << "SEG inferBatch images.size() exceeds kBatchSize." << std::endl;
            return results;
        }
        for (const auto& image : images) {
            if (image.empty()) {
                std::cerr << "SEG inferBatch received empty image." << std::endl;
                return results;
            }
        }

        auto* context = reinterpret_cast<nvinfer1::IExecutionContext*>(context_);
        auto stream = reinterpret_cast<cudaStream_t>(stream_);
        if (!context || !stream || !input_device_ || !output_device_ || !proto_device_ || !output_host_ || !proto_host_) {
            std::cerr << "SEG TensorRT resources are incomplete." << std::endl;
            return results;
        }

        std::vector<cv::Mat> img_batch = images;
        while (img_batch.size() < kBatchSize) {
            img_batch.push_back(cv::Mat::zeros(images[0].size(), images[0].type()));
        }

        cuda_batch_preprocess(img_batch, input_device_, kInputW, kInputH, stream);

        context->setInputTensorAddress(kInputTensorName, input_device_);
        context->setOutputTensorAddress(kOutputTensorName, output_device_);
        context->setOutputTensorAddress(kProtoTensorName, proto_device_);
        const bool enqueue_ok = context->enqueueV3(stream);
        if (!enqueue_ok) {
            std::cerr << "SEG enqueueV3 failed." << std::endl;
            return results;
        }

        CUDA_CHECK(cudaMemcpyAsync(output_host_, output_device_, static_cast<size_t>(kBatchSize) * kSegDetOutputSize * sizeof(float), cudaMemcpyDeviceToHost, stream));
        CUDA_CHECK(cudaMemcpyAsync(proto_host_, proto_device_, static_cast<size_t>(kBatchSize) * kSegProtoOutputSize * sizeof(float), cudaMemcpyDeviceToHost, stream));
        CUDA_CHECK(cudaStreamSynchronize(stream));

        std::vector<std::vector<Detection>> det_batches;
        batch_nms(det_batches, output_host_, static_cast<int>(images.size()), kSegDetOutputSize, kConfThresh, kNmsThresh);

        results.resize(images.size());
        for (size_t b = 0; b < images.size(); ++b) {
            auto& dets = det_batches[b];
            const float* proto = proto_host_ + b * kSegProtoOutputSize;
            const auto masks = processMasks(proto, kSegProtoOutputSize, dets);
            for (size_t i = 0; i < dets.size(); ++i) {
                SegmentationResult item;
                item.detection = dets[i];
                if (i < masks.size()) {
                    item.mask = masks[i];
                }
                results[b].push_back(item);
            }
        }
        return results;
    }

    cv::Mat Yolo11SegDetector::draw(const cv::Mat& image, const std::vector<SegmentationResult>& segmentations) {
        if (image.empty()) {
            return {};
        }
        cv::Mat vis = image.clone();
        std::vector<Detection> dets;
        std::vector<cv::Mat> masks;
        dets.reserve(segmentations.size());
        masks.reserve(segmentations.size());
        for (const auto& item : segmentations) {
            dets.push_back(item.detection);
            masks.push_back(item.mask);
        }
        draw_mask_bbox(vis, dets, masks, labels_map_);
        return vis;
    }

    void Yolo11SegDetector::release() noexcept {
        try {
            if (stream_) {
                cudaStreamDestroy(reinterpret_cast<cudaStream_t>(stream_));
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
            if (proto_device_) {
                cudaFree(proto_device_);
                proto_device_ = nullptr;
            }

            delete[] output_host_;
            output_host_ = nullptr;
            delete[] proto_host_;
            proto_host_ = nullptr;

            auto* context = reinterpret_cast<nvinfer1::IExecutionContext*>(context_);
            auto* engine = reinterpret_cast<nvinfer1::ICudaEngine*>(engine_);
            auto* runtime = reinterpret_cast<nvinfer1::IRuntime*>(runtime_);
            delete context;
            delete engine;
            delete runtime;
            context_ = nullptr;
            engine_ = nullptr;
            runtime_ = nullptr;

            if (initialized_) {
                cuda_preprocess_destroy();
            }
            initialized_ = false;
            model_bboxes_ = 0;
        }
        catch (...) {
            stream_ = nullptr;
            input_device_ = nullptr;
            output_device_ = nullptr;
            proto_device_ = nullptr;
            output_host_ = nullptr;
            proto_host_ = nullptr;
            context_ = nullptr;
            engine_ = nullptr;
            runtime_ = nullptr;
            initialized_ = false;
            model_bboxes_ = 0;
        }
    }

}  // namespace yolo11
