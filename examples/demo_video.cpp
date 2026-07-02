#include <iostream>
#include <string>
#include <cctype>

#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>

#include "yolo11_detector_api.h"

static bool isCameraIndex(const std::string& s) {
    if (s.empty()) {
        return false;
    }

    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    return true;
}

int main(int argc, char** argv) {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    if (argc < 3) {
        std::cerr << "Usage:\n"
            << "  demo_video.exe <engine_path> <video_path_or_camera_id> [output_path]\n\n"
            << "Examples:\n"
            << "  demo_video.exe best.engine test.mp4\n"
            << "  demo_video.exe best.engine test.mp4 result_video.mp4\n"
            << "  demo_video.exe best.engine 0\n";
        return -1;
    }

    std::string engine_path = argv[1];
    std::string input_source = argv[2];
    std::string output_path = "result_video.mp4";

    if (argc >= 4) {
        output_path = argv[3];
    }

    std::cout << "Engine path : " << engine_path << std::endl;
    std::cout << "Input source: " << input_source << std::endl;
    std::cout << "Output path : " << output_path << std::endl;

    yolo11::DetectorConfig config;
    config.engine_path = engine_path;
    config.gpu_id = 0;
    config.use_gpu_postprocess = false;

    yolo11::Yolo11Detector detector;

    if (!detector.init(config)) {
        std::cerr << "Detector init failed." << std::endl;
        return -1;
    }

    cv::VideoCapture cap;
    bool use_camera = isCameraIndex(input_source);

    if (use_camera) {
        int camera_id = std::stoi(input_source);

        std::cout << "Opening camera index: " << camera_id << std::endl;

        cap.open(camera_id, cv::CAP_DSHOW);

        if (!cap.isOpened()) {
            std::cout << "CAP_DSHOW failed, trying CAP_MSMF..." << std::endl;
            cap.open(camera_id, cv::CAP_MSMF);
        }

        if (!cap.isOpened()) {
            std::cout << "CAP_MSMF failed, trying default backend..." << std::endl;
            cap.open(camera_id);
        }
    }
    else {
        std::cout << "Opening video file: " << input_source << std::endl;
        cap.open(input_source, cv::CAP_FFMPEG);
    }

    if (!cap.isOpened()) {
        std::cerr << "Failed to open input source: " << input_source << std::endl;
        return -1;
    }

    int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);

    if (width <= 0 || height <= 0) {
        std::cerr << "Invalid input frame size." << std::endl;
        return -1;
    }

    if (fps <= 0.0 || fps > 240.0) {
        fps = 25.0;
    }

    std::cout << "Frame width : " << width << std::endl;
    std::cout << "Frame height: " << height << std::endl;
    std::cout << "FPS         : " << fps << std::endl;

    cv::VideoWriter writer;

    if (!use_camera) {
        int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');

        writer.open(
            output_path,
            fourcc,
            fps,
            cv::Size(width, height)
        );

        if (!writer.isOpened()) {
            std::cerr << "Failed to open video writer: " << output_path << std::endl;
            return -1;
        }
    }

    cv::namedWindow("YOLO11 TensorRT", cv::WINDOW_NORMAL);

    cv::Mat frame;
    int frame_count = 0;

    while (true) {
        if (!cap.read(frame)) {
            break;
        }

        if (frame.empty()) {
            break;
        }

        auto detections = detector.infer(frame);
        cv::Mat result = detector.draw(frame, detections);

        if (writer.isOpened()) {
            writer.write(result);
        }

        cv::imshow("YOLO11 TensorRT", result);

        frame_count++;

        if (frame_count % 30 == 0) {
            std::cout << "Processed frames: " << frame_count
                << ", objects: " << detections.size()
                << std::endl;
        }

        int key = cv::waitKey(1);

        if (key == 27 || key == 'q' || key == 'Q') {
            break;
        }
    }

    cap.release();

    if (writer.isOpened()) {
        writer.release();
    }

    cv::destroyAllWindows();

    std::cout << "Video inference finished." << std::endl;
    std::cout << "Total processed frames: " << frame_count << std::endl;

    if (!use_camera) {
        std::cout << "Saved result video to: " << output_path << std::endl;
    }

    return 0;
}