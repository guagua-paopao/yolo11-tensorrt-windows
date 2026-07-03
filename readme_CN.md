# YOLO11 TensorRT Windows

本项目用于在 Windows 平台部署 YOLO11 TensorRT 推理，支持 Visual Studio 2019、CUDA、TensorRT 10、OpenCV、可复用 C++ Runtime API，以及当前已经跑通的 Phase 1.5 纯 C++ HTTP 图片检测服务。

这个 README 中文版主要用于记录项目演进过程、踩坑原因、修改逻辑和阶段性反思。

## 当前阶段结论

当前项目已经完成：

```text
Phase 1.5：纯 C++ HTTP 同步图片检测服务
```

已经验证通过的能力：

- 原始 `yolo11_det.exe` Detection 命令行推理
- 原始 `yolo11_obb.exe` OBB 命令行推理
- `Yolo11Detector` C++ API 封装
- `Yolo11ObbDetector` C++ API 封装
- `demo_image.exe` 图片检测
- `demo_video.exe` 视频 / 摄像头检测
- `demo_obb_image.exe` OBB 图片检测
- `yolo11_server.exe` 常驻 HTTP 服务
- `GET /api/v1/health` 健康检查
- `POST /api/v1/detect/image` 同步图片检测
- `GET /api/v1/image/<filename>` 结果图访问
- JSON 返回原图像素坐标 bbox
- JSON 返回 `class_name`
- 普通模式不返回调试坐标
- `?debug=true` 时返回 `raw_model_bbox`

当前还没有做：

- Redis 异步任务队列
- 多 Worker 推理池
- OBB HTTP 服务化接口
- 视频文件异步检测接口
- RTSP / 摄像头流服务化接口
- 多 GPU 调度

当前开发原则是：**先稳定 detect 图片服务，再扩展 Redis、异步任务、多线程 Worker、OBB/Seg/Pose/Video。**

---

## 功能特点

- 支持 YOLO11 Detection 模型在 Windows 上进行 TensorRT 部署
- 支持 YOLO11 OBB 旋转框模型在 Windows 上进行 TensorRT 部署
- 支持 TensorRT 10 API
- 支持 Visual Studio 2019 + CMake 编译
- 支持 CUDA 图像预处理
- Detection 支持 CPU 后处理和可选 GPU 后处理路径
- 通过 `myplugins.dll` 支持自定义 TensorRT 插件
- 提供 C++ Runtime API 封装
  - `Yolo11Detector`
  - `Yolo11ObbDetector`
- 保留原始命令行 demo，不破坏原项目验证路径
- 新增纯 C++ HTTP Server 模块
  - `yolo11_server.exe`
  - Crow HTTP 框架
  - YAML 配置读取
  - JSON 返回检测结果
  - 结果图保存与 HTTP 访问

---

## 测试环境

| 依赖 | 版本 / 路径 |
|---|---|
| 操作系统 | Windows |
| IDE | Visual Studio 2019 |
| CUDA | `D:\GPU13.3` |
| cuDNN | `D:\cuDNN9\bin\13.3\x64` |
| TensorRT | `D:\TensorRT-10.16.1.11` |
| OpenCV | `D:\libs\opencv\build` |
| vcpkg | `D:\vcpkg` |
| Python | 3.12 |
| GPU | RTX 4080 Laptop GPU |
| CUDA 架构 | `sm_89` |

运行前需要确保以下目录在系统 `Path` 或当前终端 `PATH` 中：

```text
D:\GPU13.3\bin
D:\cuDNN9\bin\13.3\x64
D:\TensorRT-10.16.1.11\lib
D:\libs\opencv\build\x64\vc16\bin
```

PowerShell 临时设置示例：

```powershell
$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"
```

---

## HTTP Server 第三方依赖

为了支持纯 C++ HTTP 服务，当前新增了三个主要依赖：

- Crow：HTTP Server
- nlohmann/json：JSON 生成
- yaml-cpp：读取 `config/server.yaml`
- asio：Crow 依赖，安装 Crow 时会自动处理

