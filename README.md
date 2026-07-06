# YOLO11 TensorRT Windows

Windows-based YOLO11 TensorRT deployment with Visual Studio 2019, CUDA, TensorRT 10, OpenCV, reusable C++ runtime APIs, and a pure C++ HTTP detection server.

The current version supports synchronous image detection and Redis Stream based asynchronous image detection. The service can receive an image through HTTP, enqueue an async task into Redis, process it with a local C++ worker thread, and return inference results through a task result endpoint.

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
| Pure C++ HTTP detection server | Supported |
| Synchronous HTTP image detection | Supported |
| Redis Stream async image detection | Supported |
| Result image HTTP access | Supported |
| `class_name` in HTTP JSON response | Supported for COCO class order |
| Debug raw model bbox output | Supported with `?debug=true` |
| Split server / worker executables | Planned next |
| OBB / Seg / Pose / Video service APIs | Planned later |

## Features

- YOLO11 detection TensorRT deployment on Windows
- YOLO11 OBB oriented bounding box TensorRT deployment on Windows
- TensorRT 10 API support
- Visual Studio 2019 + CMake build
- CUDA preprocessing
- CPU post-processing and optional GPU post-processing path for detection
- Custom TensorRT plugin support through `myplugins.dll`
- Reusable C++ runtime API wrappers:
  - `Yolo11Detector`
  - `Yolo11ObbDetector`
- Original TensorRT command-line demos preserved
- Pure C++ HTTP detection server based on Crow
- YAML-based server configuration
- Synchronous image detection endpoint
- Redis Stream based asynchronous image detection queue
- Task status/result storage in Redis
- Result image saving and HTTP image access
- JSON bbox coordinates restored to original image pixels

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
| Redis | Redis 8.2.1 in WSL Ubuntu |
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

## Dependencies

The HTTP server uses:

- Crow
- nlohmann/json
- yaml-cpp
- asio, installed as a Crow dependency
- hiredis, used by the Redis async task queue

Install through vcpkg:

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
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
├── engine or engines
├── output
├── temp
├── gen_wts.py
├── yolo11_det.cpp
├── yolo11_obb.cpp
└── CMakeLists.txt
```

Generated directories and model files such as `out/`, `build/`, `output/`, `temp/`, `*.engine`, `*.pt`, and `*.wts` should not be committed to Git.

## Support Matrix

| Task | Original CLI | C++ API | Demo | HTTP Service | Status |
|---|---|---|---|---|---|
| Detection | `yolo11_det.exe` | `Yolo11Detector` | `demo_image.exe`, `demo_video.exe` | Sync + Redis async | Supported |
| OBB | `yolo11_obb.exe` | `Yolo11ObbDetector` | `demo_obb_image.exe` | Planned | API and CPU demo verified |
| Classification | Source available | Planned | Planned | Planned | Not wrapped yet |
| Segmentation | Source available | Planned | Planned | Planned | Not wrapped yet |
| Pose | Source available | Planned | Planned | Planned | Not wrapped yet |

For OBB, CPU post-processing is the recommended validation path. The OBB GPU post-processing path is reserved for further completion and testing.

## Configuration

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

### Server Config

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
```

Use an absolute engine path when possible. If Redis runs inside WSL, use the current WSL IP from `hostname -I`.

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

Server dependencies:

```cmake
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)

set(YOLO11_HIREDIS_TARGET hiredis::hiredis)
```

Link dependencies only to `yolo11_server`:

```cmake
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

On Windows, include WinSock headers before hiredis in `redis_task_queue.cpp`:

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

## Redis Setup

The tested setup uses Redis 8.2.1 in WSL Ubuntu.

Start Redis in WSL:

```bash
sudo service redis-server start
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

Expected output:

```text
PONG
LISTEN ... 0.0.0.0:6379
172.19.196.109
```

Verify from Windows PowerShell:

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

Redis command-level check from Windows:

```powershell
$client = New-Object System.Net.Sockets.TcpClient
$client.Connect("172.19.196.109", 6379)
$stream = $client.GetStream()
$bytes = [Text.Encoding]::ASCII.GetBytes("*1`r`n`$4`r`nPING`r`n")
$stream.Write($bytes, 0, $bytes.Length)
$buffer = New-Object byte[] 1024
$n = $stream.Read($buffer, 0, $buffer.Length)
[Text.Encoding]::ASCII.GetString($buffer, 0, $n)
$client.Close()
```

Expected output:

```text
+PONG
```

If Windows also has a Redis service on `127.0.0.1:6379`, disable it or avoid using `127.0.0.1:6379` in `server.yaml`:

```powershell
Get-Service | Where-Object { $_.Name -match "redis" -or $_.DisplayName -match "redis" }
Stop-Service Redis
Set-Service Redis -StartupType Disabled
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

Recommended validation command:

```bat
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

## HTTP Detection Server

Current server phase:

```text
Phase 2: HTTP detection service with Redis Stream async queue
```

Supported endpoints:

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/v1/health` | Service health, model config, Redis status |
| POST | `/api/v1/detect/image` | Synchronous image detection |
| POST | `/api/v1/detect/image?debug=true` | Synchronous detection with raw model bbox debug fields |
| POST | `/api/v1/detect/image/async` | Submit async image detection task and return `task_id` |
| GET | `/api/v1/result/{task_id}` | Query queued/running/done/failed status and result JSON |
| GET | `/api/v1/image/{filename}` | Read saved result image |

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

