# YOLO11 TensorRT Windows

本项目用于在 Windows 平台部署 YOLO11 TensorRT 推理，支持 Visual Studio 2019、CUDA、TensorRT 10、OpenCV、可复用 C++ Runtime API、纯 C++ HTTP 图片检测服务，以及当前已经跑通的 Phase 8 / Phase 8.5 健康检查增强、Worker 心跳、Redis 图片 TTL 与内存控制、Worker 离线拒绝入队、spdlog 日志系统、labels_path 标签外置和 metrics 指标接口。前序 Phase 7 已完成 Server / Worker 独立进程拆分、Redis Binary 图片存储、Worker 崩溃恢复与 Debug 弹窗修复；Phase 6 已完成 Redis 连接复用、压测统计、Pending 恢复与 Stream 长度控制。

这个 README 中文版主要用于记录项目演进过程、踩坑原因、修改逻辑和阶段性反思。

## 当前阶段结论

当前项目已经完成：

```text
Phase 8.5：健康检查增强 + Worker 心跳 + Redis 图片 TTL / 内存控制 + spdlog 日志 + labels_path + metrics 指标接口
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
- Redis Stream Consumer Group 多 Worker 推理池
- `worker_num=3` 多线程 TensorRT 推理实例
- 任务性能指标记录：`queue_wait_ms`、`inference_ms`、`total_ms`
- Pending 任务 `XAUTOCLAIM` 恢复机制
- Redis Stream `XTRIM MAXLEN ~` 长度控制
- `tools/benchmark_async.py` 异步压测脚本
- Redis 连接复用：HTTP Producer 与每个 Worker 复用长期 `redisContext`
- Redis 命令失败后自动 reconnect 一次
- Phase 6 极限压测 `3000 tasks / concurrency=20` 全部完成，Failed=0，Timeout=0
- Redis Stream 任务入队、Worker 消费、结果写回 Redis
- Redis 中保存 `status`、`meta`、`result` 三类任务信息
- JSON 返回原图像素坐标 bbox
- JSON 返回 `class_name`
- 普通模式不返回调试坐标
- `?debug=true` 时返回 `raw_model_bbox`
- `yolo11_worker.exe` 独立 Worker 进程
- `yolo11_server.exe` 与 `yolo11_worker.exe` 分离部署
- Redis Binary 图片存储：`yolo:image:{task_id}:input` 与 `yolo:image:{task_id}:result`
- 新增结果图访问接口：`GET /api/v1/result/{task_id}/image`
- Phase 7 标准压测：`1000 tasks / concurrency=10`，`1000/0/0`，QPS=68.261
- Phase 7 极限压测：`3000 tasks / concurrency=20`，`3000/0/0`，QPS=60.948
- Worker 强杀恢复测试：`5000 tasks / concurrency=20`，中途终止一个 Worker，最终 `5000/0/0`
- Debug 版本下 `Ctrl+C` 结束 Worker 的 Visual C++ Runtime abort 弹窗修复
- Phase 8 健康检查增强：新增 `/api/v1/ready` 和 `/api/v1/workers`
- Phase 8 Worker 心跳：`yolo:worker:{consumer_name}:heartbeat`，支持 TTL 自动过期
- Phase 8 Redis 图片 TTL：input image 完成后删除，result image 按 TTL 保存
- Phase 8 Redis 内存保护：超过 `max_redis_used_memory_mb` 时 `/ready=false`
- Phase 8 Worker 离线保护：Worker 不足时异步提交返回 HTTP 503，不再继续入队
- Phase 8 修正任务确认逻辑：`markDone/markFailed` 成功后才 `XACK`
- Phase 8 标准压测：`1000 tasks / concurrency=10`，`1000/0/0`，QPS=60.373
- Phase 8.5 引入 spdlog：`logs/server.log` 和 `logs/worker.log` 独立写入
- Phase 8.5 新增 `labels_path`：`class_name` 从 `labels/coco.txt` 加载，不再依赖硬编码数组
- Phase 8.5 新增 `/api/v1/metrics`：统计累计任务、失败数、近期 QPS、平均耗时和 Worker 分布
- Phase 8.5 标准压测：`1000 tasks / concurrency=10`，`1000/0/0`，QPS=59.177

当前还没有做：

- ImageStorage 抽象层（当前 Redis Binary 仍然是主要实现，后续需要拆出 `RedisImageStorage` / `LocalFileImageStorage`）
- OBB HTTP 服务化接口
- 视频文件异步检测接口
- RTSP / 摄像头流服务化接口
- 多 GPU 调度
- 启停脚本、服务守护、Docker / Linux 部署路径

当前开发原则是：**先把 detect 图片异步服务、Redis Stream、多 Worker、Redis 连接复用、Server/Worker 进程拆分、Redis Binary 图片存储、健康检查、可观测性和异常恢复链路做扎实，再进入 ImageStorage 抽象、OBB/Seg/Pose/Video 扩展。**

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
  - 独立 `yolo11_worker.exe` 消费任务
  - Redis Binary Key 保存输入图和结果图
  - Redis 保存任务状态、元信息和结果 JSON
  - Redis Stream Consumer Group 多 Worker 消费
  - Pending 任务恢复与 `XAUTOCLAIM`
  - Stream 长度控制与 `XTRIM MAXLEN ~`
  - 压测统计与 worker 分布统计
  - Worker 强杀恢复测试
  - Debug 版本 Ctrl+C 优雅退出与弹窗抑制

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
- spdlog：结构化日志，拆分 `server.log` 和 `worker.log`

通过 vcpkg 安装：

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install spdlog:x64-windows --vcpkg-root D:\vcpkg
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
│   ├── server.yaml
│   └── worker.yaml（可选，Worker 独立配置）
├── examples
│   ├── demo_image.cpp
│   ├── demo_video.cpp
│   └── demo_obb_image.cpp
├── include
│   ├── server
│   │   ├── app_config.h
│   │   ├── app_logger.h
│   │   ├── http_controller.h
│   │   ├── image_codec.h
│   │   ├── inference_service.h
│   │   ├── inference_worker.h
│   │   ├── label_map.h
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
│   │   ├── app_logger.cpp
│   │   ├── http_controller.cpp
│   │   ├── image_codec.cpp
│   │   ├── inference_service.cpp
│   │   ├── inference_worker.cpp
│   │   ├── label_map.cpp
│   │   ├── main_server.cpp
│   │   ├── main_worker.cpp
│   │   ├── redis_task_queue.cpp
│   │   └── result_serializer.cpp
│   ├── preprocess.cu
│   ├── postprocess.cpp
│   └── ...
├── labels
│   └── coco.txt
├── images
├── engine
│   └── yolo11n.engine
├── output
├── temp
├── tools
│   └── benchmark_async.py
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
| Detection | `yolo11_det.exe` | `Yolo11Detector` | `demo_image.exe`, `demo_video.exe` | 同步 + Redis 异步 + 多 Worker | 已支持 |
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
  stream_max_len: 10000
  enable_pending_reclaim: true
  pending_min_idle_ms: 60000

worker:
  worker_num: 3
  consumer_name_prefix: "worker_"
  log_task_done: false
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
yolo11_worker
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
yolo11_worker
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
Phase 8.5：Server / Worker 分离 + Redis Binary 图片存储 + 健康检查 + Worker 心跳 + 日志 + labels_path + metrics
```

