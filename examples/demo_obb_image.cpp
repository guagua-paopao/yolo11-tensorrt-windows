#include <iostream>
#include <string>
#include <filesystem>

#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>

#include "yolo11_obb_api.h"

static std::string makeDefaultOutputPath(const std::string& image_path) {
    std::filesystem::path input_path(image_path);

    std::string stem = input_path.stem().string();
    std::string ext = input_path.extension().string();

    if (stem.empty()) {
        stem = "result";
    }

    if (ext.empty()) {
        ext = ".jpg";
    }

    std::filesystem::path output_path =
        input_path.parent_path() / (stem + "_obb_result" + ext);

    return output_path.string();
}

int main(int argc, char** argv) {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    if (argc < 3) {
        std::cerr << "Usage:\n"
            << "  demo_obb_image.exe <engine_path> <image_path> [output_path] [cpu/gpu]\n\n"
            << "Examples:\n"
            << "  demo_obb_image.exe yolo11n-obb.engine test.jpg\n"
            << "  demo_obb_image.exe yolo11n-obb.engine test.jpg result.jpg\n"
            << "  demo_obb_image.exe yolo11n-obb.engine test.jpg result.jpg gpu\n";
        return -1;
    }

    std::string engine_path = argv[1];
    std::string image_path = argv[2];

    std::string output_path;
    if (argc >= 4) {
        output_path = argv[3];
    }
    else {
        output_path = makeDefaultOutputPath(image_path);
    }

    bool use_gpu_postprocess = false;
    if (argc >= 5) {
        std::string mode = argv[4];
        if (mode == "gpu" || mode == "g" || mode == "GPU") {
            use_gpu_postprocess = true;
        }
    }

    std::cout << "Engine path : " << engine_path << std::endl;
    std::cout << "Image path  : " << image_path << std::endl;
    std::cout << "Output path : " << output_path << std::endl;
    std::cout << "Postprocess : "
        << (use_gpu_postprocess ? "GPU" : "CPU") << std::endl;

    yolo11::ObbConfig config;
    config.engine_path = engine_path;
    config.gpu_id = 0;
    config.use_gpu_postprocess = use_gpu_postprocess;

    yolo11::Yolo11ObbDetector detector;

    if (!detector.init(config)) {
        std::cerr << "OBB detector init failed." << std::endl;
        return -1;
    }

    cv::Mat image = cv::imread(image_path);

    if (image.empty()) {
        std::cerr << "Failed to read image: " << image_path << std::endl;
        return -1;
    }

    auto detections = detector.infer(image);

    std::cout << "Detected OBB objects: " << detections.size() << std::endl;

    cv::Mat result = detector.draw(image, detections);

    if (!cv::imwrite(output_path, result)) {
        std::cerr << "Failed to save result image: " << output_path << std::endl;
        return -1;
    }

    std::cout << "Result saved to: " << output_path << std::endl;

    return 0;
}