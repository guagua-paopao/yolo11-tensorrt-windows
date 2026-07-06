# YOLO11 TensorRT Windows

Windows-based YOLO11 TensorRT deployment project with Visual Studio 2019, CUDA, TensorRT 10, OpenCV, reusable C++ runtime APIs, and a pure C++ HTTP detection service.

The current implementation focuses on YOLO11 detection service deployment. It supports synchronous HTTP image detection, Redis Stream based asynchronous task submission, a multi-worker inference pool, task status/result query, result image access, benchmark testing, pending-task recovery, and Redis Stream length control.

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
| Redis Stream consumer group | Supported |
| Multi-worker inference pool | Supported, `worker_num=3` verified |
| Task metrics: queue / inference / total latency | Supported |
| Pending task reclaim with `XAUTOCLAIM` | Supported |
| Redis Stream trimming with `XTRIM MAXLEN ~` | Supported |
| Benchmark script | Supported |
| OBB / Seg / Pose / Video service APIs | Planned later |
| Redis connection reuse / connection pool | Next optimization |

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
| GPU | RTX 4080 Laptop GPU |
| CUDA Architecture | `sm_89` |

Runtime DLL paths should be available in the system `Path` or the current terminal session:

```powershell
$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"
```

## Dependencies

Install server-side dependencies with vcpkg:

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
```

Configure CMake with the vcpkg toolchain:

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
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
│   ├── config.h
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
├── tools
│   └── benchmark_async.py
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

## Model Configuration

Main model configuration file:

```text
include/config.h
```

For COCO detection models:

```cpp
const static int kNumClass = 80;
```

For official YOLO11 OBB / DOTA models:

```cpp
const static int kNumClass = 16;
```

For a custom one-class model:

```cpp
const static int kNumClass = 1;
```

After changing `kNumClass`, model structure, TensorRT version, CUDA version, or GPU architecture, rebuild the project and regenerate the TensorRT engine. Do not reuse an old `.engine` file.

## Server Configuration

Server configuration file:

```text
config/server.yaml
```

Recommended configuration for the current verified setup:

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

Use the current WSL IP from `hostname -I` when Redis runs in WSL.

## Redis Setup

Start Redis in WSL:

```bash
sudo service redis-server start
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

Expected output includes:

```text
PONG
172.19.196.109
```

Verify Redis from Windows:

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

Useful Redis checks:

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:detect
```

## Build

Open the project folder with Visual Studio 2019 or build from PowerShell:

```powershell
cd D:\tensorrtx\yolo11
cmake --build out\build\x64-Debug --config Debug --target yolo11_server
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

`myplugins.dll` must be in the same directory as the executable. The CMake configuration copies it automatically after build.

## Convert Model and Build TensorRT Engine

Download YOLO11 weights with Ultralytics, then convert `.pt` to `.wts`:

```bat
cd /d D:\tensorrtx\yolo11
python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
copy /Y yolo11n.wts out\build\x64-Debug\
```

Build detection engine:

```bat
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
```

Run original command-line detection:

```bat
yolo11_det.exe -d yolo11n.engine D:\tensorrtx\yolo11\images c
```

For OBB:

```bat
cd /d D:\tensorrtx\yolo11
python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
copy /Y yolo11n-obb.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

## Run HTTP Detection Server

Start the server:

```powershell
cd D:\tensorrtx\yolo11\out\build\x64-Debug
.\yolo11_server.exe D:\tensorrtx\yolo11\config\server.yaml
```

Expected startup logs include:

```text
Loading sync TensorRT engine: D:/tensorrtx/yolo11/engines/yolo11n.engine
kNumClass = 80
Starting InferenceService: worker_num=3, stream=yolo:stream:detect, group=yolo11_group
InferenceWorker started: id=1, consumer=worker_1, backend=redis_stream
InferenceWorker started: id=2, consumer=worker_2, backend=redis_stream
InferenceWorker started: id=3, consumer=worker_3, backend=redis_stream
YOLO11 server started.
```

Health check:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
```

Expected fields:

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
  "worker_num": 3
}
```

## HTTP APIs

### Synchronous Image Detection

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Debug mode returns raw model bbox fields:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image?debug=true" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

### Asynchronous Image Detection

Submit task:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Example response:

```json
{
  "success": true,
  "status": "queued",
  "queue_backend": "redis_stream",
  "task_id": "20260706_103021_1",
  "result_url": "/api/v1/result/20260706_103021_1"
}
```

Query result:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260706_103021_1"
```

Completed result contains:

```json
{
  "success": true,
  "status": "done",
  "worker_id": 2,
  "consumer_name": "worker_2",
  "queue_wait_ms": 18,
  "inference_ms": 51.0155,
  "total_ms": 77,
  "num_detections": 3,
  "result_image_url": "/api/v1/image/20260706_103021_1_result.jpg"
}
```