当前支持接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| GET | `/api/v1/health` | Server 和 Redis 基础健康检查 |
| GET | `/api/v1/ready` | 判断系统是否可接收新异步任务 |
| GET | `/api/v1/workers` | 查看 Worker 心跳和在线状态 |
| GET | `/api/v1/metrics` | 查看累计任务、近期 QPS、平均耗时和 Worker 分布 |
| POST | `/api/v1/detect/image` | 同步图片检测 |
| POST | `/api/v1/detect/image?debug=true` | 同步图片检测，并返回模型原始 bbox |
| POST | `/api/v1/detect/image/async` | 提交异步图片检测任务，返回 `task_id` |
| GET | `/api/v1/result/{task_id}` | 查询异步任务状态和检测结果 |
| GET | `/api/v1/image/<filename>` | 读取保存后的结果图 |

### 编译 server

```powershell
cd D:\tensorrtx\yolo11
cmake --build out\build\x64-Debug --config Debug --target yolo11_server
yolo11_worker
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
    "phase": "phase6_redis_connection_reuse",
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


## Phase 2 Redis Stream 异步图片检测队列（历史记录）

下面内容保留为 Phase 2 的阶段记录，用来回顾异步队列最初接入时的设计和踩坑。

当前服务阶段曾经从：

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

---

## Phase 4 / Phase 5 Redis Stream 多 Worker 推理池与压测验证

### 阶段定位

在 Phase 2 和 Phase 3 中，系统已经具备了 Redis Stream 异步任务队列能力；本阶段进一步完成了多 Worker 推理池、性能指标采集、压测脚本、Pending 恢复和 Redis Stream 长度控制。当前工程已经从“异步任务能跑通”进入到“多 Worker 能稳定消费、性能数据可量化、异常任务可恢复、队列不会无限增长”的阶段。

当前阶段可以概括为：

```text
Phase 4：Redis Stream Consumer Group 多 Worker 推理池
Phase 5：压测统计、Pending 恢复、XTRIM 清理与稳定性验证
```

### 最新架构

当前仍然是一个可执行程序 `yolo11_server.exe`，但内部已经拆分出 HTTP 生产者和多 Worker 消费者：

```text
yolo11_server.exe
├── Crow HTTP Server
│   ├── GET  /api/v1/health
│   ├── POST /api/v1/detect/image
│   ├── POST /api/v1/detect/image/async
│   └── GET  /api/v1/result/{task_id}
├── RedisTaskQueue
│   ├── XADD 提交任务
│   ├── XREADGROUP 消费任务
│   ├── XACK 确认任务
│   ├── XAUTOCLAIM 回收 Pending
│   └── XTRIM 控制 Stream 长度
├── InferenceService
│   └── 管理多个 InferenceWorker
├── InferenceWorker × 3
│   ├── 每个 Worker 独立加载 TensorRT Engine
│   ├── 每个 Worker 使用独立 consumer_name
│   ├── 消费 Redis Stream 任务
│   ├── 执行 YOLO11 TensorRT 推理
│   ├── 保存结果图
│   └── 写回 Redis result/meta/status
└── Yolo11Detector
    └── 同步接口和 Worker 内部推理复用同一套 Runtime API
```

### 核心配置

本阶段推荐配置：

```yaml
server:
  host: "0.0.0.0"
  port: 8080
  threads: 4

model:
  type: "detect"
  engine_path: "D:/tensorrtx/yolo11/engines/yolo11n.engine"
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
  stream_max_len: 10000
  enable_pending_reclaim: true
  pending_min_idle_ms: 60000

worker:
  worker_num: 3
  consumer_name_prefix: "worker_"
  log_task_done: false
