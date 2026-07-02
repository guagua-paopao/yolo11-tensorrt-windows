# YOLO11 TensorRT Windows

YOLO11 TensorRT deployment on Windows with Visual Studio 2019, CUDA, TensorRT 10, and OpenCV.

This project provides a Windows-based TensorRT implementation for YOLO11 detection.  
In addition to the original command-line inference program, this version provides a C++ detector API, allowing YOLO11 TensorRT inference to be called directly from other C++ programs using `cv::Mat`.

## Features

- YOLO11 detection TensorRT deployment on Windows
- TensorRT 10 API support
- Visual Studio 2019 + CMake build
- CUDA preprocessing
- CPU post-processing and optional GPU post-processing path
- Custom TensorRT plugin support through `myplugins.dll`
- C++ detector API wrapper
- Image inference demo
- Video file inference demo
- Camera real-time inference demo

## Environment

Tested environment:

| Dependency | Version / Path |
|---|---|
| OS | Windows |
| IDE | Visual Studio 2019 |
| CUDA | `D:\GPU13.3` |
| cuDNN | `D:\cuDNN9\bin\13.3\x64` |
| TensorRT | `D:\TensorRT-10.16.1.11` |
| OpenCV | `D:\libs\opencv\build` |
| Python | 3.12 |
| GPU | RTX 4080 Laptop GPU |
| CUDA Architecture | `sm_89` |

Add the following directories to the system `Path`:

```text
D:\GPU13.3\bin
D:\cuDNN9\bin\13.3\x64
D:\TensorRT-10.16.1.11\bin
D:\libs\opencv\build\x64\vc16\bin
```

## Project Structure

```text
yolo11-tensorrt-windows
├── api
│   └── yolo11_detector_api.cpp
├── examples
│   ├── demo_image.cpp
│   └── demo_video.cpp
├── include
│   ├── config.h
│   ├── model.h
│   ├── postprocess.h
│   ├── preprocess.h
│   ├── utils.h
│   └── yolo11_detector_api.h
├── plugin
│   └── yololayer.cu
├── src
│   ├── preprocess.cu
│   ├── postprocess.cu
│   └── ...
├── images
├── gen_wts.py
├── yolo11_det.cpp
├── CMakeLists.txt
└── README.md
```

## Support

| Task | C++ Command-Line Demo | C++ API Wrapper | Python TensorRT Demo |
|---|---|---|---|
| Detection | Supported | Supported | Supported |
| Classification | Available in original code | Not wrapped yet | Supported |
| Segmentation | Available in original code | Not wrapped yet | Supported |
| Pose | Available in original code | Not wrapped yet | Supported |
| OBB | Available in original code | Not wrapped yet | Supported |

The current C++ API wrapper focuses on YOLO11 detection.

## Config

Main config file:

```text
include/config.h
```

For custom detection models, set the class number correctly:

```cpp
const static int kNumClass = 80;
```

Example for a one-class model:

```cpp
const static int kNumClass = 1;
```

After modifying `config.h`, rebuild the project and regenerate the TensorRT engine.

Do not reuse an old `.engine` file after changing class number, model structure, or TensorRT environment.

## Build

Open the project folder with Visual Studio 2019:

```text
File -> Open -> Folder
```

Open the project directory, for example:

```text
D:\tensorrtx\yolo11
```

If CMake cache already exists, delete:

```text
out/
```

Then configure and build the project.

The executable files are usually generated in:

```text
out\build\x64-Debug
```

Required runtime files:

```text
yolo11_det.exe
demo_image.exe
demo_video.exe
myplugins.dll
```

`myplugins.dll` must be placed in the same directory as the executable files.

## Convert `.pt` to `.wts`

Enter the project directory:

```bat
cd /d D:\tensorrtx\yolo11
```

For official YOLO11n detection model:

```bat
python gen_wts.py -w yolo11n.pt -o yolo11n.wts -t detect
```

For custom detection model:

```bat
python gen_wts.py -w best.pt -o best.wts -t detect
```

If a `C3k2` error occurs, update Ultralytics:

```bat
python -m pip install -U ultralytics
```

## Build TensorRT Engine

Enter the executable directory:

```bat
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
```

Serialize engine from `.wts`:

```bat
yolo11_det.exe -s best.wts best.engine n
```

Model scale argument:

| YOLO11 Model | Argument |
|---|---|
| YOLO11n | `n` |
| YOLO11s | `s` |
| YOLO11m | `m` |
| YOLO11l | `l` |
| YOLO11x | `x` |

Example:

```bat
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
```

For a custom model trained from YOLO11n:

```bat
yolo11_det.exe -s best.wts best.engine n
```

## Original Command-Line Inference

The original command-line detection program is still available.

Run inference on an image directory:

```bat
yolo11_det.exe -d best.engine D:/tensorrtx/yolo11/images c
```

The last argument controls the post-processing mode:

| Argument | Meaning |
|---|---|
| `c` | CPU post-processing |
| `g` | GPU post-processing |

Results are saved in the executable directory.

## C++ Detector API

Include the detector API header:

```cpp
#include "yolo11_detector_api.h"
```

Main class:

```cpp
yolo11::Yolo11Detector
```

Basic usage:

