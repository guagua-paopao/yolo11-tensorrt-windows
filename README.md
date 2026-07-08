# YOLO11 TensorRT Windows C++ Inference Service

Windows-based YOLO11 TensorRT deployment project using Visual Studio 2019, CUDA, TensorRT 10, OpenCV, Crow, hiredis, Redis Stream, spdlog, yaml-cpp, nlohmann/json, and reusable C++ runtime APIs.

The current version has reached **Phase 12.0: Detect + OBB dual parallel services**. It supports command-line inference, reusable C++ APIs, independent HTTP Server / TensorRT Worker processes, Redis Stream async queues, Redis binary image storage, Worker heartbeat, readiness checks, structured logs, external labels, metrics, OBB async inference, OBB stability/recovery testing, and parallel Detect + OBB deployment.

## Current Status

| Area | Status |
|---|---|
| YOLO11 Detection command-line inference | Supported |
| YOLO11 OBB command-line inference | Supported |
| C++ Detection runtime API | Supported |
| C++ OBB runtime API | Supported |
| Detection image / video / camera demos | Supported |
| OBB image demo | Supported, CPU post-processing verified |
| Pure C++ HTTP image detection service | Supported |
| Server / Worker process split | Supported |
| Redis Stream async task queue | Supported |
| Redis binary input/result image storage | Supported |
| Worker heartbeat and readiness checks | Supported |
| Worker-offline submission protection | Supported, HTTP 503 |
| Redis image TTL and memory guard | Supported |
| Pending task reclaim with `XAUTOCLAIM` | Supported |
| Redis Stream trimming with `XTRIM MAXLEN ~` | Supported |
| Structured logs with spdlog | Supported: `logs/server.log`, `logs/worker.log` |
| External labels via `labels_path` | Supported: `labels/coco.txt`, `labels/dota.txt` |
| Unified result serialization | Supported for Detect and OBB |
| `/api/v1/health`, `/api/v1/ready`, `/api/v1/workers`, `/api/v1/metrics` | Supported |
| OBB async image service | Supported: `/api/v1/detect/obb/async` |
| Multi-model runner refactor | Supported: Detect / OBB runners |
| OBB stability and recovery validation | Passed |
| Detect + OBB dual parallel services | Passed |
| Video file async service | Planned later |
| RTSP / camera stream service | Planned later |
| Multi-GPU scheduling | Planned later |

## Phase Summary

| Phase | Main result |
|---|---|
| Phase 1 | Synchronous C++ HTTP image detection service |
| Phase 1.5 | bbox coordinate correction, result image access, debug mode |
| Phase 2 | Redis task/status/result storage |
| Phase 3 | Redis Stream async task queue |
| Phase 4 | Redis Stream Consumer Group multi-worker inference pool |
| Phase 5 | Benchmarking, metrics, pending reclaim, XTRIM validation |
| Phase 6 | Redis connection reuse and reconnect-on-failure |
| Phase 7 | Server / Worker process split and Redis binary image storage |
| Phase 8 | Health, readiness, Worker heartbeat, Redis image TTL, memory guard |
| Phase 8.5 | spdlog logging, labels_path, ResultSerializer, metrics API |
| Phase 10.0 | OBB async image inference minimal loop |
| Phase 10.5 | Multi-model config and `IModelRunner` refactor |
| Phase 11.0 | OBB stability, invalid input, and Worker recovery validation |
| Phase 12.0 | Detect + OBB dual parallel service deployment |

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

Configure CMake with vcpkg:

```bat
cmake -S . -B out\build\x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

If using Visual Studio generator:

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
│   ├── worker.yaml
│   ├── server_detect_phase12.yaml
│   ├── server_obb_phase12.yaml
│   ├── worker_detect_phase12.yaml
│   └── worker_obb_phase12.yaml
├── labels
│   ├── coco.txt
│   └── dota.txt
├── examples
│   ├── demo_image.cpp
│   ├── demo_video.cpp
│   └── demo_obb_image.cpp
├── include/server
│   ├── app_config.h
│   ├── app_logger.h
│   ├── http_controller.h
│   ├── image_codec.h
│   ├── inference_service.h
│   ├── inference_worker.h
│   ├── label_map.h
│   ├── model_runner.h
│   ├── redis_task_queue.h
│   └── result_serializer.h
├── src/server
│   ├── app_config.cpp
│   ├── app_logger.cpp
│   ├── http_controller.cpp
│   ├── image_codec.cpp
│   ├── inference_service.cpp
│   ├── inference_worker.cpp
│   ├── label_map.cpp
│   ├── main_server.cpp
│   ├── main_worker.cpp
│   ├── model_runner.cpp
│   ├── redis_task_queue.cpp
│   └── result_serializer.cpp
├── tools
│   ├── benchmark_async.py
│   ├── phase11_obb_stability_suite.py
│   ├── phase11_invalid_input_test.py
│   ├── phase11_worker_recovery_test.py
│   ├── phase12_dual_service_test.py
│   └── phase12_dual_benchmark.py
├── plugin
│   └── yololayer.cu
├── images
├── engines
├── output
├── temp
├── logs
├── reports
├── gen_wts.py
├── yolo11_det.cpp
├── yolo11_obb.cpp
└── CMakeLists.txt
```

Generated directories and model files such as `out/`, `build/`, `.vs/`, `output/`, `temp/`, `logs/`, `reports/`, `*.engine`, `*.pt`, `*.wts`, and `*.onnx` should not be committed to Git.

## Model and Label Configuration

Low-level TensorRT class count is configured in:

```text
include/config.h
```

Typical values:

```cpp
const static int kNumClass = 80;      // COCO Detection
constexpr static int kObbNumClass = 15; // current DOTA OBB labels used in this project
```

After changing class count, model structure, TensorRT version, CUDA version, or GPU architecture, rebuild the project and regenerate the TensorRT engine. Do not reuse an incompatible `.engine` file.

Class names used by HTTP JSON are loaded from `labels_path`:

```yaml
model:
  labels_path: "D:/tensorrtx/yolo11/labels/coco.txt"   # detect
  # labels_path: "D:/tensorrtx/yolo11/labels/dota.txt" # obb
```

## Build

Use the x64 Visual Studio command environment before building:

```powershell
cd D:\tensorrtx\yolo11

cmd /c "`"D:\vs2019\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && set" |
ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
    }
}
```

Clean and configure:

```powershell
Remove-Item -Recurse -Force out\build\x64-Debug

cmake -S . -B out\build\x64-Debug `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

Build core targets:

```powershell
cmake --build out\build\x64-Debug --target myplugins
cmake --build out\build\x64-Debug --target yolo11_det
cmake --build out\build\x64-Debug --target yolo11_obb
cmake --build out\build\x64-Debug --target demo_image
cmake --build out\build\x64-Debug --target demo_obb_image
cmake --build out\build\x64-Debug --target yolo11_server
cmake --build out\build\x64-Debug --target yolo11_worker
```

Confirm executables:

```powershell
Get-ChildItem out\build\x64-Debug -Filter "*.exe"
```

`myplugins.dll` must be in the same directory as the executables. The CMake configuration should copy it automatically.

## Convert Models and Build TensorRT Engines

Detection:

```bat
cd /d D:\tensorrtx\yolo11
python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
copy /Y yolo11n.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
yolo11_det.exe -d yolo11n.engine D:\tensorrtx\yolo11\images c
```

OBB:

```bat
cd /d D:\tensorrtx\yolo11
python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
copy /Y yolo11n-obb.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

Run OBB image demo:

```bat
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

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
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:obb yolo11_obb_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:obb yolo11_obb_group
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:detect
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:obb
```

## Phase 12 Deployment: Detect + OBB Dual Parallel Services

Phase 12 uses two HTTP Server processes and two Worker processes. Detect and OBB use independent ports, Redis Streams, Consumer Groups, labels, metrics, and Worker heartbeats.

| Service | Port | Stream | Consumer Group | Worker |
|---|---:|---|---|---|
| Detect | 8080 | `yolo:stream:detect` | `yolo11_group` | `worker_1` |
| OBB | 8081 | `yolo:stream:obb` | `yolo11_obb_group` | `obb_worker_1` |

### 1. Start Detect Server

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server_detect_phase12.yaml
```

### 2. Start OBB Server

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server_obb_phase12.yaml
```

### 3. Start Detect Worker

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker_detect_phase12.yaml --consumer-name worker_1
```

### 4. Start OBB Worker

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker_obb_phase12.yaml --consumer-name obb_worker_1
```