```

其中：

| 配置 | 作用 |
|---|---|
| `worker_num: 3` | 启动 3 个 C++ 推理 Worker |
| `consumer_name_prefix: "worker_"` | 自动生成 `worker_1`、`worker_2`、`worker_3` |
| `stream_max_len: 10000` | Redis Stream 近似最大长度 |
| `enable_pending_reclaim: true` | 开启 Pending 任务恢复 |
| `pending_min_idle_ms: 60000` | Pending 任务 idle 超过 60 秒后允许被回收 |
| `log_task_done: false` | 压测时关闭每个任务完成日志，减少 Windows 控制台 I/O 干扰 |

### Redis Stream 核心命令理解

| 命令 | 项目中的作用 |
|---|---|
| `XADD` | HTTP 接口提交异步任务到 `yolo:stream:detect` |
| `XGROUP CREATE` | 创建消费组 `yolo11_group` |
| `XREADGROUP` | Worker 从消费组中读取任务 |
| `XACK` | Worker 完成任务后确认消息 |
| `XPENDING` | 查看未确认任务数量 |
| `XAUTOCLAIM` | 回收长时间未 ack 的 pending 任务 |
| `XINFO CONSUMERS` | 查看每个 worker 的 pending 和 idle 状态 |
| `XLEN` | 查看 Stream 当前长度 |
| `XTRIM MAXLEN ~` | 近似裁剪 Stream，防止无限增长 |

这次真正理解到：Redis Stream 不只是“缓存结果”，而是承担了消息队列、消费组、异常恢复和运行状态观测的职责。

### 异步任务状态机

```text
queued -> running -> done
queued -> running -> failed
queued/running -> pending -> reclaimed -> running -> done
```

本阶段新增了以下任务指标：

| 字段 | 含义 |
|---|---|
| `worker_id` | 处理该任务的 Worker 编号 |
| `consumer_name` | Redis Consumer 名称，例如 `worker_2` |
| `queue_wait_ms` | 从任务创建到 Worker 开始处理的排队等待时间 |
| `inference_ms` | TensorRT 推理和结果图生成耗时 |
| `total_ms` | 从任务创建到任务完成的总耗时 |
| `create_time_ms` | 任务创建时间 |
| `start_time_ms` | Worker 开始处理时间 |
| `finish_time_ms` | 任务完成时间 |

这些字段让压测不再只看“能不能返回”，而是能区分到底是模型慢、队列慢，还是 HTTP / Redis / 文件 I/O 慢。

### Health 接口新增字段

当前 `/api/v1/health` 会返回 Redis 和 Worker 状态，例如：

```json
{
  "success": true,
  "status": "ok",
  "queue_backend": "redis_stream",
  "redis_ping": "ok",
  "redis_pending": 0,
  "redis_stream_len": 10012,
  "redis_stream_max_len": 10000,
  "redis_pending_reclaim": true,
  "redis_pending_min_idle_ms": 60000,
  "worker_num": 3
}
```

重点观察：

```text
redis_ping = ok
redis_pending = 0
redis_stream_len ≈ redis_stream_max_len
worker_num = 3
```

### 压测脚本

本阶段新增：

```text
tools/benchmark_async.py
```

标准压测命令：

```powershell
python tools\benchmark_async.py --url http://127.0.0.1:8080 --image D:/tensorrtx/yolo11/images/bus.png --tasks 1000 --concurrency 10 --timeout 240
```

极限压测命令：

```powershell
python tools\benchmark_async.py --url http://127.0.0.1:8080 --image D:/tensorrtx/yolo11/images/bus.png --tasks 3000 --concurrency 20 --timeout 300
```

脚本统计：

- 提交成功数
- done / failed / timeout 数量
- QPS
- worker 分布
- `total_ms` 平均值、p50、p95、p99
- `queue_wait_ms` 平均值、p50、p95、p99
- `inference_ms` 平均值、p50、p95、p99
- 客户端观测延迟

### 实测结果

`worker_num=3` 下的稳定压测结果：

| tasks | concurrency | Done/Failed/Timeout | QPS | avg total_ms | avg queue_wait_ms | avg inference_ms |
|---:|---:|---:|---:|---:|---:|---:|
| 500 | 5 | 500/0/0 | 34.424 | 3756.384 | 3729.490 | 7.529 |
| 1000 | 10 | 1000/0/0 | 34.545 | 8165.548 | 8139.791 | 7.035 |
| 1000 | 20 | 1000/0/0 | 33.784 | 8542.974 | 8514.400 | 8.937 |

结论：

```text
worker_num=3 下，当前稳定吞吐约为 33–35 QPS。
concurrency 从 10 增加到 20 后 QPS 没有继续提升，说明 3-worker 推理池已经接近当前吞吐上限。
继续增加并发主要会增加 queue_wait_ms，而不是提升服务能力。
```

### 极限压测与 XTRIM 验证

极限压测：

```text
Tasks: 3000
Concurrency: 20
Submitted ok: 2999/3000
Done/Failed/Timeout: 2975/0/24
avg total_ms = 64823.343 ms
avg queue_wait_ms = 64787.961 ms
avg inference_ms = 11.221 ms
```

压测后 Health：

```json
{
  "redis_pending": 0,
  "redis_stream_len": 10012,
  "redis_stream_max_len": 10000,
  "worker_num": 3
}
```

这说明两件事：

1. `XTRIM MAXLEN ~ 10000` 生效，Stream 没有无限增长，而是被控制在 10000 附近。由于 `~` 是近似裁剪，`10012` 是正常结果。
2. 即使极限压测中出现 pending 和 Redis 连接错误，最终 `XPENDING = 0`，说明 Pending 恢复机制最终把任务清理干净。

### 本阶段遇到的问题与解决方案

#### 1. `kNumClass = 16` 配置隐患

启动日志一度出现：

```text
kNumClass = 16
```

但当前运行的是 COCO Detection 模型 `yolo11n.engine`，应该是：

```text
kNumClass = 80
```

解决方式：修改 `include/config.h`，重新编译并重新生成 engine。修正后启动日志显示：

```text
kNumClass = 80
```

反思：模型类别数、engine 文件和运行接口必须一致。`person` 这类 `class_id=0` 的结果能跑出来，并不代表类别配置一定正确。

#### 2. Crow INFO 日志影响压测

Crow 默认会输出大量请求日志，压测时每个 POST / GET 都会刷屏。Windows 控制台输出很慢，会干扰性能数据。

解决方式是在 `main_server.cpp` 中设置：

```cpp
crow::SimpleApp app;
app.loglevel(crow::LogLevel::Warning);
```

反思：压测时日志也是性能瓶颈。服务端 benchmark 要尽量减少控制台 I/O。

#### 3. Worker 每任务日志影响压测

即使关闭 Crow INFO 日志，Worker 仍可能输出：

```text
Worker 1 task done: ...
```

压测时 1000 个任务就是 1000 行日志。建议通过配置控制：

```yaml
worker:
  log_task_done: false
```

反思：调试日志和压测日志应该分级，不能混在一起。

#### 4. 高压下 Redis 出现 `address in use`

极限压测中出现：

```text
failed to submit task to Redis
Redis markDone failed: address in use
Redis ackTask failed: address in use
Redis claimPendingTask failed: address in use
```

判断原因：当前 Redis 操作可能频繁创建短连接。高压下，Windows 本地端口 / TCP 连接资源出现压力。

短期处理：

- 不把 `tasks=3000, concurrency=20` 作为常规部署压力。
- 将该组作为极限边界测试记录。
- 观察 `XPENDING` 是否最终回到 0。

长期优化：

- 每个 Worker 持有长期 Redis 连接。
- HTTP Producer 持有长期 Redis 连接。
- 命令失败时自动 reconnect。
- 必要时实现 Redis connection pool。

#### 5. 队列等待才是主要瓶颈

在稳定压测中：

```text
avg queue_wait_ms 远大于 avg inference_ms
```

例如：

```text
1000 tasks / concurrency=10
avg queue_wait_ms = 8139.791 ms
avg inference_ms  = 7.035 ms
```

说明 TensorRT 单次推理很快，真正的耗时来自任务堆积后的排队等待。

反思：当模型推理时间已经降到毫秒级后，系统瓶颈往往会转移到队列、I/O、状态更新、结果轮询和连接管理。

### 本阶段最终结论

```text
Phase 5 压测与稳定性验证完成。
系统在 worker_num=3 下可稳定完成 500/1000 级异步检测任务，稳定吞吐约 33–35 QPS。
Redis Stream Consumer Group 多 Worker 调度均衡，worker_1、worker_2、worker_3 基本平均分配任务。
Redis pending 最终可恢复至 0，Pending reclaim 机制有效。
Redis Stream 长度在 stream_max_len=10000 配置下被控制在 10000 附近，XTRIM 清理策略有效。
当前系统主要瓶颈已经不是 TensorRT 推理本身，而是高并发下的队列等待、Redis 状态更新和连接管理。
```

---

---

## Phase 6 Redis 连接复用与生产化稳定性优化

### 阶段定位

Phase 6 的核心目标不是继续增加新接口，而是解决 Phase 5 极限压测中暴露出的 Redis 连接压力问题。上一阶段在 `tasks=3000, concurrency=20` 场景下曾出现 `address in use`、`markDone failed`、`ackTask failed` 等现象，说明系统虽然具备 Pending 恢复能力，但 Redis 命令执行方式还不够生产化。

本阶段将 Redis 操作从“频繁创建短连接”优化为“连接复用 + 命令失败自动重连”，让 HTTP Producer 和各个 InferenceWorker 在高压下保持更稳定的 Redis 访问能力。

当前阶段可以概括为：

```text
Phase 6：Redis 连接复用 + 自动重连 + 高压稳定性验证
```

### 本阶段新增能力

- `RedisTaskQueue` 内部长期持有 `redisContext*`，避免每条 Redis 命令都重新建立 TCP 连接。
- Redis 命令统一通过连接复用入口执行，并使用 `std::mutex` 保护 `redisContext`。
- Redis 命令失败后支持自动 `reconnect` 一次，提高瞬时网络/连接异常下的恢复能力。
- HTTP Producer 使用一个长期 Redis 连接。
- 每个 InferenceWorker 各自持有独立 `RedisTaskQueue` 和独立 Redis 长连接。
- `/api/v1/health` 的 `phase` 更新为 `phase6_redis_connection_reuse`。
- `server.yaml` 显式保留 `stream_max_len`、`enable_pending_reclaim`、`pending_min_idle_ms` 和 `log_task_done` 等稳定性配置。
- 修复 `markFailed()` 中 Redis 命令参数数量不匹配的隐藏问题。
- 完成 `1000/concurrency=10`、`1000/concurrency=20` 和 `3000/concurrency=20` 三组压测验收。

### Phase 6 当前架构

```text
yolo11_server.exe
├── Crow HTTP Server
│   ├── 同步检测接口
│   ├── 异步任务提交接口
│   ├── 结果查询接口
│   └── Health 状态接口
├── RedisTaskQueue for HTTP Producer
│   ├── 长期 redisContext
│   ├── XADD 提交任务
│   ├── GET/HGET 查询状态
│   └── 命令失败自动 reconnect
├── InferenceService
│   └── 管理 3 个 InferenceWorker
├── InferenceWorker × 3
│   ├── 每个 Worker 独立 Yolo11Detector
│   ├── 每个 Worker 独立 RedisTaskQueue
│   ├── 每个 Worker 独立 redisContext
│   ├── XREADGROUP / XAUTOCLAIM 获取任务
│   ├── markRunning / markDone / markFailed
│   └── XACK 确认任务
└── Redis Stream
    ├── yolo:stream:detect
    ├── yolo11_group
    └── worker_1 / worker_2 / worker_3
