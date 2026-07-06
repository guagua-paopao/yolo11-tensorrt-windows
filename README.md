# YOLO11 TensorRT Windows

Windows-based YOLO11 TensorRT deployment project with Visual Studio 2019, CUDA, TensorRT 10, OpenCV, reusable C++ runtime APIs, and a pure C++ HTTP inference server.

The current version has reached **Phase 4: Redis Stream Worker Pool**. It supports synchronous image detection and Redis Stream based asynchronous image detection. The HTTP layer submits tasks and queries results, while `InferenceService` manages multiple `InferenceWorker` instances. Each worker uses an independent `Yolo11Detector` instance and an independent Redis consumer name.

## Current Status

| Area | Status |
|---|---|
| YOLO11 detection CLI inference | Supported |
| YOLO11 OBB CLI inference | Supported |
| C++ detection runtime API | Supported |
| C++ OBB runtime API | Supported, CPU post-processing verified |
| Detection image demo | Supported |
| Detection video / camera demo | Supported |
| OBB image demo | Supported |
| Pure C++ HTTP detection server | Supported |
| Synchronous HTTP image detection | Supported |
| Redis Stream async image detection | Supported |
| Redis Stream consumer group | Supported |
| Multi-worker inference pool | Supported, tested with `worker_num=2` |
| Per-worker TensorRT detector instance | Supported |
| Result image HTTP access | Supported |
| `class_name` in HTTP JSON response | Supported for COCO class order |
| Debug raw model bbox output | Supported with `?debug=true` |
| Benchmark script, metrics, pending recovery | Planned for Phase 5 |
| OBB / Seg / Pose / Video service APIs | Planned later |

## Phase Timeline

| Phase | Scope | Result |
|---|---|---|
| Phase 1 | Synchronous HTTP image detection server | `/health` and `/detect/image` completed |
| Phase 1.5 | Result image access, bbox coordinate fix, `class_name`, debug mode | JSON bbox restored to original image pixels |
| Phase 2 | Redis task status, result storage, async image task submission | Redis status/meta/result keys completed |
| Phase 3 | Redis Stream async queue | `XADD` / `XREADGROUP` / `XACK` task flow completed |
| Phase 4 | Redis Stream multi-worker inference pool | `worker_1` and `worker_2` consume tasks with `XPENDING=0` |
| Phase 5 | Benchmarking, metrics, stability, stream trimming, pending recovery | Planned |

## Architecture

```text
Client / curl / application
        |
        v
Crow HTTP Server: yolo11_server.exe
        |
        +-- GET  /api/v1/health
        +-- POST /api/v1/detect/image
        +-- POST /api/v1/detect/image/async
        +-- GET  /api/v1/result/{task_id}
        +-- GET  /api/v1/image/{filename}
        |
        v
Redis Stream: yolo:stream:detect
Consumer Group: yolo11_group
        |
        v
InferenceService
        +-- InferenceWorker 1, consumer=worker_1, own Yolo11Detector
        +-- InferenceWorker 2, consumer=worker_2, own Yolo11Detector
        |
        v
TensorRT Engine + CUDA + OpenCV post-processing
```

The server still runs as a single executable in this phase, but the HTTP layer and inference worker pool are separated by class responsibility. The next production-oriented step is benchmarking and stability hardening rather than adding more model types immediately.

## Features

- YOLO11 detection TensorRT deployment on Windows.
- YOLO11 OBB TensorRT deployment on Windows.
- TensorRT 10 API support.
- Visual Studio 2019 + CMake build.
- CUDA preprocessing.
- CPU post-processing and optional GPU post-processing path for detection.
- Custom TensorRT plugin support through `myplugins.dll`.
- Reusable C++ runtime API wrappers:
  - `Yolo11Detector`
  - `Yolo11ObbDetector`
