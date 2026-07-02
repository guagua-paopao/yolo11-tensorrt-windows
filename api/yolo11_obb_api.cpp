#include "yolo11_obb_api.h"

#include <NvInfer.h>
#include <NvInferPlugin.h>
#include <cuda_runtime_api.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "cuda_utils.h"
#include "logging.h"
#include "model.h"
#include "postprocess.h"
#include "preprocess.h"
#include "utils.h"

namespace yolo11 {

    namespace {

        static Logger gLogger;

#ifdef _WIN32
        static HMODULE gYoloPluginHandle = nullptr;
#endif

        constexpr int kObbOutputSize =
            kMaxNumOutputBbox * sizeof(Detection) / sizeof(float) + 1;

        bool loadYoloPlugin() {
#ifdef _WIN32
            if (gYoloPluginHandle == nullptr) {
                gYoloPluginHandle = LoadLibraryA("myplugins.dll");

                if (!gYoloPluginHandle) {
                    std::cerr << "Failed to load myplugins.dll, Windows error code: "
                        << GetLastError() << std::endl;
                    return false;
                }

                std::cout << "myplugins.dll loaded successfully." << std::endl;
            }
#endif

            initLibNvInferPlugins(&gLogger, "");

            std::cout << "TensorRT plugins initialized." << std::endl;
            return true;
        }

    }  // namespace

    Yolo11ObbDetector::Yolo11ObbDetector() = default;

    Yolo11ObbDetector::~Yolo11ObbDetector() {
        release();
    }

    bool Yolo11ObbDetector::init(const ObbConfig& config) {
        if (initialized_) {
            release();
        }

        config_ = config;

        cudaError_t cuda_status = cudaSetDevice(config_.gpu_id);
        if (cuda_status != cudaSuccess) {
            std::cerr << "Failed to set CUDA device: "
                << cudaGetErrorString(cuda_status) << std::endl;
            return false;
        }

        if (!loadYoloPlugin()) {
            std::cerr << "Failed to load YOLO TensorRT plugin." << std::endl;
            return false;
        }

        if (!loadEngine(config_.engine_path)) {
            std::cerr << "Failed to load TensorRT engine: "
                << config_.engine_path << std::endl;
            return false;
        }

        auto* engine = reinterpret_cast<nvinfer1::ICudaEngine*>(engine_);
        if (!engine) {
            std::cerr << "Engine pointer is null after loading engine." << std::endl;
            return false;
        }

        auto out_dims = engine->getTensorShape(kOutputTensorName);
        if (out_dims.nbDims < 2) {
            std::cerr << "Invalid output tensor shape." << std::endl;
            release();
            return false;
        }

        model_bboxes_ = static_cast<int>(out_dims.d[1]);

        cudaStream_t stream = nullptr;
        cuda_status = cudaStreamCreate(&stream);
        if (cuda_status != cudaSuccess) {
            std::cerr << "Failed to create CUDA stream: "
                << cudaGetErrorString(cuda_status) << std::endl;
            release();
            return false;
        }

        stream_ = reinterpret_cast<void*>(stream);

        cuda_preprocess_init(kMaxInputImageSize);

        if (!prepareBuffers()) {
            std::cerr << "Failed to prepare CUDA buffers." << std::endl;
            release();
            return false;
        }

        initialized_ = true;

        std::cout << "Yolo11ObbDetector initialized successfully." << std::endl;
        std::cout << "kObbInputW = " << kObbInputW
            << ", kObbInputH = " << kObbInputH << std::endl;
        std::cout << "kBatchSize = " << kBatchSize << std::endl;
        std::cout << "kNumClass = " << kNumClass << std::endl;
        std::cout << "model_bboxes = " << model_bboxes_ << std::endl;
        std::cout << "use_gpu_postprocess = "
            << (config_.use_gpu_postprocess ? "true" : "false") << std::endl;

        return true;
    }

    bool Yolo11ObbDetector::loadEngine(const std::string& engine_path) {
        std::ifstream file(engine_path, std::ios::binary);
        if (!file.good()) {
            std::cerr << "Cannot open engine file: " << engine_path << std::endl;
            return false;
        }

        file.seekg(0, std::ifstream::end);
        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ifstream::beg);

        if (size == 0) {
            std::cerr << "Engine file is empty: " << engine_path << std::endl;
            return false;
        }