```

### 连接复用前后的差异

| 对比项 | Phase 5 | Phase 6 |
|---|---|---|
| Redis 连接方式 | 多数命令临时创建连接 | 每个 RedisTaskQueue 复用长期连接 |
| 高压下风险 | 可能出现 `address in use` | 标准与极限压测均未出现任务失败 |
| Worker Redis 使用 | 任务处理链路存在连接压力 | 每个 Worker 独立连接，职责更清楚 |
| 异常恢复 | 依靠 Pending reclaim 兜底 | 连接复用 + reconnect + Pending reclaim |
| 吞吐表现 | 约 33–35 QPS | 标准压测提升到约 78 QPS |
| 极限测试 | 3000 任务出现 timeout | 3000 任务全部 done |

这一步的意义是：系统不再只是“能通过 Redis Stream 跑通异步任务”，而是开始关注连接生命周期、异常恢复和高压下的资源复用。

### 推荐配置

```yaml
server:
  host: "0.0.0.0"
  port: 8080
  threads: 4

model:
  type: "detect"
  engine_path: "D:/tensorrtx/yolo11/engines/yolo11n.engine"
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
  stream_max_len: 10000
  enable_pending_reclaim: true
  pending_min_idle_ms: 60000

worker:
  worker_num: 3
  consumer_name_prefix: "worker_"
  log_task_done: false
```

注意：如果 WSL IP 变化，需要重新执行：

```bash
hostname -I
```

然后同步修改 `redis.host`。

### Health 验收

Phase 6 中，健康检查应看到：

```json
{
  "success": true,
  "status": "ok",
  "phase": "phase6_redis_connection_reuse",
  "queue_backend": "redis_stream",
  "redis_ping": "ok",
  "redis_pending": 0,
  "redis_stream_len": 10012,
  "redis_stream_max_len": 10000,
  "redis_pending_reclaim": true,
  "redis_pending_min_idle_ms": 60000,
  "worker_num": 3
}
```

重点观察：

```text
phase = phase6_redis_connection_reuse
redis_ping = ok
redis_pending = 0
worker_num = 3
redis_stream_len ≈ 10000
```

### 单任务验证结果

单任务查询示例：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260706_142058_1"
```

关键返回：

```text
status = done
num_detections = 3
worker_id = 1
consumer_name = worker_1
queue_wait_ms = 2
total_ms = 71
bbox_coordinate_system = original_image_pixels
```

这说明异步链路完整闭环：

```text
HTTP 提交任务
-> Redis Stream 入队
-> worker_1 消费
-> TensorRT 推理
-> 保存结果图
-> 写回 Redis result/meta/status
-> 查询接口返回 done
```

### Phase 6 压测结果

| 场景 | Submitted | Done/Failed/Timeout | QPS | avg total_ms | avg queue_wait_ms | avg inference_ms | Worker 分布 |
|---:|---:|---:|---:|---:|---:|---:|---|
| 1000 tasks / concurrency=10 | 1000/1000 | 1000/0/0 | 78.354 | 268.695 | 253.579 | 3.724 | 332 / 335 / 333 |
| 1000 tasks / concurrency=20 | 1000/1000 | 1000/0/0 | 77.986 | 274.207 | 259.444 | 3.738 | 332 / 338 / 330 |
| 3000 tasks / concurrency=20 | 3000/3000 | 3000/0/0 | 71.856 | 544.305 | 528.604 | 3.931 | 999 / 1006 / 995 |

Phase 6 的最关键变化是：`3000 tasks / concurrency=20` 极限测试中，任务全部完成，没有 failed，也没有 timeout。

### Redis 最终验收

极限压测后执行：

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:detect
```

最终结果：

```text
XPENDING = 0

worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0

XLEN = 10026
```

其中 `XLEN=10026` 是正常结果，因为项目使用的是：

```text
XTRIM MAXLEN ~ 10000
```

`~` 表示近似裁剪，Stream 长度会稳定在 10000 附近，而不是严格等于 10000。

### Phase 6 结论

```text
Phase 6 已完成并通过验收。
```

本阶段通过 Redis 连接复用优化，将标准压测吞吐从 Phase 5 的约 33–35 QPS 提升至约 78 QPS；在 `3000 tasks / concurrency=20` 极限场景下，3000 个任务全部完成，Failed=0，Timeout=0，XPENDING=0，三个 Worker 的 pending 均为 0。说明当前 Redis Stream 异步推理链路在连接复用后具备更好的高压稳定性。

### Phase 6 学习反思

1. Redis Stream 不只是队列，Redis 连接本身也是系统资源。高压下频繁创建短连接会成为瓶颈。
2. Pending reclaim 是异常兜底机制，但不能代替正常的连接生命周期管理。
3. 工程优化不能只看模型推理耗时，Redis、HTTP、文件 I/O、日志和连接管理都会影响整体吞吐。
4. `queue_wait_ms` 仍然大于 `inference_ms`，说明系统瓶颈已经从 TensorRT 推理转向服务链路。
5. 每个 Worker 独立 Detector、独立 Redis 连接，是当前单进程多 Worker 架构下更清晰、更稳妥的设计。
6. 压测时必须同时看 QPS、Failed、Timeout、XPENDING、Worker 分布和 Stream 长度，不能只看一个数字。
7. Phase 6 是从“功能跑通”走向“工程稳定”的关键阶段。

---

## Phase 7 Server / Worker 独立进程拆分与 Redis Binary 图片存储

### 阶段定位

Phase 7 的核心目标是把前面阶段已经跑通的 all-in-one 服务进一步拆成更接近生产部署的两类进程：

```text
yolo11_server.exe  -> HTTP Producer / Query
 yolo11_worker.exe -> Redis Stream Consumer / TensorRT Inference Worker