Or run from the executable directory:

```powershell
cd D:\tensorrtx\yolo11\out\build\x64-Debug
.\yolo11_server.exe D:\tensorrtx\yolo11\config\server.yaml
```

### Health Check

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
```

Expected response includes:

```json
{
    "success": true,
    "status": "ok",
    "phase": "phase2_redis_stream_queue",
    "queue_backend": "redis_stream",
    "async_worker": "running",
    "redis_enabled": true,
    "redis_ping": "ok"
}
```

### Synchronous Image Detection

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

### Asynchronous Image Detection

Submit task:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Expected response:

```json
{
    "success": true,
    "status": "queued",
    "queue_backend": "redis_stream",
    "task_id": "20260705_214026_1",
    "result_url": "/api/v1/result/20260705_214026_1"
}
```

Query result:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260705_214026_1"
```

Expected final response contains:

```json
{
    "success": true,
    "status": "done",
    "queue_backend": "redis_stream",
    "num_detections": 3,
    "result_image_url": "/api/v1/image/20260705_214026_1_result.jpg"
}
```

Download result image:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/image/20260705_214026_1_result.jpg" -o redis_async_result.jpg
start .\redis_async_result.jpg
```

### Batch Async Submission Test

```powershell
for ($i=1; $i -le 5; $i++) {
  curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
    -H "Content-Type: image/png" `
    --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
}
```

## Redis Data Model

Redis keys used by the async queue:

| Key | Type | Description |
|---|---|---|
| `yolo:stream:detect` | Stream | Async detection task queue |
| `yolo:task:{task_id}:status` | String | `queued`, `running`, `done`, or `failed` |
| `yolo:task:{task_id}:meta` | Hash | Task input path, timestamps, error, result image path |
| `yolo:task:{task_id}:result` | String | Final result JSON |

Task state machine:

```text
queued -> running -> done
queued -> running -> failed
```

Inspect Redis:

```bash
redis-cli -h 172.19.196.109 -p 6379
KEYS yolo:*
XRANGE yolo:stream:detect - +
GET yolo:task:20260705_214026_1:status
GET yolo:task:20260705_214026_1:result
HGETALL yolo:task:20260705_214026_1:meta
XINFO GROUPS yolo:stream:detect
XINFO CONSUMERS yolo:stream:detect yolo11_group
```

## Minimal Commands

### HTTP Server with Redis Async Queue

```powershell
cd D:\tensorrtx\yolo11
cmake --build out\build\x64-Debug --config Debug --target yolo11_server

cd D:\tensorrtx\yolo11\out\build\x64-Debug
.\yolo11_server.exe D:\tensorrtx\yolo11\config\server.yaml
```

Health:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
```

Sync detection:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Async detection:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Result query:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>"
```

## Troubleshooting

### `Cannot open engine file`

Check `config/server.yaml`:

```yaml
engine_path: "D:/tensorrtx/yolo11/engines/yolo11n.engine"
```

Make sure the file exists. Do not confuse `engine/` with `engines/`.

### PowerShell cannot run `yolo11_server.exe`

In PowerShell, run relative executables with `./` or `.\`:

```powershell
.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml
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

### `ERR unknown command 'XGROUP'`

The server is not connected to a Redis version that supports Redis Streams / consumer groups. On this machine, `127.0.0.1:6379` was occupied by a Windows Redis service, while the intended Redis was WSL Redis 8.2.1 at `172.19.196.109:6379`.

Check the actual Redis server:

```powershell
netstat -ano | findstr :6379
Get-Process -Id <PID>
```

Check Redis server information:

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

### `failed to connect Redis: timed out` or `connection refused`

Check WSL Redis status:

```bash
sudo service redis-server status
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

Check from Windows:

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

If WSL IP changes, update `config/server.yaml`.

### Windows Redis conflicts with WSL Redis

If Windows Redis is not needed:

```powershell
Get-CimInstance Win32_Service | Where-Object { $_.Name -match "redis" -or $_.DisplayName -match "redis" }
Stop-Service Redis
Set-Service Redis -StartupType Disabled
```

### hiredis build errors on Windows

Use the vcpkg package name and target below:

```bat
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
```

```cmake
find_package(hiredis CONFIG REQUIRED)
target_link_libraries(yolo11_server PRIVATE hiredis::hiredis ws2_32)
```

Include WinSock headers before `hiredis.h`.

### JSON bbox is inconsistent with the result image

The HTTP server should serialize bboxes using the same coordinate restoration logic as drawing. If this issue appears again, check that `src/server/http_controller.cpp` uses `get_rect()` when converting `Detection` to JSON.

## Git Ignore

Do not upload generated files to GitHub:

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

## Roadmap

Current completed phase:

```text
Phase 2: Redis Stream async image detection queue inside yolo11_server.exe
```

Next planned phase:

```text
Phase 3: split HTTP server and inference worker
```

Planned architecture:

```text
yolo11_server.exe
├── HTTP request handling
├── image saving
├── Redis Stream task submission
└── result query

yolo11_worker.exe
├── Redis Stream consumer
├── TensorRT inference
├── result image saving
└── Redis result writing
```

Future extensions:

- Multiple worker processes
- Multi-GPU scheduling
- OBB HTTP service
- Video file async detection
- RTSP / camera stream service
- Micro-batching with `inferBatch`
- Redis + object storage / database productionization

## Acknowledgement

Modified from `tensorrtx/yolo11`.
