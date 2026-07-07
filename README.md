# YOLO11 TensorRT Windows C++ Inference Service

Windows-based YOLO11 TensorRT deployment project using Visual Studio 2019, CUDA, TensorRT 10, OpenCV, Crow, hiredis, Redis Stream, spdlog, yaml-cpp, nlohmann/json, and reusable C++ runtime APIs.

The current version focuses on a production-style YOLO11 detection image service. It supports command-line inference, reusable C++ detector APIs, a pure C++ HTTP server, Redis Stream asynchronous task queues, a multi-worker TensorRT inference pool, Server/Worker process split, Redis binary image storage, Worker heartbeat, readiness checks, Redis image TTL, structured logs, external label mapping, and runtime metrics.

## Current Status

| Area | Status |
|---|---|
| YOLO11 detection command-line inference | Supported |
| YOLO11 OBB command-line inference | Supported |
| C++ detection runtime API | Supported |
| C++ OBB runtime API | Supported, CPU post-processing verified |
| Detection image / video / camera demos | Supported |
| OBB image demo | Supported |
| Pure C++ HTTP detection server | Supported |
| Server / Worker process split | Supported |
| `yolo11_server.exe` HTTP producer/query process | Supported |
| `yolo11_worker.exe` Redis Stream inference worker process | Supported |
| Redis Stream async image detection | Supported |
| Redis binary input/result image storage | Supported |
| Redis input image cleanup after completion | Supported |
| Redis result image TTL | Supported |
| Redis memory guard in readiness check | Supported |
| Multi-worker inference pool | Supported, 3 workers verified |
| Redis connection reuse and reconnect-on-failure | Supported |
| Pending task reclaim with `XAUTOCLAIM` | Supported |
| Redis Stream trimming with `XTRIM MAXLEN ~` | Supported |
| Worker heartbeat | Supported |
| `/api/v1/health`, `/api/v1/ready`, `/api/v1/workers` | Supported |
| Worker-offline submission protection | Supported, returns HTTP 503 |
| Structured logs with spdlog | Supported: `logs/server.log`, `logs/worker.log` |
| External labels via `labels_path` | Supported: `labels/coco.txt` |
| Unified result serialization | Supported |
| `/api/v1/metrics` | Supported |
| Phase 8.5 standard benchmark | Passed, 1000/0/0, QPS 59.177 |
| OBB / Seg / Pose / Video service APIs | Planned later |

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

Runtime DLL paths should be available in the system `Path` or in the current terminal session:

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
.\vcpkg.exe install spdlog:x64-windows --vcpkg-root D:\vcpkg
```

Configure CMake with the vcpkg toolchain when needed:

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
│   ├── server.yaml
│   └── worker.yaml
├── labels
│   └── coco.txt
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
│   ├── yolo11_detector_api.h
│   └── yolo11_obb_api.h
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
├── tools
│   └── benchmark_async.py
├── plugin
│   └── yololayer.cu
├── images
├── engines
├── output
├── temp
├── logs
├── gen_wts.py
├── yolo11_det.cpp
├── yolo11_obb.cpp
└── CMakeLists.txt
```

Generated directories and model files such as `out/`, `build/`, `.vs/`, `output/`, `temp/`, `logs/`, `*.engine`, `*.pt`, `*.wts`, and `*.onnx` should not be committed to Git.

## Model and Label Configuration

Low-level TensorRT model class count is still configured in:

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

After changing `kNumClass`, model structure, TensorRT version, CUDA version, or GPU architecture, rebuild the project and regenerate the TensorRT engine. Do not reuse an old `.engine` file.

Class names used by HTTP JSON results are loaded from `labels_path`:

```yaml
model:
  labels_path: "D:/tensorrtx/yolo11/labels/coco.txt"
```

For COCO, `labels/coco.txt` should contain 80 lines. The health API should report:

```json
{
  "labels_loaded": true,
  "label_count": 80
}
```

## Recommended Server Configuration

`config/server.yaml`:

```yaml
server:
  host: "0.0.0.0"
  port: 8080
  threads: 4
  enable_sync_detect: false

model:
  type: "detect"
  engine_path: "D:/tensorrtx/yolo11/engines/yolo11n.engine"
  labels_path: "D:/tensorrtx/yolo11/labels/coco.txt"
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
  task_ttl_seconds: 1800
  input_image_ttl_seconds: 600
  result_image_ttl_seconds: 1800
  max_image_bytes: 5242880
  max_result_image_bytes: 5242880
  delete_input_after_done: true
  max_redis_used_memory_mb: 2048
  stream_max_len: 10000
  enable_pending_reclaim: true
  pending_min_idle_ms: 10000
  image_storage: "redis_binary_keys"

worker:
  enabled: false
  worker_num: 3
  min_alive_workers: 1
  consumer_name_prefix: "worker_"
  heartbeat_enabled: true
  heartbeat_interval_ms: 3000
  heartbeat_ttl_seconds: 15
  log_task_done: false

logging:
  enabled: true
  log_dir: "./logs"
  server_log_file: "server.log"
  worker_log_file: "worker.log"
  level: "info"
  console: true

metrics:
  enabled: true
  recent_window_seconds: 60
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
redis-cli -h 172.19.196.109 -p 6379 INFO memory
```