```

在 Phase 6 中，HTTP Server、Redis Producer、InferenceService 和多个 InferenceWorker 仍然位于同一个进程中。虽然功能和压测已经稳定，但这种结构在部署层面仍然偏本地化：HTTP 接口进程会和 GPU 推理 Worker 绑定在一起，Worker 也依赖 Server 保存的本地图片路径。

Phase 7 的工程意义是：把“一个能跑的 C++ 服务”继续升级为“可拆分、可单独启动、可单独崩溃恢复、可为后续多机器部署打基础的推理系统”。

### 本阶段完成内容

- 新增 `yolo11_worker.exe`，作为独立 Worker 进程入口。
- `yolo11_server.exe` 默认只负责 HTTP 接入、异步任务提交、任务结果查询和结果图返回。
- HTTP Server 生产环境下可以不加载 TensorRT engine，减少 GPU 资源占用。
- 异步图片输入从本地路径传递升级为 Redis Binary Key 存储。
- 新增 Redis 图片 Key：
  - `yolo:image:{task_id}:input`
  - `yolo:image:{task_id}:result`
- 新增结果图接口：`GET /api/v1/result/{task_id}/image`。
- Worker 从 Redis 读取图片 bytes，OpenCV 解码后执行 TensorRT 推理。
- Worker 推理完成后将结果图重新编码为 JPEG bytes 写回 Redis。
- 完成 1000、3000、5000 级别压测。
- 完成 Worker 强杀恢复测试。
- 修复 Debug 版本下 Ctrl+C 结束 Worker 触发 Visual C++ Runtime abort 弹窗的问题。

### Phase 7 新架构

```text
yolo11_server.exe
├── Crow HTTP Server
│   ├── GET  /api/v1/health
│   ├── POST /api/v1/detect/image/async
│   ├── GET  /api/v1/result/{task_id}
│   └── GET  /api/v1/result/{task_id}/image
├── RedisTaskQueue for HTTP Producer / Query
│   ├── SETEX yolo:image:{task_id}:input
│   ├── XADD yolo:stream:detect
│   ├── GET yolo:task:{task_id}:result
│   └── GET yolo:image:{task_id}:result
└── 不再默认加载 TensorRT Detector

Redis Stream / Redis Binary Keys
├── yolo:stream:detect
├── yolo:task:{task_id}:status
├── yolo:task:{task_id}:meta
├── yolo:task:{task_id}:result
├── yolo:image:{task_id}:input
└── yolo:image:{task_id}:result

yolo11_worker.exe
├── InferenceWorker
│   ├── 独立 consumer_name，例如 worker_1
│   ├── 独立 RedisTaskQueue 长连接
│   ├── 独立 Yolo11Detector
│   ├── XREADGROUP / XAUTOCLAIM
│   ├── GET input image bytes
│   ├── TensorRT 推理
│   ├── SETEX result image bytes
│   ├── markDone / markFailed
│   └── XACK
└── Ctrl+C 优雅退出
```

### 为什么要从本地路径改成 Redis Binary 图片存储

Phase 6 之前的异步任务中，Worker 主要通过 `input_image_path` 找到 HTTP Server 保存到 `./temp/input` 的图片。这在单机单进程或同一机器上没有问题，但一旦 Server 和 Worker 拆成两个进程，尤其后续放到不同机器或容器中，本地路径就会失效。

Phase 7 改成 Redis Binary Key 后，任务传递变成：

```text
HTTP Server 接收图片 bytes
↓
OpenCV 校验图片有效性
↓
Redis SETEX yolo:image:{task_id}:input
↓
XADD yolo:stream:detect
↓
Worker XREADGROUP 领取任务
↓
Redis GET yolo:image:{task_id}:input
↓
OpenCV imdecode
↓
TensorRT 推理
↓
OpenCV imencode 结果图
↓
Redis SETEX yolo:image:{task_id}:result
↓
HTTP Server 通过 task_id 返回结果图
```

这个设计的价值是：Worker 不再依赖 Server 的本地磁盘路径，为后续多进程、多机器、容器化、对象存储替换打基础。

### Phase 7 推荐启动方式

先启动 Redis：

```bash
sudo service redis-server start
redis-cli ping
hostname -I
```

启动 HTTP Server：

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml
```

分别启动 3 个独立 Worker。推荐用三个 PowerShell 窗口：

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --consumer-name worker_1
```

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --consumer-name worker_2
```

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --consumer-name worker_3
```

本地调试也可以用一个 Worker 进程启动多个内部 Worker：

```powershell
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --worker-num 3
```

但做崩溃恢复测试时，更推荐三个独立 Worker 进程，因为这样可以只强杀其中一个 Worker，不影响其他 Worker。

### Phase 7 API 变化

异步提交任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

返回示例：

```json
{
  "image_storage": "redis_binary_keys",
  "input_image_key": "yolo:image:20260706_152358_460_1:input",
  "result_image_key": "yolo:image:20260706_152358_460_1:result",
  "queue_backend": "redis_stream",
  "result_image_url": "/api/v1/result/20260706_152358_460_1/image",
  "result_url": "/api/v1/result/20260706_152358_460_1",
  "status": "queued",
  "success": true,
  "task_id": "20260706_152358_460_1"
}
```

查询结果：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260706_152358_460_1"
```

下载结果图：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260706_152358_460_1/image" -o result.jpg
start .\result.jpg
```

注意：不要把 `<task_id>` 原样复制到命令里，要换成实际返回的任务 ID。

### Phase 7 压测结果

| 场景 | Submitted | Done/Failed/Timeout | QPS | avg total_ms | avg queue_wait_ms | avg inference_ms | Worker 分布 |
|---:|---:|---:|---:|---:|---:|---:|---|
| 1000 tasks / concurrency=10 | 1000/1000 | 1000/0/0 | 68.261 | 467.554 ms | 446.909 ms | 5.279 ms | 332 / 333 / 335 |
| 3000 tasks / concurrency=20 | 3000/3000 | 3000/0/0 | 60.948 | 649.988 ms | 628.005 ms | 4.202 ms | 997 / 1002 / 1001 |
| 5000 tasks / concurrency=20，中途强杀一个 Worker | 5000/5000 | 5000/0/0 | 57.398 | 15732.293 ms | 15709.110 ms | 5.928 ms | 3507 / 182 / 1311 |

从结果可以看到，Phase 7 的 QPS 比 Phase 6 略低，这是合理的。因为 Phase 7 不再通过本地路径传图，而是把输入图和结果图都写入 Redis Binary Key。这个设计增加了 Redis 网络传输、OpenCV 编解码和二进制读写开销，但换来了进程解耦和后续分布式部署能力。

### Worker 强杀恢复测试

测试方法：启动 1 个 `yolo11_server.exe` 和 3 个独立 `yolo11_worker.exe`，压测过程中强杀 `worker_2` 进程：

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.Name -eq "yolo11_worker.exe" -and $_.CommandLine -like "*worker_2*" } |
  Select-Object ProcessId, CommandLine

Stop-Process -Id <PID> -Force
```

最终压测结果：

