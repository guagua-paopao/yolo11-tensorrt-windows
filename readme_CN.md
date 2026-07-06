# YOLO11 TensorRT Windows

本项目用于在 Windows 平台部署 YOLO11 TensorRT 推理，支持 Visual Studio 2019、CUDA、TensorRT 10、OpenCV、可复用 C++ Runtime API、纯 C++ HTTP 图片检测服务，以及当前已经跑通的 Phase 2 Redis Stream 异步图片检测队列。

这个 README 中文版主要用于记录项目演进过程、踩坑原因、修改逻辑和阶段性反思。

## 当前阶段结论

当前项目已经完成：

```text
Phase 2：纯 C++ HTTP 图片检测服务 + Redis Stream 异步任务队列
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
- `POST /api/v1/detect/image/async` Redis 异步图片检测任务提交
- `GET /api/v1/result/{task_id}` 异步任务状态与结果查询
- Redis Stream 任务入队、Worker 消费、结果写回 Redis
- Redis 中保存 `status`、`meta`、`result` 三类任务信息
- JSON 返回原图像素坐标 bbox
- JSON 返回 `class_name`
- 普通模式不返回调试坐标
- `?debug=true` 时返回 `raw_model_bbox`

当前还没有做：

- Server / Worker 拆分为两个独立可执行程序
- 多 Worker 推理池
- OBB HTTP 服务化接口
- 视频文件异步检测接口
- RTSP / 摄像头流服务化接口
- 多 GPU 调度

当前开发原则是：**先稳定 detect 图片服务与 Redis 异步队列，再拆分 Server / Worker，最后扩展多 Worker、多 GPU、OBB/Seg/Pose/Video。**

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
  - Redis Stream 异步队列
  - 本地 Worker 线程消费任务
  - Redis 保存任务状态、元信息和结果 JSON

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
| Redis | WSL Ubuntu Redis 8.2.1 |
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
- hiredis：C++ 连接 Redis，支持 Redis Stream 异步任务队列

通过 vcpkg 安装：

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
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
│   │   ├── redis_task_queue.h
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
│   │   ├── redis_task_queue.cpp
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
| Detection | `yolo11_det.exe` | `Yolo11Detector` | `demo_image.exe`, `demo_video.exe` | 同步 + Redis 异步 | 已支持 |
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
  input_dir: "./temp/input"
  output_dir: "./output"
  jpeg_quality: 90

redis:
  enabled: true
  host: "172.19.196.109"
  port: 6379
  password: ""
  db: 0
  stream_key: "yolo:stream:detect"
  consumer_group: "yolo11_group"
  consumer_name: "worker_1"
  block_ms: 500
  ttl_seconds: 1800
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
find_package(hiredis CONFIG REQUIRED)

set(YOLO11_HIREDIS_TARGET hiredis::hiredis)

target_link_libraries(yolo11_server PRIVATE
    yolo11_runtime
    myplugins
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    Crow::Crow
    ${YOLO11_HIREDIS_TARGET}
)

if(WIN32)
    target_link_libraries(yolo11_server PRIVATE ws2_32)
endif()
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
Phase 2：同步 HTTP 图片检测服务 + Redis Stream 异步任务队列
```

当前支持接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| GET | `/api/v1/health` | 健康检查 |
| POST | `/api/v1/detect/image` | 同步图片检测 |
| POST | `/api/v1/detect/image?debug=true` | 同步图片检测，并返回模型原始 bbox |
| POST | `/api/v1/detect/image/async` | 提交异步图片检测任务，返回 `task_id` |
| GET | `/api/v1/result/{task_id}` | 查询异步任务状态和检测结果 |
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
    "phase": "phase2_redis_stream_queue",
    "model_type": "detect",
    "queue_backend": "redis_stream",
    "redis_ping": "ok",
    "async_worker": "running"
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


### 异步图片检测

提交异步任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

正常返回示例：

```json
{
    "queue_backend": "redis_stream",
    "result_url": "/api/v1/result/20260705_214026_1",
    "status": "queued",
    "success": true,
    "task_id": "20260705_214026_1"
}
```