        std::vector<char> serialized_engine(size);
        file.read(serialized_engine.data(), static_cast<std::streamsize>(size));
        file.close();

        auto* runtime = nvinfer1::createInferRuntime(gLogger);
        if (!runtime) {
            std::cerr << "createInferRuntime failed." << std::endl;
            return false;
        }

        auto* engine = runtime->deserializeCudaEngine(
            serialized_engine.data(),
            size
        );

        if (!engine) {
            std::cerr << "deserializeCudaEngine failed." << std::endl;
            delete runtime;
            return false;
        }

        auto* context = engine->createExecutionContext();
        if (!context) {
            std::cerr << "createExecutionContext failed." << std::endl;
            delete engine;
            delete runtime;
            return false;
        }

        runtime_ = reinterpret_cast<void*>(runtime);
        engine_ = reinterpret_cast<void*>(engine);
        context_ = reinterpret_cast<void*>(context);

        return true;
    }

    bool Yolo11ObbDetector::prepareBuffers() {
        auto* engine = reinterpret_cast<nvinfer1::ICudaEngine*>(engine_);
        if (!engine) {
            std::cerr << "Engine is null in prepareBuffers." << std::endl;
            return false;
        }

        if (engine->getNbIOTensors() != 2) {
            std::cerr << "OBB model should have 1 input and 1 output." << std::endl;
            return false;
        }

        auto input_mode = engine->getTensorIOMode(kInputTensorName);
        if (input_mode != nvinfer1::TensorIOMode::kINPUT) {
            std::cerr << kInputTensorName << " is not input tensor." << std::endl;
            return false;
        }

        auto output_mode = engine->getTensorIOMode(kOutputTensorName);
        if (output_mode != nvinfer1::TensorIOMode::kOUTPUT) {
            std::cerr << kOutputTensorName << " is not output tensor." << std::endl;
            return false;
        }

        CUDA_CHECK(cudaMalloc(
            reinterpret_cast<void**>(&input_device_),
            kBatchSize * 3 * kObbInputH * kObbInputW * sizeof(float)
        ));

        CUDA_CHECK(cudaMalloc(
            reinterpret_cast<void**>(&output_device_),
            kBatchSize * kObbOutputSize * sizeof(float)
        ));

        if (!config_.use_gpu_postprocess) {
            output_host_ = new float[kBatchSize * kObbOutputSize];
        }
        else {
            if (kBatchSize > 1) {
                std::cerr << "GPU postprocess currently supports batch size 1 only." << std::endl;
                return false;
            }

            decode_host_ = new float[1 + kMaxNumOutputBbox * bbox_element];

            CUDA_CHECK(cudaMalloc(
                reinterpret_cast<void**>(&decode_device_),
                sizeof(float) * (1 + kMaxNumOutputBbox * bbox_element)
            ));
        }

        return true;
    }

    std::vector<Detection> Yolo11ObbDetector::infer(const cv::Mat& image) {
        std::vector<cv::Mat> batch;
        batch.push_back(image);

        auto batch_results = inferBatch(batch);

        if (batch_results.empty()) {
            return {};
        }

        return batch_results[0];
    }

    std::vector<std::vector<Detection>>
        Yolo11ObbDetector::inferBatch(const std::vector<cv::Mat>& images) {
        std::vector<std::vector<Detection>> results;

        if (!initialized_) {
            std::cerr << "OBB detector is not initialized." << std::endl;
            return results;
        }

        if (images.empty()) {
            return results;
        }

        if (images.size() > kBatchSize) {
            std::cerr << "Input batch size is larger than kBatchSize." << std::endl;
            return results;
        }

        for (const auto& img : images) {
            if (img.empty()) {
                std::cerr << "Input image is empty." << std::endl;
                return results;
            }
        }

        std::vector<cv::Mat> img_batch = images;

        while (img_batch.size() < kBatchSize) {
            img_batch.push_back(cv::Mat::zeros(images[0].size(), images[0].type()));
        }

        auto stream = reinterpret_cast<cudaStream_t>(stream_);
        auto* context = reinterpret_cast<nvinfer1::IExecutionContext*>(context_);

        if (!stream || !context) {
            std::cerr << "CUDA stream or TensorRT context is null." << std::endl;
            return results;
        }

        cuda_batch_preprocess(
            img_batch,
            input_device_,
            kObbInputW,
            kObbInputH,
            stream
        );

        context->setInputTensorAddress(kInputTensorName, input_device_);
        context->setOutputTensorAddress(kOutputTensorName, output_device_);

        bool enqueue_ok = context->enqueueV3(stream);
        if (!enqueue_ok) {
            std::cerr << "TensorRT enqueueV3 failed." << std::endl;
            return results;
        }

        if (!config_.use_gpu_postprocess) {
            CUDA_CHECK(cudaMemcpyAsync(
                output_host_,
                output_device_,
                kBatchSize * kObbOutputSize * sizeof(float),
                cudaMemcpyDeviceToHost,
                stream
            ));

            CUDA_CHECK(cudaStreamSynchronize(stream));

            batch_nms_obb(
                results,
                output_host_,
                static_cast<int>(images.size()),
                kObbOutputSize,
                kConfThresh,
                kNmsThresh
            );
        }
        else {
            CUDA_CHECK(cudaMemsetAsync(
                decode_device_,
                0,
                sizeof(float) * (1 + kMaxNumOutputBbox * bbox_element),
                stream
            ));

            cuda_decode_obb(
                output_device_,
                model_bboxes_,
                kConfThresh,
                decode_device_,
                kMaxNumOutputBbox,
                stream
            );

            cuda_nms_obb(
                decode_device_,
                kNmsThresh,
                kMaxNumOutputBbox,
                stream
            );

            CUDA_CHECK(cudaMemcpyAsync(
                decode_host_,
                decode_device_,
                sizeof(float) * (1 + kMaxNumOutputBbox * bbox_element),
                cudaMemcpyDeviceToHost,
                stream
            ));

            CUDA_CHECK(cudaStreamSynchronize(stream));

            processGpuObbResults(
                results,
                img_batch,
                static_cast<int>(images.size())
            );
        }

        if (results.size() > images.size()) {
            results.resize(images.size());
        }

        return results;
    }

    void Yolo11ObbDetector::processGpuObbResults(
        std::vector<std::vector<Detection>>& results,
        const std::vector<cv::Mat>& img_batch,
        int valid_batch_size
    ) {
        results.clear();
        results.resize(valid_batch_size);

        if (!decode_host_) {
            return;
        }

        int num_dets = static_cast<int>(decode_host_[0]);
        num_dets = std::max(0, std::min(num_dets, kMaxNumOutputBbox));

        const int det_size = bbox_element;

        for (int i = 0; i < num_dets; ++i) {
            float* ptr = decode_host_ + 1 + i * det_size;

            Detection det;
            std::memset(&det, 0, sizeof(Detection));
            std::memcpy(&det, ptr, sizeof(Detection));

            if (det.conf < kConfThresh) {
                continue;
            }

            // 当前 GPU OBB 后处理只支持 batch size = 1。
            // 所以所有结果都放到 results[0]。
            if (valid_batch_size > 0) {
                results[0].push_back(det);
            }
        }
    }

    cv::Mat Yolo11ObbDetector::draw(
        const cv::Mat& image,
        const std::vector<Detection>& detections
    ) {
        cv::Mat vis = image.clone();

        std::vector<cv::Mat> img_batch;
        img_batch.push_back(vis);

        std::vector<std::vector<Detection>> res_batch;
        res_batch.push_back(detections);

        draw_bbox_obb(img_batch, res_batch);

        return img_batch[0];
    }

    void Yolo11ObbDetector::release() {
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

        if (decode_device_) {
            cudaFree(decode_device_);
            decode_device_ = nullptr;
        }

        delete[] output_host_;
        output_host_ = nullptr;

        delete[] decode_host_;
        decode_host_ = nullptr;

        if (initialized_) {
            cuda_preprocess_destroy();
        }

        auto* context = reinterpret_cast<nvinfer1::IExecutionContext*>(context_);
        auto* engine = reinterpret_cast<nvinfer1::ICudaEngine*>(engine_);
        auto* runtime = reinterpret_cast<nvinfer1::IRuntime*>(runtime_);

        delete context;
        delete engine;
        delete runtime;

        context_ = nullptr;
        engine_ = nullptr;
        runtime_ = nullptr;

        model_bboxes_ = 0;
        initialized_ = false;
    }

}  // namespace yolo11