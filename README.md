# YOLO11 TensorRT Windows

YOLO11 TensorRT deployment on Windows with Visual Studio 2019, CUDA, TensorRT 10, and OpenCV.

This project provides Windows-based TensorRT inference for YOLO11. It keeps the original command-line demos and adds reusable C++ API wrappers so YOLO11 inference can be called directly from other C++ programs using `cv::Mat`.

## Features

- YOLO11 detection TensorRT deployment on Windows
- YOLO11 OBB oriented bounding box TensorRT deployment on Windows
- TensorRT 10 API support
- Visual Studio 2019 + CMake build
- CUDA preprocessing
- CPU post-processing and optional GPU post-processing path for detection
- Custom TensorRT plugin support through `myplugins.dll`
- C++ runtime API wrappers
  - `Yolo11Detector`
  - `Yolo11ObbDetector`
- Image inference demos
- Detection video / camera inference demo
- OBB image inference demo

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
│   ├── yolo11_detector_api.cpp
│   └── yolo11_obb_api.cpp
├── examples
│   ├── demo_image.cpp
│   ├── demo_video.cpp
│   └── demo_obb_image.cpp
├── include
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
│   ├── preprocess.cu
│   ├── postprocess.cu
│   └── ...
├── images
├── gen_wts.py
├── yolo11_det.cpp
├── yolo11_obb.cpp
├── CMakeLists.txt
└── README.md
```

## Support

| Task | Original Command-Line Demo | C++ API Wrapper | Demo Program | Status |
|---|---|---|---|---|
| Detection | `yolo11_det.exe` | `Yolo11Detector` | `demo_image.exe`, `demo_video.exe` | Supported |
| OBB | `yolo11_obb.exe` | `Yolo11ObbDetector` | `demo_obb_image.exe` | Supported, CPU post-processing verified |
| Classification | Source code available | Planned | Planned | Not wrapped yet |
| Segmentation | Source code available | Planned | Planned | Not wrapped yet |
| Pose | Source code available | Planned | Planned | Not wrapped yet |

For OBB, CPU post-processing is the recommended validation path. The OBB GPU post-processing path is reserved for further completion and testing.

## Config

Main config file:

```text
include/config.h
```

For detection models trained on COCO, use:

```cpp
const static int kNumClass = 80;
```

For official YOLO11 OBB models trained on DOTA, use:

```cpp
const static int kNumClass = 16;
```

For a custom one-class model, use:

```cpp
const static int kNumClass = 1;
```

After modifying `config.h`, rebuild the project and regenerate the TensorRT engine.

Do not reuse an old `.engine` file after changing class number, model structure, TensorRT version, CUDA version, or GPU architecture.

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
```

Required runtime file:

```text
myplugins.dll
```

`myplugins.dll` must be placed in the same directory as the executable files. The provided CMake configuration copies it automatically after build.

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

A successful run prints messages similar to:

```text
myplugins.dll loaded successfully.
TensorRT plugins initialized.
Yolo11ObbDetector initialized successfully.
kObbInputW = 1024, kObbInputH = 1024
kNumClass = 16
Detected OBB objects: 1
Result saved to: a_obb_result.jpg
```

## Minimal Commands

### Detection

```bat
cd /d D:\tensorrtx\yolo11

python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
copy /Y yolo11n.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug

yolo11_det.exe -s yolo11n.wts yolo11n.engine n
yolo11_det.exe -d yolo11n.engine D:\tensorrtx\yolo11\images c

demo_image.exe yolo11n.engine D:\tensorrtx\yolo11\images\a.jpg det_result.jpg
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

### `read xxx.engine error!`

The engine file does not exist in the current executable directory, or the engine name is wrong. Use `dir *.engine` to check existing engine files.

### `Detected OBB objects: 0`

Common causes:

- `kNumClass` is still `80`, but the OBB model expects `16` classes.
- A detection engine is used with the OBB demo.
- The test image does not match the OBB model domain.
- Confidence threshold is too high.

For debugging, you can lower the threshold in `include/config.h`:

```cpp
const static float kConfThresh = 0.25f;
```

Then rebuild and regenerate the engine.

### `vector subscript out of range` when using `g`

Use CPU post-processing for OBB validation:

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

### `Failed to load myplugins.dll`

Make sure `myplugins.dll` is in the same directory as the executable. The CMake build copies it automatically, but manual copying may be needed if the executable is moved.

### `gen_wts.py` does not accept `-t obb`

Update `gen_wts.py` so the type choices include `obb`:

```python
choices=['detect', 'cls', 'seg', 'pose', 'obb']
```

And make sure OBB is included in the YOLO-head branch:

```python
if m_type in ['detect', 'seg', 'pose', 'obb']:
```

## Git Ignore

Do not upload generated files to GitHub:

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
```

## Acknowledgement

Modified from `tensorrtx/yolo11`.