通过 vcpkg 安装：

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
```

如果手动配置 CMake，需要带上 vcpkg toolchain：

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

Visual Studio 的 CMake 配置也需要确保使用同一个 toolchain，否则会出现找不到：

```text
crow.h
nlohmann/json.hpp
yaml-cpp/yaml.h
```

---

## 项目结构

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

注意：

- `src/server/*.cpp` 是服务化新增代码
- `api/*.cpp` 是 C++ Runtime API
- `examples/*.cpp` 是 API 使用示例
- `yolo11_det.cpp` / `yolo11_obb.cpp` 是原始命令行程序
- `output/` 是运行时输出目录，不应该提交
- `engine/` 或 `engines/` 目录里的 `.engine` 不应该提交

---

## 支持情况

| 任务 | 原始命令行程序 | C++ API 封装 | Demo 程序 | HTTP 服务 | 状态 |
|---|---|---|---|---|---|
| Detection | `yolo11_det.exe` | `Yolo11Detector` | `demo_image.exe`, `demo_video.exe` | `yolo11_server.exe` | 已支持 |
| OBB | `yolo11_obb.exe` | `Yolo11ObbDetector` | `demo_obb_image.exe` | 后续计划 | API 和 CPU demo 已验证 |
| Classification | 源码已有 | 计划封装 | 计划添加 | 后续计划 | 暂未封装 |
| Segmentation | 源码已有 | 计划封装 | 计划添加 | 后续计划 | 暂未封装 |
| Pose | 源码已有 | 计划封装 | 计划添加 | 后续计划 | 暂未封装 |

OBB 当前推荐优先使用 CPU 后处理路径进行验证。OBB 的 GPU 后处理路径保留为后续完善和测试方向。

---

## 配置说明

### 模型配置

主要模型配置文件：

```text
include/config.h
```

如果使用 COCO Detection 模型：

```cpp
const static int kNumClass = 80;
```

如果使用官方 YOLO11 OBB / DOTA 模型：

```cpp
const static int kNumClass = 16;
```

如果使用自定义单类别模型：

```cpp
const static int kNumClass = 1;
```

修改 `config.h` 后，必须重新编译项目，并重新生成 TensorRT engine。

如果修改了类别数、模型结构、TensorRT 版本、CUDA 版本或 GPU 架构，不要继续复用旧的 `.engine` 文件。

### 服务配置

HTTP 服务配置文件：

```text
config/server.yaml
```

推荐写法：

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

这里建议 `engine_path` 使用绝对路径，避免相对路径混乱。
---

## 编译方法

使用 Visual Studio 2019 打开项目文件夹：

```text
File -> Open -> Folder
```

打开项目目录，例如：

```text
D:\tensorrtx\yolo11
```

如果已经存在旧的 CMake 缓存，建议删除：

```text
out/
```

然后重新配置并编译目标。

可执行文件通常生成在：

```text
out\build\x64-Debug
```

常用编译目标：

```text
myplugins
yolo11_det
yolo11_obb
demo_image
demo_video
demo_obb_image
yolo11_server
```

运行时必须有：

```text
myplugins.dll
```

`myplugins.dll` 必须和可执行文件位于同一目录。当前 CMake 配置会在编译后自动复制该 DLL。

---

## CMake 关键注意事项

新增 server 后，最容易出问题的地方不是 CUDA 或 TensorRT，而是 CMake target 划分。

错误方式：

```cmake
file(GLOB_RECURSE SRCS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.cu
)
```

然后把 `${SRCS}` 同时加入：

```text
yolo11_det
yolo11_obb
yolo11_runtime
yolo11_server
```

这样会导致 `src/server/*.cpp` 被编进原来的 `yolo11_det`、`yolo11_obb`，产生莫名其妙的编译错误。

正确做法是排除 server 目录：

```cmake
file(GLOB_RECURSE SRCS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.cu
)

list(FILTER SRCS EXCLUDE REGEX ".*src[/\\\\]server[/\\\\].*")
```

然后 server 单独编译：

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

这一点是服务化改造的第一个重要经验：**不要破坏原始 demo；新增 server 模块必须和原始命令行 target 隔离。**

---

## 下载 YOLO11 模型

示例脚本：

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

`yolo11n.pt` 用于普通检测，`yolo11n-obb.pt` 用于 OBB 旋转框检测。

---

## `.pt` 转 `.wts`

进入项目目录：

```bat
cd /d D:\tensorrtx\yolo11
```

Detection 模型：

```bat
python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
```

OBB 模型：

```bat
python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
```

自定义 Detection 模型：

```bat
python gen_wts.py -w weights/best.pt -o best.wts -t detect
```

自定义 OBB 模型：

```bat
python gen_wts.py -w weights/best.pt -o best-obb.wts -t obb
```

如果出现 `C3k2` 相关错误，请更新 Ultralytics：

```bat
python -m pip install -U ultralytics
```

---

## 生成 TensorRT Engine

将 `.wts` 文件复制到可执行文件目录：

```bat
copy /Y yolo11n.wts out\build\x64-Debug\
copy /Y yolo11n-obb.wts out\build\x64-Debug\
```

进入可执行文件目录：

```bat
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
```

生成 Detection engine：

```bat
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
```

生成 OBB engine：

```bat
yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
```

模型规模参数：

| YOLO11 模型 | 参数 |
|---|---|
| YOLO11n | `n` |
| YOLO11s | `s` |
| YOLO11m | `m` |
| YOLO11l | `l` |
| YOLO11x | `x` |

如果自定义模型是基于 YOLO11n 训练的，最后一个参数使用 `n`。

---

## 原始命令行推理

### Detection

对图片文件夹进行检测推理：

```bat
yolo11_det.exe -d yolo11n.engine D:\tensorrtx\yolo11\images c
```

最后一个参数表示后处理模式：

| 参数 | 含义 |
|---|---|
| `c` | CPU 后处理 |
| `g` | GPU 后处理 |

结果会保存到当前可执行文件目录。

### OBB

对图片文件夹进行 OBB 推理：

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

建议先使用 `c` 验证 OBB 推理流程。OBB 测试图片应尽量匹配模型领域，例如 DOTA 风格的航拍图像。

---

## C++ Detection API

头文件：

```cpp
#include "yolo11_detector_api.h"
```

调用示例：

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

运行图片 demo：

```bat
demo_image.exe yolo11n.engine D:\tensorrtx\yolo11\images\a.jpg det_result.jpg
```

运行视频 demo：

```bat
demo_video.exe yolo11n.engine D:\tensorrtx\yolo11\test.mp4 result_video.mp4
```

运行摄像头 demo：

```bat
demo_video.exe yolo11n.engine 0
```

---

## C++ OBB API

头文件：

```cpp
#include "yolo11_obb_api.h"
```

调用示例：

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

运行 OBB 图片 demo：

```bat
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

最后一个参数表示 OBB demo 的后处理模式：

| 参数 | 含义 |
|---|---|
| `cpu` | CPU OBB 后处理 |
| `gpu` | GPU OBB 后处理路径，实验性 |

推荐验证命令：

```bat
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

---

## 纯 C++ HTTP Detection Server

当前服务阶段：

```text
Phase 1.5：同步 HTTP 图片检测服务
```

当前支持接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| GET | `/api/v1/health` | 健康检查 |
| POST | `/api/v1/detect/image` | 同步图片检测 |
| POST | `/api/v1/detect/image?debug=true` | 同步图片检测，并返回模型原始 bbox |
| GET | `/api/v1/image/<filename>` | 读取保存后的结果图 |

### 编译 server

```powershell
cd D:\tensorrtx\yolo11
cmake --build out\build\x64-Debug --config Debug --target yolo11_server
```

### 启动 server

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml
```

如果找不到 exe，可以先搜索：

```powershell
Get-ChildItem -Recurse -Filter yolo11_server.exe | Select-Object FullName
```

### 健康检查

```powershell
curl.exe http://127.0.0.1:8080/api/v1/health
```

正常返回类似：

```json
{
    "success": true,
    "status": "ok",
    "service": "yolo11_server",
    "phase": "phase1_sync_http",
    "model_type": "detect"
}
```

### 图片检测

PNG 图片：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

JPEG 图片：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/jpeg" --data-binary "@D:/tensorrtx/yolo11/images/bus.jpg"
```

正常返回示例：

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

这里的 `bbox` 是**原始输入图片像素坐标**，不是模型输入坐标，也不是 640×640/letterbox 坐标。

### Debug 模式

调试请求：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image?debug=true" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

会额外返回：

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

`raw_model_bbox` 是模型原始输出坐标，用于排查坐标映射问题；正式前端应该只使用 `bbox` 字段。

### 访问结果图

浏览器打开：

```text
http://127.0.0.1:8080/api/v1/image/result_xxx.jpg
```

或者下载：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/image/result_xxx.jpg" --output test_result.jpg
start .\test_result.jpg
```

---

## 关键问题记录与反思

### 1. 第三方库安装成功后，下一步不是继续装库，而是接入 CMake

已安装：

```text
nlohmann-json
yaml-cpp
crow
asio
```

但仅仅安装成功不代表项目能找到它们。必须在 CMake 中：

```cmake
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)
```

并且 CMake 配置时要使用 vcpkg toolchain。

反思：**依赖安装只是第一步，CMake target 能正确链接才是关键。**

### 2. server 源码不能混进原始 yolo11_det / yolo11_obb

最初的问题之一是 `src/server/*.cpp` 被编进了多个 target。这样原始命令行程序也会尝试编译 HTTP Server 代码，导致一堆看似无关的错误。

反思：**服务化扩展应该新增模块，而不是污染原 demo。**

### 3. PowerShell 运行 exe 必须加 `.\`

错误写法：

```powershell
build\Release\yolo11_server.exe
```

正确写法：

```powershell
.\out\build\x64-Debug\yolo11_server.exe
```

如果不知道 exe 在哪里：

```powershell
Get-ChildItem -Recurse -Filter yolo11_server.exe | Select-Object FullName
```

反思：**Windows 下 CMD 和 PowerShell 对可执行文件路径的处理方式不同。**

### 4. engine 路径错误是最常见运行错误

曾经配置写的是：

```yaml
engine_path: "./engines/yolo11n.engine"
```

但真实路径是：

```text
D:\tensorrtx\yolo11\engine\yolo11n.engine
```

导致：

```text
Cannot open engine file
```

反思：**服务配置阶段优先使用绝对路径，避免相对路径歧义。**

### 5. bbox 坐标不一致问题的根因

最初 HTTP JSON 直接读取了：

```cpp
detection.bbox[0]
detection.bbox[1]
detection.bbox[2]
detection.bbox[3]
```

并误以为它是原图坐标下的 `x, y, w, h`。

但实际画图时用的是：

```cpp
cv::Rect r = get_rect(img, res[j].bbox);
```

`get_rect()` 会把模型输出坐标从 letterbox / 模型输入坐标系还原到原图坐标系。

所以正确做法是：**JSON 序列化时也复用 `get_rect()`，保证 JSON 坐标和结果图画框一致。**

反思：**后端接口不能只看字段名，要确认字段处于哪个坐标系。**

### 6. 为什么要加 `class_name`

仅返回：

```json
"class_id": 0
```

对调用者不直观。

现在返回：

```json
"class_id": 0,
"class_name": "person"
```

前端和日志都更容易理解。

反思：**接口不是只给模型看，而是给人和外部系统看。**

### 7. 为什么 `raw_model_bbox` 只在 debug=true 时返回

`raw_model_bbox` 对调试有用，但对正式前端容易造成混淆，因为它不是原图坐标。

所以普通接口不返回：

```json
"raw_model_bbox": ...
```

只有调试请求：

```text
/api/v1/detect/image?debug=true
```

才返回。

反思：**正式接口要干净，调试信息要可开关。**

---

## 最小运行命令

### Detection 命令行和 API demo

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

使用官方 DOTA OBB 模型前，先在 `include/config.h` 中设置 `kNumClass = 16`，然后重新编译项目。

```bat
cd /d D:\tensorrtx\yolo11

python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
copy /Y yolo11n-obb.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug

yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c

demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

---

## 常见问题

### `Cannot open engine file`

检查 `config/server.yaml`：

```yaml
engine_path: "D:/tensorrtx/yolo11/engine/yolo11n.engine"
```

### PowerShell 无法识别 exe

PowerShell 运行相对路径 exe 时需要加：

```powershell
.\out\build\x64-Debug\yolo11_server.exe
```

如果不知道文件在哪里：

```powershell
Get-ChildItem -Recurse -Filter yolo11_server.exe | Select-Object FullName
```

### `Failed to load myplugins.dll`

确认 `myplugins.dll` 和当前运行的 `.exe` 位于同一目录。CMake 默认会自动复制，如果移动了可执行文件，则需要手动复制。

### `read xxx.engine error!`

说明当前可执行目录下没有该 engine 文件，或者 engine 文件名写错。可以用下面命令检查：

```bat
dir *.engine
```

### `Detected OBB objects: 0`

常见原因：

- `kNumClass` 仍然是 `80`，但 OBB 模型需要 `16` 类
- 使用了普通 Detection engine，却运行 OBB demo
- 测试图片不符合 OBB 模型领域
- 置信度阈值设置过高

调试时可以临时降低 `include/config.h` 中的置信度阈值：

```cpp
const static float kConfThresh = 0.25f;
```

然后重新编译并重新生成 engine。

### 使用 `g` 时出现 `vector subscript out of range`

OBB 验证阶段请先使用 CPU 后处理：

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

### `gen_wts.py` 不接受 `-t obb`

请修改 `gen_wts.py`，确保类型选项包含 `obb`：

```python
choices=['detect', 'cls', 'seg', 'pose', 'obb']
```

并确保 YOLO head 分支包含 OBB：

```python
if m_type in ['detect', 'seg', 'pose', 'obb']:
```

### JSON bbox 和结果图不一致

检查 `src/server/http_controller.cpp` 中 Detection -> JSON 的逻辑，必须使用与画图一致的坐标还原方式，即复用 `get_rect()`。

---

## Git Ignore

以下生成文件不建议上传到 GitHub：

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
---

## 下一阶段路线

下一步建议进入：

```text
Phase 2：Redis 异步图片检测任务队列
```

目标接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| POST | `/api/v1/detect/image/async` | 上传图片，返回 `task_id` |
| GET | `/api/v1/result/{task_id}` | 查询 queued/running/done/failed 和检测结果 |
| GET | `/api/v1/image/{filename}` | 读取结果图 |

目标流程：

```text
Client 上传图片
↓
HTTP Server 生成 task_id
↓
保存原图 bytes 到 Redis
↓
写入 Redis Stream 任务队列
↓
Worker 读取任务
↓
调用 Yolo11Detector 推理
↓
保存 result JSON 和 result image
↓
Client 根据 task_id 查询结果
```
---

## 致谢

Modified from `tensorrtx/yolo11`.