```text
Submitted ok:        5000/5000
Done/Failed/Timeout: 5000/0/0
Throughput QPS:      57.398
Worker distribution: {'worker_1': 3507, 'worker_2': 182, 'worker_3': 1311}
```

这个结果说明：`worker_2` 中途退出后，剩余 Worker 继续消费任务，Pending 任务能够被其他 Worker 回收并最终完成。`queue_wait_ms` 明显升高是正常现象，因为 Worker 数量减少且 Pending 回收需要等待 `pending_min_idle_ms`。

### Redis 最终验收

压测结束后执行：

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:detect
```

最终应看到：

```text
XPENDING = 0
worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0
XLEN ≈ 10000
```

`XLEN` 不需要严格等于 10000，因为项目使用的是近似裁剪：

```text
XTRIM MAXLEN ~ 10000
```

### Debug 弹窗修复

测试中发现，Debug 版本下直接 Ctrl+C 结束 `yolo11_worker.exe` 可能弹出：

```text
Microsoft Visual C++ Runtime Library
Debug Error!
abort() has been called
```

这个弹窗不适合无人值守服务和生产部署，因为它会阻塞进程退出。Phase 7 后续补充了以下修复：

- 在 `main_worker.cpp` 中配置 Windows `SetErrorMode`，避免错误弹窗阻塞。
- 通过 `_set_abort_behavior` 抑制 CRT abort 弹窗。
- Debug 模式下将 CRT report 输出到 stderr。
- 使用 `condition_variable` 处理 Ctrl+C 停止信号。
- `InferenceWorker::stop()`、析构和 Detector 释放流程改为 `noexcept` 兜底。
- `detector.release()` 放入安全释放函数，避免析构阶段异常触发 `std::terminate`。

修复后的区别：

```text
Ctrl+C                  -> 测试优雅退出，不应该弹窗
Stop-Process -Force     -> 测试崩溃恢复，进程直接死亡是正常现象
```

### Phase 7 学习反思

1. Server / Worker 分离不是简单多一个 exe，而是服务职责边界的重划分。
2. HTTP Server 不应该长期占用 GPU；生产部署中它更适合只做任务提交和查询。
3. 本地文件路径在单机阶段很方便，但不是跨进程、跨机器部署的稳定接口。
4. Redis Binary 图片存储牺牲了一部分 QPS，但换来了进程解耦和部署弹性。
5. `queue_wait_ms` 仍然远大于 `inference_ms`，说明系统瓶颈主要在队列和服务链路，不在 TensorRT 单次推理。
6. 崩溃恢复测试要用 `Stop-Process -Force`，Ctrl+C 应该用于测试优雅退出。
7. Debug 版本下的弹窗也需要处理，因为它会影响自动化测试和无人值守服务。
8. 生产化不是只看功能能不能跑，而是看能不能退出、能不能恢复、能不能观测、能不能长期运行。

### Phase 7 结论

```text
Phase 7 已完成核心验收。
```

本阶段将系统从 all-in-one 单进程架构升级为 Server / Worker 分离架构；`yolo11_server.exe` 负责 HTTP 接入、任务提交、状态查询和结果图返回，`yolo11_worker.exe` 负责 Redis Stream 消费、TensorRT 推理、结果写回和 XACK 确认。异步图片通过 Redis Binary Key 存储，避免 Worker 依赖 Server 本地路径。1000、3000、5000 级别压测均全部完成，Worker 强杀恢复测试通过，Redis 最终 `XPENDING=0`，说明当前 detect 图片异步服务已经具备较好的工程化部署基础。


---


## Phase 8 / Phase 8.5 健康检查、Worker 心跳、Redis TTL、日志、labels_path 与 metrics

### 阶段定位

Phase 8 和 Phase 8.5 的目标不是继续堆新模型接口，而是把前面已经跑通的 detect 图片异步服务进一步做成“能观测、能拒绝异常请求、能控制 Redis 内存、能定位问题、能给部署人员看懂状态”的工程化服务。

可以概括为：

```text
Phase 8：健康检查增强 + Worker 心跳 + Redis 图片 TTL / 内存控制 + Worker 离线拒绝入队
Phase 8.5：spdlog 日志系统 + labels_path 标签外置 + ResultSerializer 收口 + /api/v1/metrics 指标接口
```

这两个阶段的意义是：系统不再只是“能完成异步推理”，而是开始具备生产服务常见的健康检查、就绪判断、运行指标、日志追踪和资源保护能力。

### Phase 8 完成内容

Phase 8 在 Phase 7 的 Server / Worker 分离基础上，新增了以下能力：

- `/api/v1/health`：Server 基础健康检查，包含 Redis ping、Stream 长度、Pending 数量、Redis 内存和当前 phase。
- `/api/v1/ready`：系统是否可以接收新任务，综合判断 Server、Redis、Worker 和 Redis 内存状态。
- `/api/v1/workers`：查看 Worker 心跳、PID、host、gpu_id、status、current_task_id、processed_count、failed_count 等信息。
- Worker 心跳：每个 Worker 定期写入 `yolo:worker:{consumer_name}:heartbeat`，并设置 TTL。
- Worker 掉线检测：heartbeat key 过期后，Server 能判断该 Worker 不再 alive。
- Worker 离线保护：当 `alive_workers < min_alive_workers` 时，异步提交接口返回 HTTP 503，不再继续写入 Redis Stream。
- Redis 图片 TTL：input image 和 result image 使用不同 TTL，避免 Redis 图片无限堆积。
- input image 清理：任务完成并成功 `XACK` 后删除 `yolo:image:{task_id}:input`。
- Redis 内存保护：当 `used_memory` 超过 `max_redis_used_memory_mb` 时，`/ready=false`，异步提交应拒绝新任务。
- 任务确认逻辑修正：`markDone/markFailed` 成功后才执行 `XACK`，避免“结果没写好但消息已确认”的任务丢失风险。

### Phase 8 核心配置

```yaml
redis:
  task_ttl_seconds: 1800
  input_image_ttl_seconds: 600
  result_image_ttl_seconds: 1800
  max_image_bytes: 5242880
  max_result_image_bytes: 5242880
  delete_input_after_done: true
  max_redis_used_memory_mb: 2048

worker:
  worker_num: 3
  min_alive_workers: 1
  heartbeat_enabled: true
  heartbeat_interval_ms: 3000
  heartbeat_ttl_seconds: 15
