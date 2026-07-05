# YOLO11 TensorRT Windows

Windows-based YOLO11 TensorRT deployment with Visual Studio 2019, CUDA, TensorRT 10, OpenCV, reusable C++ runtime APIs, and a Phase 1.5 pure C++ HTTP detection server.

This project keeps the original TensorRT command-line demos and adds reusable C++ API wrappers so YOLO11 inference can be called directly from other C++ programs through `cv::Mat`. The current service extension also provides a Crow-based HTTP server for synchronous image detection.

## Current Status

| Area | Status |
|---|---|
| YOLO11 detection command-line inference | Supported |
| YOLO11 OBB command-line inference | Supported |
| C++ detection runtime API | Supported |
| C++ OBB runtime API | Supported, CPU post-processing verified |
| Detection image demo | Supported |
| Detection video / camera demo | Supported |
| OBB image demo | Supported |
| Pure C++ HTTP detection server | Phase 1.5 supported |
| Result image HTTP access | Supported |
| `class_name` in HTTP JSON response | Supported for COCO class order |
| Debug raw model bbox output | Supported with `?debug=true` |
| Redis async task queue | Planned next |
| OBB / Seg / Pose / Video service APIs | Planned later |

## Features

- YOLO11 detection TensorRT deployment on Windows
- YOLO11 OBB oriented bounding box TensorRT deployment on Windows
- TensorRT 10 API support
- Visual Studio 2019 + CMake build
- CUDA preprocessing
- CPU post-processing and optional GPU post-processing path for detection
- Custom TensorRT plugin support through `myplugins.dll`
- Reusable C++ runtime API wrappers
  - `Yolo11Detector`
  - `Yolo11ObbDetector`
- Original command-line demos preserved
- Pure C++ HTTP detection server
  - `GET /api/v1/health`
  - `POST /api/v1/detect/image`
  - `GET /api/v1/image/<filename>`
- HTTP JSON response with original-image-pixel bbox coordinates
- Optional debug response with raw model bbox coordinates

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
| vcpkg | `D:\vcpkg` |
| Python | 3.12 |
| GPU | RTX 4080 Laptop GPU |
| CUDA Architecture | `sm_89` |

Add the following directories to the system `Path` or to the current terminal session before running executables:

```text
D:\GPU13.3\bin
D:\cuDNN9\bin\13.3\x64
D:\TensorRT-10.16.1.11\lib
D:\libs\opencv\build\x64\vc16\bin
```

PowerShell example:

```powershell
$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"
```

## Third-Party Dependencies for HTTP Server

The HTTP server uses:

- Crow
- nlohmann/json
- yaml-cpp
- asio, installed as a Crow dependency

Install them through vcpkg:

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
```

When configuring CMake manually, use the vcpkg toolchain file:

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

If Visual Studio CMake is used, make sure the project configuration also uses the same vcpkg toolchain.

## Project Structure

```text
yolo11-tensorrt-windows
├── api
│   ├── yolo11_detector_api.cpp
│   └── yolo11_obb_api.cpp
├── config
│   └── server.yaml
├── examples
│   ├── demo_image.cpp
│   ├── demo_video.cpp
│   └── demo_obb_image.cpp
├── include
│   ├── server
│   │   ├── app_config.h
│   │   ├── http_controller.h
│   │   ├── image_codec.h
│   │   └── result_serializer.h
│   ├── config.h
│   ├── model.h
│   ├── postprocess.h
│   ├── preprocess.h
│   ├── utils.h
│   ├── yolo11_detector_api.h
│   └── yolo11_obb_api.h
├── plugin
│   └── yololayer.cu
├── src
│   ├── server
│   │   ├── app_config.cpp
│   │   ├── http_controller.cpp
│   │   ├── image_codec.cpp
│   │   ├── main_server.cpp
│   │   └── result_serializer.cpp
│   ├── preprocess.cu
│   ├── postprocess.cpp
│   └── ...
├── images
├── engine
│   └── yolo11n.engine
├── output
├── gen_wts.py
├── yolo11_det.cpp
├── yolo11_obb.cpp
├── CMakeLists.txt
├── README_EN.md
└── README_CN.md
```

Generated directories and model files such as `out/`, `build/`, `output/`, `*.engine`, `*.pt`, and `*.wts` should not be committed to Git.

## Support Matrix

| Task | Original Command-Line Demo | C++ API Wrapper | Demo Program | HTTP Server | Status |
|---|---|---|---|---|---|
| Detection | `yolo11_det.exe` | `Yolo11Detector` | `demo_image.exe`, `demo_video.exe` | `yolo11_server.exe` | Supported |
| OBB | `yolo11_obb.exe` | `Yolo11ObbDetector` | `demo_obb_image.exe` | Planned | API and CPU demo verified |
| Classification | Source code available | Planned | Planned | Planned | Not wrapped yet |
| Segmentation | Source code available | Planned | Planned | Planned | Not wrapped yet |
| Pose | Source code available | Planned | Planned | Planned | Not wrapped yet |

For OBB, CPU post-processing is the recommended validation path. The OBB GPU post-processing path is reserved for further completion and testing.

## Config

### Model Config

Main model configuration file:

```text
include/config.h
```

For detection models trained on COCO:

```cpp
const static int kNumClass = 80;
```

For official YOLO11 OBB models trained on DOTA:

```cpp
const static int kNumClass = 16;
```

For a custom one-class model:

```cpp
const static int kNumClass = 1;
```

After modifying `config.h`, rebuild the project and regenerate the TensorRT engine.

Do not reuse an old `.engine` file after changing class number, model structure, TensorRT version, CUDA version, or GPU architecture.

### HTTP Server Config

Server configuration file:

```text
config/server.yaml
```

Example:

```yaml
server:
  host: "0.0.0.0"
  port: 8080
  threads: 4