查询异步结果：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260705_214026_1"
```

任务完成后返回中的关键字段：

```json
{
    "success": true,
    "status": "done",
    "queue_backend": "redis_stream",
    "num_detections": 3,
    "result_image_url": "/api/v1/image/20260705_214026_1_result.jpg"
}
```

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


## Phase 2 Redis Stream 异步图片检测队列

当前服务阶段已经从：

```text
Phase 1.5：同步 HTTP 图片检测服务
```

升级为：

```text
Phase 2：HTTP 图片检测服务 + Redis Stream 异步任务队列
```

这一阶段的核心目标不是单纯“能检测”，而是把一次 HTTP 请求变成可排队、可查询、可追踪的任务。

### 当前 Phase 2 架构

目前仍然是一个可执行程序：

```text
yolo11_server.exe
├── Crow HTTP Server
├── Yaml 配置读取
├── RedisTaskQueue
├── 本地 Async Worker Thread
├── Yolo11Detector TensorRT 推理
├── 结果图保存
└── Redis 结果写回
```

也就是说，HTTP Server 和 Worker 目前还在同一个进程中，只是通过 Redis Stream 完成了任务队列化。下一阶段才会拆成独立的 `yolo11_server.exe` 和 `yolo11_worker.exe`。

### 异步检测数据流

```text
Client 上传图片
↓
POST /api/v1/detect/image/async
↓
HTTP Server 生成 task_id
↓
保存原图到 ./temp/input
↓
写入 Redis Stream: yolo:stream:detect
↓
Worker 使用 XREADGROUP 消费任务
↓
更新状态 queued -> running
↓
调用 Yolo11Detector 推理
↓
保存结果图到 ./output
↓
写入 Redis result/meta/status
↓
Client 通过 GET /api/v1/result/{task_id} 查询结果
```

这一步的意义是：客户端不需要一直阻塞等待推理完成，后端也具备了后续扩展多 Worker、多进程、多机器的基础。

### Phase 2 新增接口

| 方法 | 接口 | 说明 |
|---|---|---|
| POST | `/api/v1/detect/image/async` | 上传图片并提交异步检测任务，立即返回 `task_id` |
| GET | `/api/v1/result/{task_id}` | 查询任务状态和检测结果 |
| GET | `/api/v1/image/{filename}` | 读取同步或异步生成的结果图 |
| GET | `/api/v1/health` | 健康检查，新增 Redis 状态字段 |

健康检查中 Redis 正常时应看到：

```json
{
    "async_worker": "running",
    "phase": "phase2_redis_stream_queue",
    "queue_backend": "redis_stream",
    "redis_enabled": true,
    "redis_ping": "ok",
    "redis_stream_key": "yolo:stream:detect",
    "success": true
}
```

### Redis Key 设计

| Redis Key | 类型 | 作用 |
|---|---|---|
| `yolo:stream:detect` | Stream | 异步检测任务队列 |
| `yolo:task:{task_id}:status` | String | 保存任务状态：`queued` / `running` / `done` / `failed` |
| `yolo:task:{task_id}:meta` | Hash | 保存任务路径、时间戳、错误信息、结果图路径等元信息 |
| `yolo:task:{task_id}:result` | String | 保存最终检测 JSON |

任务状态机：

```text
queued -> running -> done
queued -> running -> failed
```

这个状态机是异步服务的核心。后续拆分 Worker、多 Worker 并发、任务失败重试，都要围绕这套状态设计继续扩展。

### Phase 2 运行命令

启动 Redis，当前使用 WSL Ubuntu 中的 Redis 8.2.1：

```bash
sudo service redis-server start
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

Windows 侧确认能连到 WSL Redis：

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

启动服务：

```powershell
cd D:\tensorrtx\yolo11\out\build\x64-Debug
.\yolo11_server.exe D:\tensorrtx\yolo11\config\server.yaml
```

健康检查：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
```

提交异步任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

查询任务结果：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260705_214026_1"
```