## Build

Build the server and worker targets:

```powershell
cd D:\tensorrtx\yolo11
cmake --build out\build\x64-Debug --config Debug --target yolo11_server
cmake --build out\build\x64-Debug --config Debug --target yolo11_worker
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
yolo11_worker
```

`myplugins.dll` must be in the same directory as the executable. The CMake configuration should copy it automatically after build.

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

## Run Server / Worker Deployment

### 1. Start the HTTP server

```powershell
cd D:\tensorrtx\yolo11

$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"

.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml
```

The server is a lightweight HTTP producer/query process when `server.enable_sync_detect=false`. It submits tasks to Redis, queries results, serves result images from Redis binary keys, and exposes health/readiness/metrics APIs. It does not need to load TensorRT in production mode.

### 2. Start workers

One process with three internal workers:

```powershell
cd D:\tensorrtx\yolo11

$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"

.\out\build\x64-Debug\yolo11_worker.exe .\config\worker.yaml --worker-num 3
```

Or start three independent worker processes:

```powershell
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker.yaml --consumer-name worker_1
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker.yaml --consumer-name worker_2
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker.yaml --consumer-name worker_3
```

For crash-recovery validation, independent worker processes are preferred.

## HTTP APIs

### Health

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
```

Expected key fields:

```json
{
  "success": true,
  "status": "ok",
  "phase": "phase8_5_logging_labels_metrics",
  "queue_backend": "redis_stream",
  "redis_ping": "ok",
  "redis_pending": 0,
  "labels_loaded": true,
  "label_count": 80
}
```

### Readiness

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/ready"
```

Expected when the system can accept new async tasks:

```json
{
  "success": true,
  "ready": true,
  "server_status": "ok",
  "redis_status": "ok",
  "worker_status": "ok",
  "alive_workers": 3,
  "redis_memory_status": "ok"
}
```

If all workers are offline, async submission returns HTTP 503 with:

```json
{
  "success": false,
  "error_code": "NOT_ENOUGH_ALIVE_WORKERS",
  "ready": false,
  "reason": "not enough alive workers"
}
```

### Workers

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/workers"
```

This endpoint reports Worker heartbeat status, PID, host, GPU id, current task, processed count, failed count, and last heartbeat age.

### Metrics

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/metrics"
```

Typical response fields:

```json
{
  "success": true,
  "phase": "phase8_5_logging_labels_metrics",
  "metrics_enabled": true,
  "total_tasks_done": 1101,
  "total_tasks_failed": 0,
  "qps_recent": 16.6666666667,
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

`qps_recent` is calculated using a fixed recent window. For example, `1000 / 60 = 16.666`. Benchmark QPS and `/metrics` recent-window QPS use different calculation methods.

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
  "image_storage": "redis_binary_keys",
  "task_id": "20260706_205743_045_1",
  "input_image_key": "yolo:image:20260706_205743_045_1:input",
  "result_image_key": "yolo:image:20260706_205743_045_1:result",
  "result_url": "/api/v1/result/20260706_205743_045_1",
  "result_image_url": "/api/v1/result/20260706_205743_045_1/image"
}
```

Query result:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>"
```

Download result image from Redis binary storage:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>/image" -o result.jpg
start .\result.jpg
```

### Synchronous Image Detection

Optional debug interface. In production deployment this is usually disabled by `server.enable_sync_detect=false`.

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Debug mode returns raw model bbox fields:

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image?debug=true" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

## Redis Data Model