### 5. Health Checks

Detect:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
curl.exe "http://127.0.0.1:8080/api/v1/ready?model=detect"
curl.exe "http://127.0.0.1:8080/api/v1/workers?model=detect"
curl.exe "http://127.0.0.1:8080/api/v1/metrics?model=detect"
```

OBB:

```powershell
curl.exe "http://127.0.0.1:8081/api/v1/health"
curl.exe "http://127.0.0.1:8081/api/v1/ready?model=obb"
curl.exe "http://127.0.0.1:8081/api/v1/workers?model=obb"
curl.exe "http://127.0.0.1:8081/api/v1/metrics?model=obb"
```

Expected key fields:

```text
Detect: model_type=detect, ready=true, label_count=80, redis_pending=0
OBB:    model_type=obb,    ready=true, label_count=15, redis_pending=0
```

## HTTP APIs

| Method | Endpoint | Service | Description |
|---|---|---|---|
| GET | `/api/v1/health` | Detect / OBB | Server and Redis health |
| GET | `/api/v1/ready?model=detect` | Detect | Detect readiness |
| GET | `/api/v1/ready?model=obb` | OBB | OBB readiness |
| GET | `/api/v1/workers?model=...` | Detect / OBB | Worker heartbeat status |
| GET | `/api/v1/metrics?model=...` | Detect / OBB | Runtime metrics |
| POST | `/api/v1/detect/image/async` | Detect | Submit async detection task |
| POST | `/api/v1/detect/obb/async` | OBB | Submit async OBB task |
| GET | `/api/v1/result/{task_id}` | Detect / OBB | Query async result |
| GET | `/api/v1/result/{task_id}/image` | Detect / OBB | Download result image from Redis binary storage |

### Submit Detect Task

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

### Submit OBB Task

```powershell
curl.exe -X POST "http://127.0.0.1:8081/api/v1/detect/obb/async" `
  -H "Content-Type: image/jpeg" `
  --data-binary "@D:/tensorrtx/yolo11/images/a.jpg"
```

### Query and Download Result

```powershell
$task_id = "replace_with_actual_task_id"

curl.exe "http://127.0.0.1:8080/api/v1/result/$task_id"
curl.exe "http://127.0.0.1:8080/api/v1/result/$task_id/image" --output detect_result.jpg

curl.exe "http://127.0.0.1:8081/api/v1/result/$task_id"
curl.exe "http://127.0.0.1:8081/api/v1/result/$task_id/image" --output obb_result.jpg
```

Task IDs include a model prefix in Phase 12:

```text
detect_20260708_093905_635_1
obb_20260708_093905_657_1
```

## Redis Data Model

| Key | Type | Description |
|---|---|---|
| `yolo:stream:detect` | Stream | Detect async task queue |
| `yolo:stream:obb` | Stream | OBB async task queue |
| `yolo:task:{task_id}:status` | String | `queued`, `running`, `done`, or `failed` |
| `yolo:task:{task_id}:meta` | Hash | Worker, timing, image key, error, metrics metadata |
| `yolo:task:{task_id}:result` | String | Final result JSON |
| `yolo:image:{task_id}:input` | Binary String | Input image bytes |
| `yolo:image:{task_id}:result` | Binary String | Rendered result image bytes |
| `yolo:worker:{consumer}:heartbeat` | Hash + TTL | Worker liveness record |
| `yolo:metrics:*` | Hash / ZSet | Done/failed counts, recent QPS, latency, Worker distribution |

Task acknowledgement rule:

```text
markDone/markFailed succeeds -> XACK -> optionally delete input image
markDone/markFailed fails    -> no XACK -> keep input image -> allow XAUTOCLAIM recovery
```

## Benchmarks and Validation

### Phase 11 OBB Stability

| Test | Result |
|---|---|
| OBB single task | `done`, result JSON and result image returned |
| OBB 100 tasks, concurrency 5 | 100/0/0, QPS ≈ 33.047 |
| OBB 500 tasks, concurrency 5 | 500/0/0, QPS ≈ 34.336 |
| Invalid input | HTTP 400, `IMAGE_DECODE_FAILED`, no Worker crash |
| Worker recovery test | Passed, 100 tasks eventually all done |
| Final Redis | `XPENDING=0`, `XLEN=743` |

### Phase 12 Dual Service Smoke Test

```powershell
python tools\phase12_dual_service_test.py `
  --detect-url http://127.0.0.1:8080 `
  --obb-url http://127.0.0.1:8081 `
  --detect-image D:/tensorrtx/yolo11/images/bus.png `
  --obb-image D:/tensorrtx/yolo11/images/a.jpg `
  --timeout 120