下载结果图：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/image/20260705_214026_1_result.jpg" -o redis_async_result.jpg
start .\redis_async_result.jpg
```

连续提交 5 个异步任务：

```powershell
for ($i=1; $i -le 5; $i++) {
  curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
    -H "Content-Type: image/png" `
    --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
}
```

### Redis 验收命令

进入 Redis：

```bash
redis-cli -h 172.19.196.109 -p 6379
```

查看项目相关 key：

```redis
KEYS yolo:*
```

查看 Stream 任务记录：

```redis
XRANGE yolo:stream:detect - +
```

查看任务状态：

```redis
GET yolo:task:20260705_214026_1:status
```

查看检测结果：

```redis
GET yolo:task:20260705_214026_1:result
```

查看任务元信息：

```redis
HGETALL yolo:task:20260705_214026_1:meta
```

查看消费组：

```redis
XINFO GROUPS yolo:stream:detect
XINFO CONSUMERS yolo:stream:detect yolo11_group
```

本阶段实际验收结果中，Redis 已经能看到：

```text
yolo:stream:detect
yolo:task:20260705_214026_1:status = done
yolo:task:20260705_214026_1:result = 检测 JSON
yolo:task:20260705_214026_1:meta = input/result 路径和时间戳
```

并且 `POST /api/v1/detect/image/async` 可以连续提交多个任务，Worker 能正常消费并完成推理。

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


### 8. Redis Stream 不是简单的缓存，而是任务队列

最初对 Redis 的理解容易停留在 key-value 缓存，但这次使用的是 Redis Stream。

这意味着 Redis 不只是存图片或结果，而是在承担“消息队列”的角色：

```text
XADD 写入任务
XGROUP 创建消费组
XREADGROUP 消费任务
XACK 确认任务处理完成
```

反思：**Redis Stream 的价值在于任务可排队、可消费、可追踪；它是从同步服务走向异步服务的关键过渡。**

### 9. Windows Redis 和 WSL Redis 同时存在会造成严重混淆

本阶段最典型的问题是 Windows 本机已经有一个 Redis：

```text
127.0.0.1:6379 -> D:\redis\redis-server.exe
```

而真正想使用的是 WSL 中的 Redis：

```text
172.19.196.109:6379 -> /usr/bin/redis-server, Redis 8.2.1
```

如果 `server.yaml` 写成：

```yaml
host: "127.0.0.1"
port: 6379
```

程序会连到 Windows Redis，而不是 WSL Redis，导致出现：

```text
ERR unknown command 'XGROUP'
```

最终通过以下命令确认了两个 Redis 的区别：

```powershell
netstat -ano | findstr :6379
Get-Process -Id <PID>
```

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

反思：**部署时不能只说“Redis 在 6379”，必须明确是哪个操作系统、哪个 IP、哪个进程、哪个版本。**

### 10. hiredis 在 Windows 下需要 WinSock

hiredis 在 Windows 编译时可能出现 `timeval` 未定义等问题，原因是 Windows 网络类型来自 WinSock。

解决方式是在 `redis_task_queue.cpp` 中保证 WinSock 头文件先于 hiredis 引入：

```cpp
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <hiredis/hiredis.h>
```

同时 CMake 需要链接：

```cmake
if(WIN32)
    target_link_libraries(yolo11_server PRIVATE ws2_32)
endif()
```

反思：**跨平台库在 Windows 上经常不是“只 include 一个头文件”这么简单，系统网络库也必须正确链接。**

### 11. Redis 超时问题不一定是代码错，也可能是连错服务或服务刚重启

本阶段遇到过：

```text
failed to connect Redis: timed out
failed to connect Redis: connection refused
```

最后排查发现，原因可能包括：

- WSL Redis 没启动
- WSL IP 改变
- Windows Redis 占用了 localhost 6379
- C++ 程序连错 Redis
- Redis 正在重启

正确排查顺序应该是：

```bash
redis-cli ping
hostname -I
ss -lntp | grep 6379
```

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

反思：**网络问题不要一上来改代码，先确认服务、端口、IP、版本和进程。**

### 12. Phase 2 的核心不是“又加了一个接口”，而是服务形态改变了

同步接口：

```text
Client 等待 HTTP 请求完成
```

异步接口：

```text
Client 先拿 task_id，之后查询结果
```

这意味着系统从“单次调用”开始向“任务系统”转变。后面拆分 Worker、多 GPU、多进程、多机器，本质上都是围绕这个任务系统继续扩展。

反思：**异步队列是服务化部署的分水岭，它让模型推理从函数调用变成可调度任务。**

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

curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"

curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>"
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


### `ERR unknown command 'XGROUP'`

说明当前连接到的 Redis 不支持 Redis Stream 消费组，或者程序连到了错误的 Redis。

本项目中曾经出现过 Windows Redis 和 WSL Redis 同时存在的问题：

```text
127.0.0.1:6379        -> Windows Redis，D:\redis\redis-server.exe
172.19.196.109:6379   -> WSL Redis，Redis 8.2.1
```

排查命令：

```powershell
netstat -ano | findstr :6379
Get-Process -Id <PID>
```

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

### `failed to connect Redis: timed out` 或 `connection refused`

先确认 WSL Redis 是否正常：

```bash
sudo service redis-server status
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

再从 Windows 测试：

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

如果 WSL IP 变了，需要同步修改 `config/server.yaml`。

### Windows 本机 Redis 干扰

如果 Windows 本机 Redis 不需要，可以禁用：

```powershell
Get-Service | Where-Object { $_.Name -match "redis" -or $_.DisplayName -match "redis" }
Stop-Service Redis
Set-Service Redis -StartupType Disabled
```

### hiredis 在 Windows 下编译问题

hiredis 需要安装：

```bat
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
```

CMake 中使用：

```cmake
find_package(hiredis CONFIG REQUIRED)
target_link_libraries(yolo11_server PRIVATE hiredis::hiredis ws2_32)
```

如果出现 `timeval` 未定义，检查 `redis_task_queue.cpp` 中 WinSock 头文件是否在 `hiredis.h` 之前。

## Git Ignore

以下生成文件不建议上传到 GitHub：

```text
out/
build/
.vs/
output/
temp/
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

当前已经完成：

```text
Phase 2：Redis Stream 异步图片检测任务队列
```

下一步建议进入：

```text
Phase 3：拆分 HTTP Server 和 Inference Worker
```

目标架构：

```text
yolo11_server.exe
├── 只负责 HTTP 请求
├── 保存上传图片
├── 写入 Redis Stream
└── 查询 Redis 结果

yolo11_worker.exe
├── 连接 Redis Stream
├── 消费任务
├── TensorRT 推理
├── 保存结果图
└── 写回 Redis status/result/meta
```

Phase 3 做完后，系统就可以进一步扩展：

- 多 worker 进程
- 多 GPU 调度
- 单独部署 server 和 worker
- 后续接对象存储 / 数据库
- 更接近生产环境的推理服务架构

---

## 致谢

Modified from `tensorrtx/yolo11`.