model:
  type: "detect"
  engine_path: "D:/tensorrtx/yolo11/engine/yolo11n.engine"
  gpu_id: 0
  use_gpu_postprocess: false

output:
  save_result_image: true
  output_dir: "./output"
  jpeg_quality: 90
```

Use an absolute engine path when possible. A common mistake is using `./engines/yolo11n.engine` while the actual folder is `./engine/yolo11n.engine`.

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

Then configure and build the required targets.

The executable files are usually generated in:

```text
out\build\x64-Debug
```

Common build targets:

```text
myplugins
yolo11_det
yolo11_obb
demo_image
demo_video
demo_obb_image
yolo11_server
```

Required runtime file:

```text
myplugins.dll
```

`myplugins.dll` must be placed in the same directory as the executable files. The provided CMake configuration copies it automatically after build.

## CMake Notes

The server source files must not be compiled into the original demo targets. Keep `src/server/*.cpp` only in `yolo11_server`.

A safe pattern is:

```cmake
file(GLOB_RECURSE SRCS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.cu
)

list(FILTER SRCS EXCLUDE REGEX ".*src[/\\\\]server[/\\\\].*")
```

Then link HTTP server dependencies only to `yolo11_server`:

```cmake
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)

target_link_libraries(yolo11_server PRIVATE
    yolo11_runtime
    myplugins
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    Crow::Crow
)
```

## Download YOLO11 Models

Example Python script:

```python
from pathlib import Path
import os
from ultralytics import YOLO

models = [
    "yolo11n.pt",
    "yolo11n-obb.pt",
]

save_dir = Path("weights")
save_dir.mkdir(exist_ok=True)
os.chdir(save_dir)

for model_name in models:
    print(f"Loading: {model_name}")
    model = YOLO(model_name)
    print(Path(model_name).resolve())
```

Use `yolo11n.pt` for detection and `yolo11n-obb.pt` for OBB.

## Convert `.pt` to `.wts`

Enter the project directory:

```bat
cd /d D:\tensorrtx\yolo11
```

Detection model:

```bat
python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
```

OBB model:

```bat
python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
```

Custom detection model:

```bat
python gen_wts.py -w weights/best.pt -o best.wts -t detect
```

Custom OBB model:

```bat
python gen_wts.py -w weights/best.pt -o best-obb.wts -t obb
```

If a `C3k2` error occurs, update Ultralytics:

```bat
python -m pip install -U ultralytics
```

## Build TensorRT Engine

Copy `.wts` files to the executable directory:

```bat
copy /Y yolo11n.wts out\build\x64-Debug\
copy /Y yolo11n-obb.wts out\build\x64-Debug\
```

Enter the executable directory:

```bat
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
```

Build detection engine:

```bat
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
```

Build OBB engine:

```bat
yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
```

Model scale argument:

| YOLO11 Model | Argument |
|---|---|
| YOLO11n | `n` |
| YOLO11s | `s` |
| YOLO11m | `m` |
| YOLO11l | `l` |
| YOLO11x | `x` |

For a custom model trained from YOLO11n, use `n` as the scale argument.

## Original Command-Line Inference

### Detection

Run detection on an image directory:

```bat
yolo11_det.exe -d yolo11n.engine D:\tensorrtx\yolo11\images c
```

The last argument controls post-processing mode:

| Argument | Meaning |
|---|---|
| `c` | CPU post-processing |
| `g` | GPU post-processing |

Results are saved in the executable directory.

### OBB

Run OBB inference on an image directory:

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

Use `c` first to validate OBB inference. OBB test images should match the OBB model domain, such as aerial images from DOTA-like scenes.

## C++ Detection API

Header:

```cpp
#include "yolo11_detector_api.h"
```

Example:

```cpp
yolo11::DetectorConfig config;
config.engine_path = "yolo11n.engine";
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
detector.release();
```

Run image demo:

```bat
demo_image.exe yolo11n.engine D:\tensorrtx\yolo11\images\a.jpg det_result.jpg
```

Run video demo:

```bat
demo_video.exe yolo11n.engine D:\tensorrtx\yolo11\test.mp4 result_video.mp4
```

Run camera demo:

```bat
demo_video.exe yolo11n.engine 0
```

## C++ OBB API

Header:

```cpp
#include "yolo11_obb_api.h"
```

Example:

```cpp
yolo11::ObbConfig config;
config.engine_path = "yolo11n-obb.engine";
config.gpu_id = 0;
config.use_gpu_postprocess = false;

yolo11::Yolo11ObbDetector detector;

if (!detector.init(config)) {
    return -1;
}

cv::Mat image = cv::imread("a.jpg");
auto detections = detector.infer(image);
cv::Mat result = detector.draw(image, detections);

cv::imwrite("obb_result.jpg", result);
detector.release();
```

Run OBB image demo:

```bat
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

The last argument controls OBB demo post-processing mode:

| Argument | Meaning |
|---|---|
| `cpu` | CPU OBB post-processing |
| `gpu` | GPU OBB post-processing path, experimental |

Recommended validation command:

```bat
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

## Pure C++ HTTP Detection Server

Current server phase:

```text
Phase 1.5: synchronous HTTP image detection service
```

Supported endpoints:

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/v1/health` | Service health and model config |
| POST | `/api/v1/detect/image` | Synchronous image detection |
| POST | `/api/v1/detect/image?debug=true` | Detection with raw model bbox debug fields |
| GET | `/api/v1/image/<filename>` | Read saved result image |

### Build Server Target

```powershell
cd D:\tensorrtx\yolo11
cmake --build out\build\x64-Debug --config Debug --target yolo11_server
```

### Run Server

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml
```

If the executable is not found, locate it:

```powershell
Get-ChildItem -Recurse -Filter yolo11_server.exe | Select-Object FullName
```

### Health Check

```powershell
curl.exe http://127.0.0.1:8080/api/v1/health
```

Expected response includes:

```json
{
    "success": true,
    "status": "ok",
    "service": "yolo11_server",
    "phase": "phase1_sync_http",
    "model_type": "detect"
}
```

### Image Detection

For PNG:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

For JPEG:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/jpeg" --data-binary "@D:/tensorrtx/yolo11/images/bus.jpg"
```

Response example:

```json
{
    "success": true,
    "model_type": "detect",
    "image": {
        "width": 474,
        "height": 316,
        "channels": 3
    },
    "bbox_coordinate_system": "original_image_pixels",
    "bbox_format": "xywh_and_xyxy",
    "num_detections": 3,
    "detections": [
        {
            "class_id": 0,
            "class_name": "person",
            "confidence": 0.9210741519927979,
            "clipped": false,
            "bbox": {
                "x": 140,
                "y": 68,
                "w": 63,
                "h": 204,
                "x1": 140,
                "y1": 68,
                "x2": 203,
                "y2": 272
            }
        }
    ],
    "result_image_url": "/api/v1/image/result_xxx.jpg",
    "result_image_url_full": "http://127.0.0.1:8080/api/v1/image/result_xxx.jpg"
}
```

The `bbox` field is returned in original input image pixel coordinates. The JSON bbox is mapped through the same coordinate restoration logic used by the drawing path, so it is consistent with the saved result image.

### Debug Mode

Debug mode returns raw model bbox coordinates before restoration to original image coordinates:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image?debug=true" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Additional fields:

```json
{
    "debug": true,
    "debug_note": "raw_model_bbox is returned only when debug=true or debug=1.",
    "detections": [
        {
            "raw_model_bbox": {
                "x1": 189.4034423828125,
                "y1": 198.76870727539063,
                "x2": 274.6661071777344,
                "y2": 474.3380432128906
            }
        }
    ]
}
```

Use debug mode for backend troubleshooting only. Frontends should use the restored `bbox` field.

### View Result Image

Open the returned URL in a browser:

```text
http://127.0.0.1:8080/api/v1/image/result_xxx.jpg
```

Or download it:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/image/result_xxx.jpg" --output test_result.jpg
start .\test_result.jpg
```

## Minimal Commands

### Detection CLI and API Demo

```bat
cd /d D:\tensorrtx\yolo11

python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
copy /Y yolo11n.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug

yolo11_det.exe -s yolo11n.wts yolo11n.engine n
yolo11_det.exe -d yolo11n.engine D:\tensorrtx\yolo11\images c

demo_image.exe yolo11n.engine D:\tensorrtx\yolo11\images\a.jpg det_result.jpg
```

### HTTP Server

```powershell
cd D:\tensorrtx\yolo11

cmake --build out\build\x64-Debug --config Debug --target yolo11_server

.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml

curl.exe http://127.0.0.1:8080/api/v1/health

curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

### OBB

Before building OBB engine, set `kNumClass = 16` in `include/config.h` for the official DOTA OBB model, then rebuild the project.

```bat
cd /d D:\tensorrtx\yolo11

python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
copy /Y yolo11n-obb.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug

yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c

demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

## Troubleshooting

### `Cannot open engine file`

Check `config/server.yaml`:

```yaml
engine_path: "D:/tensorrtx/yolo11/engine/yolo11n.engine"
```

Make sure the file exists. Do not confuse `engine/` with `engines/`.

### PowerShell cannot run `build\Release\yolo11_server.exe`

In PowerShell, run relative executables with `.\`:

```powershell
.\out\build\x64-Debug\yolo11_server.exe
```

If the path is unknown:

```powershell
Get-ChildItem -Recurse -Filter yolo11_server.exe | Select-Object FullName
```

### `Failed to load myplugins.dll`

Make sure `myplugins.dll` is in the same directory as the executable. The CMake build copies it automatically, but manual copying may be needed if the executable is moved.

### `read xxx.engine error!`

The engine file does not exist in the current executable directory, or the engine name is wrong. Use:

```bat
dir *.engine
```

### `Detected OBB objects: 0`

Common causes:

- `kNumClass` is still `80`, but the OBB model expects `16` classes.
- A detection engine is used with the OBB demo.
- The test image does not match the OBB model domain.
- Confidence threshold is too high.

For debugging, lower the threshold in `include/config.h`:

```cpp
const static float kConfThresh = 0.25f;
```

Then rebuild and regenerate the engine.

### `vector subscript out of range` when using `g`

Use CPU post-processing for OBB validation:

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

### `gen_wts.py` does not accept `-t obb`

Update `gen_wts.py` so the type choices include `obb`:

```python
choices=['detect', 'cls', 'seg', 'pose', 'obb']
```

And make sure OBB is included in the YOLO-head branch:

```python
if m_type in ['detect', 'seg', 'pose', 'obb']:
```

### JSON bbox is inconsistent with the result image

The HTTP server should serialize bboxes using the same coordinate restoration logic as drawing. If this issue appears again, check that `src/server/http_controller.cpp` uses `get_rect()` when converting `Detection` to JSON.

## Git Ignore

Do not upload generated files to GitHub:

```text
out/
build/
.vs/
output/
logs/
*.exe
*.dll
*.lib
*.pdb
*.obj
*.pt
*.wts
*.engine
*.onnx
```

## Roadmap

Next planned step:

```text
Phase 2: Redis async image detection queue
```

Planned endpoints:

| Method | Endpoint | Description |
|---|---|---|
| POST | `/api/v1/detect/image/async` | Submit image and return `task_id` |
| GET | `/api/v1/result/{task_id}` | Query queued/running/done/failed and result JSON |
| GET | `/api/v1/image/{filename}` | Read result image |

Future extensions:

- OBB HTTP service
- Video file async detection
- RTSP / camera stream service
- Multi-worker inference pool
- Multi-GPU scheduling
- Micro-batching with `inferBatch`
- Redis + object storage / database productionization

## Acknowledgement

Modified from `tensorrtx/yolo11`.