```

这些配置的核心含义：

| 配置 | 含义 |
|---|---|
| `input_image_ttl_seconds` | 输入图最长保留时间，避免 Worker 异常时图片永久残留 |
| `result_image_ttl_seconds` | 结果图保留时间，给客户端下载留窗口 |
| `delete_input_after_done` | 任务成功完成后删除输入图 |
| `max_image_bytes` | 限制上传图片大小 |
| `max_redis_used_memory_mb` | Redis 内存保护阈值 |
| `heartbeat_interval_ms` | Worker 写心跳间隔 |
| `heartbeat_ttl_seconds` | Worker 心跳 key 过期时间 |
| `min_alive_workers` | 系统最少需要多少 Worker 在线才算 ready |

### Phase 8 Worker 心跳设计

每个 Worker 会写入类似：

```text
yolo:worker:worker_1:heartbeat
yolo:worker:worker_2:heartbeat
yolo:worker:worker_3:heartbeat
```

字段包括：

```text
consumer_name
pid
host
worker_id
gpu_id
model_type
status
current_task_id
processed_count
failed_count
start_time_ms
last_heartbeat_ms
last_error
```

验证命令：

```bash
redis-cli -h 172.19.196.109 -p 6379 KEYS 'yolo:worker:*:heartbeat'
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:worker:worker_1:heartbeat
redis-cli -h 172.19.196.109 -p 6379 TTL yolo:worker:worker_1:heartbeat
```

阶段测试中，3 个 Worker 心跳均存在，TTL 为 15 秒。停止 Worker 后，heartbeat key 自动过期，`/workers` 显示 `alive=false`，`/ready` 返回 `ready=false`。

### Phase 8 Worker 离线拒绝入队

初版 Phase 8 测试时发现一个重要问题：

```text
/ready = false
alive_workers = 0
但是 POST /api/v1/detect/image/async 仍然返回 queued
```

这说明 Server 已经能判断 Worker 不在线，但异步提交接口还没有使用该判断。修复后，当 Worker 全部离线时：

```powershell
curl.exe -i -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

返回：

```text
HTTP/1.1 503 Service Unavailable
```

JSON：

```json
{
  "success": false,
  "error_code": "NOT_ENOUGH_ALIVE_WORKERS",
  "error": "async task rejected because not enough alive workers",
  "ready": false,
  "reason": "not enough alive workers",
  "alive_workers": 0,
  "min_alive_workers": 1,
  "worker_status": "no_enough_alive_workers"
}
```

这个修复让 Phase 8 形成完整闭环：

```text
Server ok + Redis ok + Worker 足够 + Redis 内存正常 -> 允许入队
Server ok + Redis ok + Worker 不足 -> 拒绝入队，返回 503
Server ok + Redis 内存超限 -> 拒绝入队，返回 503
```

### Phase 8 Redis 图片 TTL 与内存控制验证

完成任务后验证：

```bash
redis-cli -h 172.19.196.109 -p 6379 EXISTS yolo:image:20260706_165348_773_4:input
redis-cli -h 172.19.196.109 -p 6379 EXISTS yolo:image:20260706_165348_773_4:result
redis-cli -h 172.19.196.109 -p 6379 TTL yolo:image:20260706_165348_773_4:result
```

结果：

```text
input exists = 0
result exists = 1
result TTL = 1726
```

说明：

```text
任务完成后 input image 已被删除
result image 继续保留一段时间供客户端下载
Redis Binary 图片不会无限堆积
```

### Phase 8 标准压测结果

```text
Tasks:       1000
Concurrency: 10
Submitted ok: 1000/1000
Done/Failed/Timeout: 1000/0/0
Throughput QPS: 60.373
Worker distribution: worker_1=336, worker_2=331, worker_3=333
```

服务端耗时：

```text
total_ms avg = 52.225 ms
queue_wait_ms avg = 30.287 ms
inference_ms avg = 3.995 ms
```

Redis 验收：

```text
XPENDING = 0
worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0
XLEN ≈ 10000
Redis used_memory ≈ 53 MB
```

Phase 8 结论：健康检查、Worker 心跳、Redis 图片 TTL、Redis 内存控制、Worker 离线拒绝入队和任务确认逻辑修正均已通过。

---

### Phase 8.5 完成内容

Phase 8.5 主要做工程收口和可观测性增强：

- 引入 `spdlog`。
- 拆分 `logs/server.log` 与 `logs/worker.log`。
- 新增 `labels/coco.txt`。
- 新增 `model.labels_path` 配置项。
- 新增 `LabelMap`，将 `class_id -> class_name` 从代码硬编码中抽离。
- 统一 `ResultSerializer`，让同步接口和 Worker 结果 JSON 使用同一套序列化逻辑。
- 新增 `/api/v1/metrics`，返回累计任务、失败数、近期 QPS、平均耗时、Worker 分布和 Redis 状态。
- 新增 Redis metrics key：`yolo:metrics:global`、`yolo:metrics:worker:done`、`yolo:metrics:worker:failed`、`yolo:metrics:recent:done`。

### Phase 8.5 日志系统

新增依赖：

```bat
.\vcpkg.exe install spdlog:x64-windows --vcpkg-root D:\vcpkg
```

日志文件：

```text
logs/server.log
logs/worker.log
```

验证命令：

```powershell
Get-ChildItem .\logs
Get-Content .\logs\server.log -Tail 20
Get-Content .\logs\worker.log -Tail 20
```

实际结果：

```text
server.log = 2095 bytes
worker.log = 1923 bytes
```

日志拆分效果：

| 日志文件 | 主要内容 |
|---|---|
| `server.log` | HTTP Server 启动、Redis Producer、异步任务提交、API 信息 |
| `worker.log` | Worker 进程启动、Redis 配置、TensorRT engine 加载、labels 加载、worker_1/2/3 启动 |

这一步的意义是：后续出问题时不再只能看控制台，而是可以通过日志追踪 Server 和 Worker 各自的行为。

### Phase 8.5 labels_path 标签外置

之前 `class_name` 如果写死在代码里，换自定义模型或 OBB 模型时容易出现 `class_id` 对但 `class_name` 错的问题。

现在配置为：

```yaml
model:
  labels_path: "D:/tensorrtx/yolo11/labels/coco.txt"
```

Health 验证：

```json
{
  "labels_loaded": true,
  "label_count": 80,
  "labels_path": "D:/tensorrtx/yolo11/labels/coco.txt"
}
```

检测结果：

```json
{
  "class_id": 0,
  "class_name": "person"
}
```

现在 `class_name=person` 来自 `labels/coco.txt`，不再来自硬编码数组。

### Phase 8.5 metrics 指标接口

新增接口：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/metrics"
```

返回关键字段：

```json
{
  "success": true,
  "phase": "phase8_5_logging_labels_metrics",
  "metrics_enabled": true,
  "total_tasks_done": 1101,
  "total_tasks_failed": 0,
  "qps_recent": 16.666666666666668,
  "recent_done_count": 1000,
  "recent_window_seconds": 60,
  "worker_distribution": {
    "worker_1": 368,
    "worker_2": 369,
    "worker_3": 364
  },
  "latency": {
    "avg_queue_wait_ms": 248.227,
    "avg_inference_ms": 4.836,
    "avg_total_ms": 269.547
  },
  "redis_pending": 0,
  "redis_stream_len": 10000,
  "alive_workers": 3
}
```

这里要注意：`qps_recent` 是按照最近 60 秒窗口计算的，例如：

```text
1000 / 60 = 16.666
```

而压测脚本里的 QPS 是按本次压测实际完成耗时计算的，所以二者口径不同，不冲突。

Redis metrics 验证：

```bash
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:global
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:worker:done
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:worker:failed
redis-cli -h 172.19.196.109 -p 6379 ZCARD yolo:metrics:recent:done
```

实际结果：

```text
done_count = 1101
worker_1 = 368
worker_2 = 369
worker_3 = 364
failed = empty
```

### Phase 8.5 标准压测结果

```powershell
python .\tools\benchmark_async.py `
  --url http://127.0.0.1:8080 `
  --image D:/tensorrtx/yolo11/images/bus.png `
  --tasks 1000 `
  --concurrency 10