Download result image:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/image/20260706_103021_1_result.jpg" -o result.jpg
start .\result.jpg
```

## Redis Data Model

| Key | Type | Description |
|---|---|---|
| `yolo:stream:detect` | Stream | Async detection task queue |
| `yolo:task:{task_id}:status` | String | `queued`, `running`, `done`, or `failed` |
| `yolo:task:{task_id}:meta` | Hash | Task path, timestamp, worker, latency, error, result image path |
| `yolo:task:{task_id}:result` | String | Final result JSON |

Redis Stream commands used by the service:

```text
XADD       submit task
XGROUP     create consumer group
XREADGROUP consume task
XACK       acknowledge completed task
XAUTOCLAIM reclaim idle pending task
XTRIM      limit stream length
XLEN       health statistics
XPENDING   pending statistics
```

Task state machine:

```text
queued -> running -> done
queued -> running -> failed
```

## Benchmark

Benchmark script:

```text
tools/benchmark_async.py
```

Standard benchmark:

```powershell
python tools\benchmark_async.py --url http://127.0.0.1:8080 --image D:/tensorrtx/yolo11/images/bus.png --tasks 1000 --concurrency 10 --timeout 240
```

High-pressure benchmark:

```powershell
python tools\benchmark_async.py --url http://127.0.0.1:8080 --image D:/tensorrtx/yolo11/images/bus.png --tasks 1000 --concurrency 20 --timeout 300
```

Extreme stream-trimming validation:

```powershell
python tools\benchmark_async.py --url http://127.0.0.1:8080 --image D:/tensorrtx/yolo11/images/bus.png --tasks 3000 --concurrency 20 --timeout 300
```

Verified results with `worker_num=3`:

| Tasks | Concurrency | Done/Failed/Timeout | QPS | avg total_ms | avg queue_wait_ms | avg inference_ms |
|---:|---:|---:|---:|---:|---:|---:|
| 500 | 5 | 500/0/0 | 34.424 | 3756.384 | 3729.490 | 7.529 |
| 1000 | 10 | 1000/0/0 | 34.545 | 8165.548 | 8139.791 | 7.035 |
| 1000 | 20 | 1000/0/0 | 33.784 | 8542.974 | 8514.400 | 8.937 |

Current stable throughput is about **33-35 QPS** with `worker_num=3`. Increasing client concurrency beyond 10 does not significantly improve throughput; it mainly increases queue waiting time.

Extreme test result:

```text
Tasks: 3000
Concurrency: 20
Submitted ok: 2999/3000
Done/Failed/Timeout: 2975/0/24
redis_stream_len: 10012
redis_pending: 0
```

This confirms that `XTRIM MAXLEN ~ 10000` works and pending tasks can be recovered, but it also exposes Redis connection pressure under extreme load.

## Troubleshooting

### `Cannot open engine file`

Check `config/server.yaml`:

```yaml
engine_path: "D:/tensorrtx/yolo11/engines/yolo11n.engine"
```

Make sure the file exists. Do not confuse `engine/` with `engines/`.

### `kNumClass` is wrong

For COCO detection, startup logs should show:

```text
kNumClass = 80
```

If it shows `16`, the project is configured for OBB/DOTA. Update `include/config.h`, rebuild, and regenerate the engine.

### Redis connects to the wrong server

This project uses WSL Redis at `172.19.196.109:6379`. If Windows Redis is running on `127.0.0.1:6379`, avoid using localhost in `server.yaml`.

Check ports and processes:

```powershell
netstat -ano | findstr :6379
Get-Process -Id <PID>
```

### `ERR unknown command 'XGROUP'`

The server is connected to an old or wrong Redis instance. Check Redis version:

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

Redis Streams and consumer groups require a Redis version that supports `XGROUP`.

### `failed to connect Redis: timed out` or `connection refused`

Check WSL Redis:

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

### `address in use` under extreme benchmark

Under `tasks=3000, concurrency=20`, Redis may report errors such as:

```text
failed to submit task to Redis
Redis markDone failed: address in use
Redis ackTask failed: address in use
Redis claimPendingTask failed: address in use
```

The likely cause is frequent Redis TCP connection creation under high pressure. The next optimization is Redis connection reuse or a connection pool.

### JSON bbox does not match result image

The HTTP server should serialize bboxes using the same coordinate restoration logic as drawing. Check that `get_rect()` is used when converting model output to JSON.

## Git Ignore

Do not upload generated files:

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

Completed phases:

```text
Phase 1: synchronous C++ HTTP detection service
Phase 1.5: bbox coordinate correction, result image access, debug mode
Phase 2: Redis task/status/result storage
Phase 3: Redis Stream async task queue
Phase 4: Redis Stream multi-worker inference pool
Phase 5: benchmark, metrics, pending reclaim, XTRIM validation
```

Recommended next phase:

```text
Phase 6: Redis connection reuse and production hardening
```

Planned improvements:

- Redis long-lived connection per worker / producer
- Reconnect strategy after Redis command failure
- Optional Redis connection pool
- Configurable worker task logging
- More accurate benchmark modes
- OBB HTTP service
- Video file async detection
- RTSP / camera stream service
- Multi-GPU scheduling
- Object storage / database integration

## Acknowledgement

Modified from `tensorrtx/yolo11`.