```cpp
#include <opencv2/opencv.hpp>
#include "yolo11_detector_api.h"

int main() {
    yolo11::DetectorConfig config;
    config.engine_path = "best.engine";
    config.gpu_id = 0;
    config.use_gpu_postprocess = false;

    yolo11::Yolo11Detector detector;

    if (!detector.init(config)) {
        return -1;
    }

    cv::Mat image = cv::imread("test.jpg");

    auto detections = detector.infer(image);

    cv::Mat result = detector.draw(image, detections);

    cv::imwrite("result.jpg", result);

    return 0;
}
```

Available API methods:

```cpp
bool init(const DetectorConfig& config);

std::vector<Detection> infer(const cv::Mat& image);

std::vector<std::vector<Detection>> inferBatch(
    const std::vector<cv::Mat>& images
);

cv::Mat draw(
    const cv::Mat& image,
    const std::vector<Detection>& detections
);

void release();
```

The API wraps:

- TensorRT engine loading
- YOLO TensorRT plugin loading
- CUDA stream creation
- GPU buffer allocation
- Image preprocessing
- TensorRT inference
- NMS post-processing
- Bounding box drawing
- Resource release

## Image Demo

Enter the executable directory:

```bat
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
```

Run image inference:

```bat
demo_image.exe best.engine test.jpg
```

Default output:

```text
test_result.jpg
```

Specify output path:

```bat
demo_image.exe best.engine test.jpg result.jpg
```

Absolute paths are recommended:

```bat
demo_image.exe "D:\tensorrtx\yolo11\out\build\x64-Debug\best.engine" "D:\tensorrtx\yolo11\out\build\x64-Debug\test.jpg"
```

## Video Demo

Enter the executable directory:

```bat
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
```

Run video file inference:

```bat
demo_video.exe best.engine test.mp4
```

Specify output video path:

```bat
demo_video.exe best.engine test.mp4 result_video.mp4
```

Default output:

```text
result_video.mp4
```

## Camera Demo

Run real-time camera inference:

```bat
demo_video.exe best.engine 0
```

`0` means the default camera index.

Try another camera index if needed:

```bat
demo_video.exe best.engine 1
```

Press `q` or `Esc` to exit.

## Common Problems

### 1. Cannot open engine file

Error:

```text
Cannot open engine file: best.engine
```

Reason:

The `.engine` file is not in the current working directory.

Solution:

Use an absolute path:

```bat
demo_image.exe "D:\tensorrtx\yolo11\out\build\x64-Debug\best.engine" "D:\tensorrtx\yolo11\out\build\x64-Debug\test.jpg"
```

Or copy the engine file to the executable directory:

```bat
copy /Y "D:\tensorrtx\yolo11\best.engine" "D:\tensorrtx\yolo11\out\build\x64-Debug\best.engine"
```

### 2. Cannot find plugin: YoloLayer_TRT

Error:

```text
Cannot find plugin: YoloLayer_TRT
```

Reason:

`myplugins.dll` is not loaded correctly.

Solution:

Make sure `myplugins.dll` is in the same directory as:

```text
yolo11_det.exe
demo_image.exe
demo_video.exe
```

Also make sure the following paths are added to system `Path`:

```text
D:\GPU13.3\bin
D:\TensorRT-10.16.1.11\bin
D:\libs\opencv\build\x64\vc16\bin
```

### 3. Failed to read image

Error:

```text
Failed to read image
```

Reason:

The image path is wrong or the image file does not exist.

Solution:

Use an absolute image path:

```bat
demo_image.exe best.engine "D:\tensorrtx\yolo11\images\bus.jpg"
```

### 4. Camera cannot be opened

Try another camera index:

```bat
demo_video.exe best.engine 1
```

Also make sure the camera is not used by another application.

### 5. No detection results

Check:

1. `kNumClass` in `include/config.h`
2. Whether the `.engine` file was regenerated after modifying `config.h`
3. Whether the model scale argument is correct, such as `n`, `s`, `m`, `l`, or `x`
4. Whether the confidence threshold is too high
5. Whether the input image belongs to the trained classes

## Minimal Workflow

```bat
cd /d D:\tensorrtx\yolo11

python gen_wts.py -w best.pt -o best.wts -t detect

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug

yolo11_det.exe -s best.wts best.engine n

demo_image.exe best.engine test.jpg

demo_video.exe best.engine test.mp4 result_video.mp4

demo_video.exe best.engine 0
```

## Custom Model Notes

For a custom detection model:

1. Prepare a YOLO11 detection `.pt` model.
2. Convert `.pt` to `.wts`.
3. Set `kNumClass` in `include/config.h`.
4. Rebuild the project.
5. Generate a new `.engine`.
6. Run `demo_image.exe` or `demo_video.exe`.

Do not reuse an old TensorRT engine after changing:

- class number
- model scale
- TensorRT version
- CUDA version
- plugin implementation
- network structure

## Git Ignore

Generated files should not be uploaded to GitHub:

```text
out/
build/
.vs/
*.exe
*.dll
*.lib
*.pdb
*.obj
*.pt
*.wts
*.engine
*.onnx
*.mp4
*.avi
```

If sample images are required, place only small test images in the `images/` directory.

## Acknowledgement

This project is modified from `tensorrtx/yolo11`.

The detector API, image demo, video file demo, and camera demo were added for easier integration into Windows C++ applications.