| Key | Type | Description |
|---|---|---|
| `yolo:stream:detect` | Stream | Async detection task queue |
| `yolo:task:{task_id}:status` | String | `queued`, `running`, `done`, or `failed` |
| `yolo:task:{task_id}:meta` | Hash | Task metadata, worker, timestamps, latency, error, result image info |
| `yolo:task:{task_id}:result` | String | Final result JSON |
| `yolo:image:{task_id}:input` | Binary String | Input image bytes, deleted after successful completion when enabled |
| `yolo:image:{task_id}:result` | Binary String | Rendered result image bytes, served by the HTTP server with TTL |
| `yolo:worker:{consumer}:heartbeat` | Hash + TTL | Worker liveness record |
| `yolo:metrics:global` | Hash | Done count, failed count, accumulated latency, last task |
| `yolo:metrics:worker:done` | Hash | Done task count per worker |
| `yolo:metrics:worker:failed` | Hash | Failed task count per worker |
| `yolo:metrics:recent:done` | ZSet | Recently completed tasks for recent-window QPS |

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
queued/running -> pending -> reclaimed -> running -> done
```

Task acknowledgement rule:

```text
markDone/markFailed succeeds -> XACK -> optionally delete input image
markDone/markFailed fails    -> no XACK -> keep input image -> allow XAUTOCLAIM recovery
```

## Logs

Phase 8.5 uses spdlog:

```text
logs/server.log
logs/worker.log
```

Check logs:

```powershell
Get-ChildItem .\logs
Get-Content .\logs\server.log -Tail 20
Get-Content .\logs\worker.log -Tail 20
```

Expected split:

| File | Main contents |
|---|---|
| `server.log` | Server startup, Redis producer/query, async task submission, API information |
| `worker.log` | Worker startup, Redis configuration, TensorRT engine loading, labels loading, worker start |

## Benchmark

Benchmark script:

```text
tools/benchmark_async.py
```

Standard benchmark:

```powershell
python .\tools\benchmark_async.py `
  --url http://127.0.0.1:8080 `
  --image D:/tensorrtx/yolo11/images/bus.png `
  --tasks 1000 `
  --concurrency 10
```

Phase 8.5 verified result:

| Scenario | Submitted | Done/Failed/Timeout | QPS | avg total_ms | avg queue_wait_ms | avg inference_ms | Worker distribution |
|---|---:|---:|---:|---:|---:|---:|---|
| 1000 tasks / concurrency=10 | 1000/1000 | 1000/0/0 | 59.177 | 272.631 ms | 251.634 ms | 4.335 ms | 334 / 335 / 331 |

Final Redis validation:

```text
XPENDING = 0
worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0
XLEN = 10000
Redis used_memory ≈ 53 MB
```

`queue_wait_ms` is much larger than `inference_ms`, which means the bottleneck is mainly queueing/service-chain overhead rather than TensorRT inference.

## Troubleshooting

### `Cannot open engine file`

Check `config/server.yaml` or `config/worker.yaml`:

```yaml
engine_path: "D:/tensorrtx/yolo11/engines/yolo11n.engine"
```

Make sure the file exists. Do not confuse `engine/` with `engines/`.

### `labels_loaded=false`

Check:

```powershell
Test-Path D:\tensorrtx\yolo11\labels\coco.txt
```

The `labels_path` in both server and worker configuration should point to the same label file.

### Synchronous detection returns unavailable

In production mode, the HTTP server may run with:

```yaml
server:
  enable_sync_detect: false
```

Use `/api/v1/detect/image/async` instead, or enable sync detection only for local debugging.

### Async submission returns HTTP 503

Check readiness:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/ready"
```

Common reasons:

```text
not enough alive workers
redis memory limit exceeded
redis ping failed
```

Start workers and wait a few seconds for heartbeat keys to appear.

### Redis connects to the wrong server

This project uses WSL Redis at `172.19.196.109:6379`. If Windows Redis is running on `127.0.0.1:6379`, avoid using localhost in YAML config.

Check ports and processes:

```powershell
netstat -ano | findstr :6379
Get-Process -Id <PID>
```

Check Redis version:

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

### `ERR unknown command 'XGROUP'`

The server is connected to an old or wrong Redis instance. Redis Streams and consumer groups require a Redis version that supports `XGROUP`.

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

If WSL IP changes, update `config/server.yaml` and `config/worker.yaml`.

### Debug build shows Visual C++ Runtime abort popup on Ctrl+C

The worker includes graceful shutdown and Windows debug popup suppression. Rebuild `yolo11_worker`, then test Ctrl+C shutdown.

### JSON bbox does not match result image

HTTP JSON serialization should use the same coordinate restoration logic as drawing. Check that `get_rect()` is used when converting model output to JSON.

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
Phase 6: Redis connection reuse and production hardening
Phase 7: Server/Worker process split and Redis binary image storage
Phase 8: health, readiness, Worker heartbeat, Redis image TTL, memory guard
Phase 8.5: spdlog logging, labels_path, unified serialization, metrics API
```

Recommended next phase:

```text
Phase 9: ImageStorage abstraction and storage-layer decoupling
```

Planned improvements:

- `ImageStorage` interface
- `RedisImageStorage` / `LocalFileImageStorage`
- OBB HTTP service
- Video file async detection
- RTSP / camera stream service
- Multi-GPU scheduling
- Object storage / database integration
- Service scripts for starting/stopping server and workers
- Docker / Linux deployment path

## Acknowledgement

Modified from `tensorrtx/yolo11`.