- Original TensorRT command-line demos preserved.
- Pure C++ HTTP detection server based on Crow.
- YAML-based server configuration.
- Redis Stream based asynchronous task queue.
- Multi-worker background inference pool.
- Task status, metadata, and result JSON stored in Redis.
- Result image saving and HTTP image access.
- JSON bbox coordinates restored to original image pixels.

## Tested Environment

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
| Redis address used in tests | `172.19.196.109:6379` |
| GPU | RTX 4080 Laptop GPU |
| CUDA architecture | `sm_89` |

Add the runtime DLL directories to the system `Path` or the current terminal session:

```powershell
$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"
```

## Dependencies

Install HTTP and Redis dependencies through vcpkg:

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
```

When configuring CMake manually, use the vcpkg toolchain:

```bat
cmake -S . -B out/build/x64-Debug -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

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
│   │   ├── inference_service.h
│   │   ├── inference_worker.h
│   │   ├── redis_task_queue.h
│   │   └── result_serializer.h
│   ├── yolo11_detector_api.h
│   └── yolo11_obb_api.h
├── src
│   ├── server
│   │   ├── app_config.cpp
│   │   ├── http_controller.cpp
│   │   ├── image_codec.cpp
│   │   ├── inference_service.cpp
│   │   ├── inference_worker.cpp
│   │   ├── main_server.cpp
│   │   ├── redis_task_queue.cpp
│   │   └── result_serializer.cpp
│   ├── preprocess.cu
│   ├── postprocess.cpp
│   └── ...
├── plugin
│   └── yololayer.cu
├── images
├── engines
├── output
├── temp
├── gen_wts.py
├── yolo11_det.cpp
├── yolo11_obb.cpp
└── CMakeLists.txt
```

Generated directories and model files such as `out/`, `build/`, `output/`, `temp/`, `*.engine`, `*.pt`, and `*.wts` should not be committed to Git.

## Configuration

Main server config:

```text
config/server.yaml
```

Recommended configuration:

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

worker:
  worker_num: 2
  consumer_name_prefix: "worker_"
```

Use an absolute `engine_path` when possible. If Redis runs inside WSL, update `redis.host` with the current WSL IP from:

```bash
hostname -I
```

## Build

Build all targets from the project root:

```bat
cd /d D:\tensorrtx\yolo11
cmake --build out\build\x64-Debug --config Debug
```

Build only the HTTP server:

```bat
cmake --build out\build\x64-Debug --config Debug --target yolo11_server
```

Common targets:

```text
myplugins
yolo11_det
yolo11_obb
demo_image
demo_video
demo_obb_image
yolo11_server
```

`myplugins.dll` must be in the same directory as the executable. The CMake configuration should copy it automatically after build.

## Build TensorRT Engine

Convert `.pt` to `.wts`:

```bat
cd /d D:\tensorrtx\yolo11
python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
```

Build detection engine:

```bat
copy /Y yolo11n.wts out\build\x64-Debug\
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
```

Build OBB engine:

```bat
copy /Y yolo11n-obb.wts out\build\x64-Debug\
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
```

Do not reuse an old `.engine` file after changing class count, model structure, TensorRT version, CUDA version, or GPU architecture.

## Redis Setup

Start Redis in WSL:

```bash
sudo service redis-server start
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

Check from Windows PowerShell:

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

If a Windows Redis service is also using `127.0.0.1:6379`, either disable it or avoid using `127.0.0.1` in `server.yaml`:

```powershell
Get-Service | Where-Object { $_.Name -match "redis" -or $_.DisplayName -match "redis" }
Stop-Service Redis
Set-Service Redis -StartupType Disabled
```

## Run HTTP Server

Run from the project root:

```bat
cd /d D:\tensorrtx\yolo11
out\build\x64-Debug\yolo11_server.exe config\server.yaml
```

Or run from the executable directory:

```bat
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
yolo11_server.exe D:\tensorrtx\yolo11\config\server.yaml
```

Expected startup logs include:

```text
Starting InferenceService: worker_num=2, stream=yolo:stream:detect, group=yolo11_group
InferenceWorker started: id=1, consumer=worker_1, backend=redis_stream
InferenceWorker started: id=2, consumer=worker_2, backend=redis_stream
YOLO11 server started.
Queue backend: Redis Stream
Async worker pool size: 2
```

## HTTP API

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/v1/health` | Service, model, Redis, and worker pool health |
| POST | `/api/v1/detect/image` | Synchronous image detection |
| POST | `/api/v1/detect/image?debug=true` | Synchronous detection with raw model bbox debug fields |
| POST | `/api/v1/detect/image/async` | Submit async image detection task |
| GET | `/api/v1/result/{task_id}` | Query queued/running/done/failed status and result JSON |
| GET | `/api/v1/image/{filename}` | Read saved result image |

Health check:

```bat
curl.exe "http://127.0.0.1:8080/api/v1/health"
```

Expected fields:

```json
{
  "success": true,
  "status": "ok",
  "phase": "phase4_redis_stream_worker_pool",
  "queue_backend": "redis_stream",
  "worker_num": 2,
  "redis_ping": "ok"
}
```

Synchronous detection:

```bat
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Asynchronous detection:

```bat
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Query async result:

```bat
curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>"
```

Batch async submission test:

```bat
for /L %i in (1,1,100) do curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

## Redis Validation

Inspect consumers:

```bash
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
```

Expected after successful processing:

```text
worker_1 pending = 0
worker_2 pending = 0
```

Check pending messages:

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
```

Expected:

```text
1) (integer) 0
```

Check stream length:

```bash
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:detect
```

`XLEN` is history length, not pending backlog. `XACK` confirms messages but does not delete stream history. To trim history:

```bash
redis-cli -h 172.19.196.109 -p 6379 XTRIM yolo:stream:detect MAXLEN ~ 1000
```

## CMake Notes

Keep `src/server/*.cpp` out of original demo targets and compile them only into `yolo11_server`.

On Windows, hiredis requires WinSock and `ws2_32`:

```cmake
find_package(hiredis CONFIG REQUIRED)
target_link_libraries(yolo11_server PRIVATE hiredis::hiredis ws2_32)
```

For MSVC, enable UTF-8 source decoding:

```cmake
if(MSVC)
    target_compile_options(yolo11_server PRIVATE /utf-8)
endif()
```

## Troubleshooting

### `Cannot open engine file`

Check `config/server.yaml` and use an absolute path:

```yaml
engine_path: "D:/tensorrtx/yolo11/engines/yolo11n.engine"
```

### PowerShell cannot run the executable

Use `./` or `.\` for relative executable paths:

```powershell
.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml
```

### `Failed to load myplugins.dll`

Make sure `myplugins.dll` is in the same directory as the executable.

### `ERR unknown command 'XGROUP'`

The server is connected to a Redis version that does not support Redis Streams or consumer groups. Confirm the actual Redis server:

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

### Redis timeout or connection refused

Check WSL Redis status and IP:

```bash
sudo service redis-server status
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

Then check from Windows:

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

### Worker logs appear on the same line

This is caused by multiple threads writing to `std::cout` at the same time. It does not affect inference results. A thread-safe logger is planned for Phase 5.

## Git Ignore

Do not commit generated binaries, build directories, models, and runtime outputs:

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

## Next Phase

Phase 5 focuses on production hardening:

- `tools/benchmark_async.py` for automated async benchmark.
- `queue_wait_ms`, `infer_ms`, `total_ms`, `worker_id`, and `consumer_name` metrics.
- Redis Stream trimming through `XTRIM`.
- Thread-safe logging.
- Error-case testing for empty body, invalid image, missing file, Redis failure, and worker failure.
- Pending task recovery with `XPENDING`, `XCLAIM`, or `XAUTOCLAIM`.

## Acknowledgement

Modified from `tensorrtx/yolo11`.