```

结果：

```text
Submitted ok: 1000/1000
Done/Failed/Timeout: 1000/0/0
Throughput QPS: 59.177
Worker distribution: worker_1=334, worker_2=335, worker_3=331
```

服务端耗时：

```text
total_ms avg = 272.631 ms
queue_wait_ms avg = 251.634 ms
inference_ms avg = 4.335 ms
```

Redis 最终状态：

```text
XPENDING = 0
worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0
XLEN = 10000
Redis used_memory ≈ 53.37 MB
```

### Phase 8 / 8.5 关键问题与反思

#### 1. `/health` 和 `/ready` 不是一回事

`/health` 只能说明 Server 进程和基础依赖还活着，不能说明系统能接任务。真正能不能接任务，要看 `/ready`。

例如 Worker 全部离线时：

```text
server_status = ok
redis_status = ok
alive_workers = 0
ready = false
```

反思：生产服务里“活着”和“可服务”必须分开判断。

#### 2. Worker 心跳比 `XINFO CONSUMERS inactive` 更适合判断 Worker 是否活着

Redis Stream 的 `XINFO CONSUMERS` 可以看 pending 和 idle，但 Worker 进程是否真的在线，更适合用 heartbeat key 判断。

反思：消息队列里的 consumer 记录不等于真实进程状态，心跳机制是服务治理中非常重要的一环。

#### 3. XACK 的时机必须非常谨慎

如果结果 JSON、meta、status 或 result image 还没写好就 `XACK`，消息会从 Pending 中消失，后续无法恢复。

正确逻辑是：

```text
markDone/markFailed 成功 -> XACK
markDone/markFailed 失败 -> 不 XACK，等待 XAUTOCLAIM
```

反思：消息确认代表“系统已经可靠完成该任务”，不能过早确认。

#### 4. Redis Binary 图片一定要有 TTL

1000 个任务压测后 Redis 内存约 53 MB，这还是图片较小的情况下。如果没有 TTL，长时间运行后 Redis 会不断膨胀。

反思：把 Redis 当图片存储用可以快速实现进程解耦，但必须配合 TTL、大小限制和内存保护。

#### 5. Worker 离线时继续入队是一种隐性故障

初版 Phase 8 中，Worker 全部离线时异步提交仍返回 `queued`。表面看请求成功了，实际任务没人处理，会导致队列堆积和用户等待。

修复后，Worker 不足直接返回 503。

反思：服务端应该尽早失败，而不是制造“看起来成功”的假成功。

#### 6. 日志不是锦上添花，而是工程系统的基础设施

Phase 8.5 后，Server 和 Worker 有了独立日志文件。后续排查问题时可以区分：

```text
请求有没有进 Server？
任务有没有入 Redis？
Worker 有没有加载 engine？
Worker 有没有消费任务？
labels 有没有加载成功？
```

反思：没有日志的服务，后续越复杂越难维护。

#### 7. class_name 不应该硬编码在业务逻辑里

检测模型、OBB 模型和自定义模型的类别表不同。如果 `class_name` 写死在代码中，换模型时容易出现语义错误。

反思：模型权重、engine、类别数、labels_path 必须作为一组配置统一管理。

#### 8. metrics 让系统从“能跑”变成“能解释”

通过 `/api/v1/metrics` 可以看到：

```text
总任务数
失败数
Worker 分布
平均 queue_wait_ms
平均 inference_ms
平均 total_ms
Redis pending
Redis memory
```

这样才能判断瓶颈到底在模型推理、队列等待、Redis、还是 Worker 数量。

反思：工程优化不能靠感觉，必须靠指标。

### Phase 8.5 阶段结论

```text
Phase 8.5 已完成并通过验收。
```

本阶段在 Phase 8 健康检查、Worker 心跳和 Redis 图片 TTL 的基础上，进一步完成了 spdlog 日志系统、labels_path 标签外置、ResultSerializer 收口和 `/api/v1/metrics` 指标接口。标准压测 `1000 tasks / concurrency=10` 结果为 `1000/0/0`，QPS=59.177，Redis `XPENDING=0`，三个 Worker pending 均为 0，Worker 分布均衡，说明当前 detect 图片异步推理服务已经具备较完整的可观测性和工程稳定性。

下一阶段建议进入：

```text
Phase 9：ImageStorage 抽象与存储层解耦
```

也就是把当前写在业务逻辑中的 Redis Binary 图片读写抽象为统一接口，为后续 LocalFileStorage、RedisImageStorage、MinIO / S3 或共享磁盘存储打基础。

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
cmake --build out\build\x64-Debug --config Debug --target yolo11_worker

.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml

# 另开终端启动 Worker
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --consumer-name worker_1

curl.exe http://127.0.0.1:8080/api/v1/health

curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"

curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"

curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>"

curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>/image" -o result.jpg
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
Phase 1：同步 HTTP 图片检测服务
Phase 1.5：bbox 坐标修正、结果图访问、debug 模式
Phase 2：Redis task/status/result 存储
Phase 3：Redis Stream 异步任务队列
Phase 4：Redis Stream Consumer Group 多 Worker 推理池
Phase 5：压测统计、Pending 恢复、XTRIM 清理与稳定性验证
Phase 6：Redis 连接复用、自动重连与生产化稳定性优化
Phase 7：Server / Worker 独立进程拆分、Redis Binary 图片存储与 Worker 崩溃恢复
Phase 8：健康检查增强、Worker 心跳、Redis 图片 TTL / 内存控制、Worker 离线拒绝入队
Phase 8.5：spdlog 日志系统、labels_path 标签外置、ResultSerializer 收口、metrics 指标接口
```

下一步建议进入：

```text
Phase 9：ImageStorage 抽象与存储层解耦
```

Phase 9 推荐目标：

```text
存储层解耦
├── 设计 ImageStorage 抽象接口
├── 实现 RedisImageStorage
├── 实现 LocalFileImageStorage
├── 让 HttpController / InferenceWorker 不直接依赖 Redis Binary 细节
└── 为后续 MinIO / S3 / 共享磁盘 / 多机器部署打基础

继续保留的稳定性机制
├── Redis Stream Consumer Group
├── 每个 Worker 独立 Detector
├── 每个 Worker 独立 Redis 长连接
├── Worker heartbeat
├── Pending reclaim
├── XTRIM Stream 长度控制
├── Redis 图片 TTL 和内存保护
├── spdlog 日志
└── /api/v1/metrics 指标接口
```

后续扩展方向：

- OBB HTTP 服务化接口
- 视频文件异步检测
- RTSP / 摄像头流服务化
- 多 GPU 调度
- 批量推理 / micro-batching
- 对象存储或数据库持久化
- 结构化日志与服务守护脚本

---

## 致谢

Modified from `tensorrtx/yolo11`.