```

Expected:

```text
[PASS] Phase 12 dual-service smoke test passed.
```

Verified result:

```text
Detect: done, num_detections=3
OBB:    done, num_detections=0 for the tested image
```

### Phase 12 Dual Benchmark

```powershell
python tools\phase12_dual_benchmark.py `
  --detect-url http://127.0.0.1:8080 `
  --obb-url http://127.0.0.1:8081 `
  --detect-image D:/tensorrtx/yolo11/images/bus.png `
  --obb-image D:/tensorrtx/yolo11/images/a.jpg `
  --detect-tasks 50 `
  --obb-tasks 50 `
  --concurrency 10 `
  --timeout 240
```

Verified result:

```text
Detect: 50 done / 0 failed / 0 timeout
OBB:    50 done / 0 failed / 0 timeout
Total:  100 done, QPS ≈ 31.866
```

Final Redis validation:

```text
XPENDING yolo:stream:detect yolo11_group = 0
XPENDING yolo:stream:obb yolo11_obb_group = 0
worker_1 pending = 0
obb_worker_1 pending = 0
```

## Logs and Reports

Logs:

```powershell
Get-Content .\logs\server.log -Tail 20
Get-Content .\logs\worker.log -Tail 20
```

Reports generated by Phase 11 / 12 scripts:

```text
reports/phase11/
reports/phase12/
```

## Troubleshooting

### `Cannot open engine file`

Check `engine_path` in the server/worker YAML file. Prefer absolute paths:

```yaml
engine_path: "D:/tensorrtx/yolo11/engines/yolo11n.engine"
```

### `labels_loaded=false`

Check that `labels_path` exists and matches the model:

```powershell
Test-Path D:\tensorrtx\yolo11\labels\coco.txt
Test-Path D:\tensorrtx\yolo11\labels\dota.txt
```

### Async submission returns HTTP 503

Check readiness:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/ready?model=detect"
curl.exe "http://127.0.0.1:8081/api/v1/ready?model=obb"
```

Common causes:

```text
not enough alive workers
Redis ping failed
Redis memory guard triggered
```

### Windows PowerShell cannot find `redis-cli`

Use WSL Redis CLI instead:

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:obb yolo11_obb_group
```

Or install/copy `redis-cli.exe` and add it to Windows `PATH`.

### Old Redis consumers show large `inactive`

Old consumers such as `worker_2` or `worker_3` may remain in Redis Consumer Group history. This is normal if `pending=0`.

Optional cleanup:

```bash
redis-cli -h 172.19.196.109 -p 6379 XGROUP DELCONSUMER yolo:stream:detect yolo11_group worker_2
redis-cli -h 172.19.196.109 -p 6379 XGROUP DELCONSUMER yolo:stream:detect yolo11_group worker_3
```

### OBB result has `num_detections=0`

This can be normal when the image does not match the DOTA/OBB domain or confidence threshold. Validate the OBB engine first with:

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

### Debug build shows Visual C++ Runtime abort popup on Ctrl+C

Use the updated `yolo11_worker.exe`. The Worker process includes graceful shutdown and Windows debug popup suppression.

## Git Ignore

Do not upload generated files:

```text
out/
build/
.vs/
output/
temp/
logs/
reports/
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

Completed:

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
Phase 10.0: OBB async image service minimal loop
Phase 10.5: multi-model Runner refactor
Phase 11.0: OBB stability and recovery validation
Phase 12.0: Detect + OBB dual parallel services
```

Recommended next phase:

```text
Phase 13: Video file async detection service
```

Planned improvements:

- Video file async detection
- Video progress / cancel / result-video download
- RTSP / camera stream service
- Multi-GPU / multi-model Worker scheduling
- ImageStorage abstraction and object storage integration
- Prometheus-style metrics
- Service scripts for start/stop/status
- Docker / Linux deployment path

## Acknowledgement

Modified from `tensorrtx/yolo11`.
