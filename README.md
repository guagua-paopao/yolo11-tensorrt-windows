# YOLO11 TensorRT Windows C++ Inference Service

Windows-based YOLO11 TensorRT deployment project using Visual Studio 2019, CUDA, TensorRT 10, OpenCV, Crow, hiredis, Redis Stream, spdlog, yaml-cpp, nlohmann/json, and reusable C++ runtime APIs.

The current version has reached **Phase 15: Worker Capability Registry**. Detect, OBB, video-file, and live-stream services now expose unified Worker capability metadata through heartbeat, readiness, workers, and metrics APIs. The system can identify and filter workers by `model_type`, `runner_model_type`, `worker_group`, `worker_kind`, `task_kind`, `stream_type`, `gpu_id`, and `max_concurrency`.

This README is deployment-oriented. It focuses on project layout, build steps, service startup, API usage, validation commands, and troubleshooting.

---

## Current Capabilities

| Area | Status |
|---|---|
| YOLO11 Detection command-line inference | Supported |
| YOLO11 OBB command-line inference | Supported |
| Reusable C++ Detection API | Supported: `Yolo11Detector` |
| Reusable C++ OBB API | Supported: `Yolo11ObbDetector` |
| Detection image / video / camera demos | Supported |
| OBB image demo | Supported, CPU post-processing verified |
| Pure C++ HTTP service | Supported |
| Server / Worker process split | Supported |
| Redis Stream async task queue | Supported |
| Redis binary image storage | Supported for image tasks |
| Local file video storage | Supported for video tasks |
| Worker heartbeat and readiness checks | Supported |
| Worker-offline submission protection | Supported, HTTP 503 |
| Redis image TTL and memory guard | Supported |
| Pending task reclaim with `XAUTOCLAIM` | Supported |
| Redis Stream trimming with `XTRIM MAXLEN ~` | Supported |
| Structured logs with spdlog | Supported under `runtime/logs/...` |
| External labels via `labels_path` | Supported: `labels/coco.txt`, `labels/dota.txt` |
| Unified result serialization | Supported for Detect and OBB |
| `/api/v1/health`, `/api/v1/ready`, `/api/v1/workers`, `/api/v1/metrics` | Supported |
| Worker capability registry | Supported in Phase 15 |
| Capability filtering by `model`, `task_kind`, `worker_group`, `worker_kind`, `stream_type`, `gpu_id` | Supported |
| Detect capability profile | `model_type=detect`, `runner_model_type=detect`, `worker_group=image_detect_gpu0`, `task_kind=image_async` |
| OBB capability profile | `model_type=obb`, `runner_model_type=obb`, `worker_group=image_obb_gpu0`, `task_kind=image_async` |
| Video capability profile | `model_type=video`, `runner_model_type=detect`, `worker_group=video_detect_gpu0`, `task_kind=video_file` |
| Stream capability profile | `model_type=stream`, `runner_model_type=detect`, `worker_group=stream_detect_gpu0`, `task_kind=live_stream`, `stream_type=long_running_stream` |
| Detection async image service | Supported: `/api/v1/detect/image/async` |
| OBB async image service | Supported: `/api/v1/detect/obb/async` |
| Detection async video-file service | Supported: `/api/v1/detect/video/async` |
| Video progress query | Supported: `/api/v1/video/result/{task_id}` |
| Video result download | Supported: `/api/v1/video/result/{task_id}/file` |
| Video task cancellation | Supported: `/api/v1/video/result/{task_id}/cancel` |
| Video task cleanup | Supported: `/api/v1/video/result/{task_id}/cleanup` |
| Live stream service | Supported, Phase 14.0 / 14.5 single-stream mode |
| Stream start / stop / status / snapshot APIs | Supported: `/api/v1/stream/start`, `/api/v1/stream/{stream_id}/stop`, `/api/v1/stream/{stream_id}/status`, `/api/v1/stream/{stream_id}/snapshot` |
| Stream Worker process | Supported: `yolo11_stream_worker.exe` |
| Duplicate stream-start protection | Supported, returns HTTP 409 `STREAM_ALREADY_ACTIVE` |
| Stream reconnect and failure handling | Supported, `reconnecting` then `failed` after max attempts |
| Stream stale-active cleanup | Supported, stale active stream can be marked failed and released before a new start |
| Stream terminal metrics | Supported, `stopped` increments `done_count`, `failed` increments `failed_count` |
| Stream snapshot output | Supported as latest local JPEG under `runtime/output/streams/{stream_id}/snapshot.jpg` |
| Video invalid input rejection | Verified |
| Video Worker recovery | Verified |
| Video batch benchmark | Verified |
| Detect + OBB + Video separated service layout | Supported |
| RTSP / live camera stream service | Supported for single-stream lifecycle; advanced multi-stream management planned |
| Multi-GPU automatic scheduling | Planned; Phase 15 only adds capability declaration and filtering |
| Prometheus / production monitoring | Planned |

---

## Phase Summary

| Phase | Main Result |
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
| Phase 13.0 | Detection video-file async service minimal loop |
| Phase 13.5 | Video stability, cancellation, invalid input, Worker recovery, benchmark |
| Structure cleanup | Clean config layout and unified `runtime/` input/output/log directories |
| Phase 14.0 | Live stream service minimal loop: start, status, snapshot, stop |
| Phase 14.5 | Stream stability: duplicate-start protection, TTL renewal, stale cleanup, reconnect/failure handling, repeated start/stop validation, and stream metrics validation |
| Phase 15 | Worker Capability Registry: unified Worker metadata, capability filtering, and Detect / OBB / Video / Stream validation |

---

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

---

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
cmake -S . -B out\build\x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
```

If CMake fails during CUDA compiler detection, start a clean x64 Visual Studio environment first:

```powershell
cmd /c "`"D:\vs2019\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && set" |
ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}

where cl
where nvcc
```

`where cl` should point to a `Hostx64\x64` compiler path.

---

## Project Structure

The current layout keeps active configs in `config/`, old phase configs under `config/archive/`, and runtime files under `runtime/`.

```text
yolo11/
├── api/
│   ├── yolo11_detector_api.cpp
│   └── yolo11_obb_api.cpp
├── config/
│   ├── server.yaml
│   ├── server_detect.yaml
│   ├── worker_detect.yaml
│   ├── server_obb.yaml
│   ├── worker_obb.yaml
│   ├── server_video.yaml
│   ├── worker_video.yaml
│   ├── server_stream.yaml
│   ├── worker_stream.yaml
│   ├── README.md
│   └── archive/
│       └── legacy_phase_configs/
├── labels/
│   ├── coco.txt
│   └── dota.txt
├── include/server/
│   ├── app_config.h
│   ├── app_logger.h
│   ├── http_controller.h
│   ├── image_codec.h
│   ├── inference_worker.h
│   ├── label_map.h
│   ├── model_runner.h
│   ├── redis_task_queue.h
│   ├── result_serializer.h
│   ├── video_inference_worker.h
│   └── stream_inference_worker.h
├── src/server/
│   ├── app_config.cpp
│   ├── app_logger.cpp
│   ├── http_controller.cpp
│   ├── image_codec.cpp
│   ├── inference_worker.cpp
│   ├── label_map.cpp
│   ├── main_server.cpp
│   ├── main_worker.cpp
│   ├── main_video_worker.cpp
│   ├── main_stream_worker.cpp
│   ├── model_runner.cpp
│   ├── redis_task_queue.cpp
│   ├── result_serializer.cpp
│   ├── video_inference_worker.cpp
│   └── stream_inference_worker.cpp
├── runtime/
│   ├── input/
│   │   ├── images/detect/
│   │   ├── images/obb/
│   │   └── videos/detect/
│   ├── output/
│   │   ├── images/detect/
│   │   ├── images/obb/
│   │   ├── videos/detect/
│   │   └── streams/
│   └── logs/
│       ├── detect/server/
│       ├── detect/worker/
│       ├── obb/server/
│       ├── obb/worker/
│       ├── video/server/
│       ├── video/worker/
│       ├── stream/server/
│       └── stream/worker/
├── scripts/
│   ├── start_detect_server.ps1
│   ├── start_detect_worker.ps1
│   ├── start_obb_server.ps1
│   ├── start_obb_worker.ps1
│   ├── start_video_server.ps1
│   ├── start_video_worker.ps1
│   ├── stop_all_services.ps1
│   ├── clean_video_redis.ps1
│   └── clean_runtime_files.ps1
├── tools/
│   ├── benchmark_async.py
│   ├── phase12_dual_service_test.py
│   ├── phase12_dual_benchmark.py
│   ├── phase13_video_smoke_test.py
│   ├── phase13_5_video_invalid_input_test.py
│   ├── phase13_5_video_cancel_test.py
│   ├── phase13_5_video_benchmark.py
│   ├── phase13_5_video_worker_recovery_test.py
│   ├── phase14_stream_smoke_test.py
│   ├── phase14_stream_source_matrix_test.py
│   ├── phase14_5_stream_stability_test.py
│   └── phase14_5_stream_metrics_check.py
├── plugin/
├── images/
├── videos/
├── engines/
├── gen_wts.py
├── yolo11_det.cpp
├── yolo11_obb.cpp
└── CMakeLists.txt
```

Generated directories and model files such as `out/`, `build/`, `.vs/`, `runtime/`, `reports/`, `*.engine`, `*.pt`, `*.wts`, and `*.onnx` should not be committed to Git.

---

## Model and Label Configuration

Low-level TensorRT class count is configured in:

```text
include/config.h
```

Typical values:

```cpp
const static int kNumClass = 80;          // COCO Detection
constexpr static int kObbNumClass = 15;  // current DOTA OBB labels used in this project
```

After changing class count, model structure, TensorRT version, CUDA version, or GPU architecture, rebuild the project and regenerate the TensorRT engine. Do not reuse an incompatible `.engine` file.

HTTP JSON class names are loaded from `labels_path`:

```yaml
model:
  labels_path: "D:/tensorrtx/yolo11/labels/coco.txt"   # detect / video detect
  # labels_path: "D:/tensorrtx/yolo11/labels/dota.txt" # obb
```

---

## Build

Use the x64 Visual Studio command environment before building:

```powershell
cd D:\tensorrtx\yolo11

cmd /c "`"D:\vs2019\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && set" |
ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}
```

Clean and configure:

```powershell
Remove-Item -Recurse -Force out\build\x64-Debug

cmake -S . -B out\build\x64-Debug `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

Build common targets:

```powershell
cmake --build out\build\x64-Debug --target myplugins --parallel 8
cmake --build out\build\x64-Debug --target yolo11_det --parallel 8
cmake --build out\build\x64-Debug --target yolo11_obb --parallel 8
cmake --build out\build\x64-Debug --target demo_image --parallel 8
cmake --build out\build\x64-Debug --target demo_obb_image --parallel 8
cmake --build out\build\x64-Debug --target yolo11_server --parallel 8
cmake --build out\build\x64-Debug --target yolo11_worker --parallel 8
cmake --build out\build\x64-Debug --target yolo11_video_worker --parallel 8
cmake --build out\build\x64-Debug --target yolo11_stream_worker --parallel 8
```

Confirm executables:

```powershell
Get-ChildItem out\build\x64-Debug -Filter "*.exe"
```

`myplugins.dll` must be in the same directory as the executables. The CMake configuration should copy it automatically.

---

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

---

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
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:obb yolo11_obb_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:video:detect yolo11_video_detect_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group

redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:obb yolo11_obb_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:video:detect yolo11_video_detect_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:live:detect yolo11_stream_detect_group

redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:detect
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:obb
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:video:detect
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:live:detect
```

Stream metrics Redis checks:

```bash
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:yolo_stream_live_detect:global
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:yolo_stream_live_detect:worker:done
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:yolo_stream_live_detect:worker:failed
redis-cli -h 172.19.196.109 -p 6379 ZCARD yolo:metrics:yolo_stream_live_detect:recent:done
```

Expected final state after a clean run:

```text
XPENDING = 0
consumer pending = 0
active key = nil
stream metrics done/failed counters match the latest validation run
```

---

## Service Deployment

Each server and worker should be started in a separate PowerShell window. The project also provides start scripts under `scripts/`.

### Detect Service

| Item | Value |
|---|---|
| Port | `8080` |
| Stream | `yolo:stream:detect` |
| Group | `yolo11_group` |
| Worker | `worker_1` |
| Configs | `config/server_detect.yaml`, `config/worker_detect.yaml` |

Start Detect Server:

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server_detect.yaml
```

Start Detect Worker:

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker_detect.yaml --consumer-name worker_1
```

Or use scripts:

```powershell
.\scripts\start_detect_server.ps1
.\scripts\start_detect_worker.ps1
```

### OBB Service

| Item | Value |
|---|---|
| Port | `8081` |
| Stream | `yolo:stream:obb` |
| Group | `yolo11_obb_group` |
| Worker | `obb_worker_1` |
| Configs | `config/server_obb.yaml`, `config/worker_obb.yaml` |

Start OBB Server:

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server_obb.yaml
```

Start OBB Worker:

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker_obb.yaml --consumer-name obb_worker_1
```

Or use scripts:

```powershell
.\scripts\start_obb_server.ps1
.\scripts\start_obb_worker.ps1
```

### Video Detection Service

| Item | Value |
|---|---|
| Port | `8082` |
| Stream | `yolo:stream:video:detect` |
| Group | `yolo11_video_detect_group` |
| Worker | `video_worker_1` |
| Configs | `config/server_video.yaml`, `config/worker_video.yaml` |
| Input videos | `runtime/input/videos/detect` |
| Output videos | `runtime/output/videos/detect` |

Start Video Server:

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server_video.yaml
```

Start Video Worker:

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_video_worker.exe .\config\worker_video.yaml --consumer-name video_worker_1
```

Or use scripts:

```powershell
.\scripts\start_video_server.ps1
.\scripts\start_video_worker.ps1
```


### Live Stream Service

| Item | Value |
|---|---|
| Port | `8083` |
| Stream | `yolo:stream:live:detect` |
| Group | `yolo11_stream_detect_group` |
| Worker | `stream_worker_1` |
| Configs | `config/server_stream.yaml`, `config/worker_stream.yaml` |
| Snapshot output | `runtime/output/streams/{stream_id}/snapshot.jpg` |

Start Stream Server:

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server_stream.yaml
```

Start Stream Worker:

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_stream_worker.exe .\config\worker_stream.yaml --consumer-name stream_worker_1
```

The Phase 14.5 stream service is intentionally single-stream first. It validates one long-running source lifecycle before adding multi-stream scheduling.

Stop all services:

```powershell
.\scripts\stop_all_services.ps1
```

## Phase 15 Worker Capability Registry

Phase 15 does not implement automatic multi-GPU scheduling yet. It first makes every Worker describe what it can handle, so the server can filter, expose, and validate workers consistently across Detect, OBB, video-file, and live-stream services.

### Capability Fields

| Field | Meaning |
|---|---|
| `model_type` | External service/task type used by `/workers`, `/ready`, and `/metrics`; examples: `detect`, `obb`, `video`, `stream` |
| `runner_model_type` | Internal model actually used for inference; video and stream currently use the Detect runner |
| `worker_group` | Deployment group or resource group, for example `image_detect_gpu0` |
| `worker_kind` | Worker category: `image`, `video`, or `stream` |
| `task_kind` | Task category: `image_async`, `video_file`, or `live_stream` |
| `stream_type` | Queue/lifecycle type: `redis_stream` for short tasks, `long_running_stream` for live stream tasks |
| `gpu_id` | GPU binding declared by the Worker |
| `max_concurrency` | Current Worker capacity declaration; this is metadata, not automatic scheduling yet |

### Expected Capability Matrix

| Service | Port | Worker | `model_type` | `runner_model_type` | `worker_group` | `worker_kind` | `task_kind` | `stream_type` |
|---|---:|---|---|---|---|---|---|---|
| Detect image | 8080 | `worker_1` | `detect` | `detect` | `image_detect_gpu0` | `image` | `image_async` | `redis_stream` |
| OBB image | 8081 | `obb_worker_1` | `obb` | `obb` | `image_obb_gpu0` | `image` | `image_async` | `redis_stream` |
| Video file | 8082 | `video_worker_1` | `video` | `detect` | `video_detect_gpu0` | `video` | `video_file` | `redis_stream` |
| Live stream | 8083 | `stream_worker_1` | `stream` | `detect` | `stream_detect_gpu0` | `stream` | `live_stream` | `long_running_stream` |

This separation is important: `model_type=video` and `model_type=stream` describe external service types, while `runner_model_type=detect` shows that both services currently use the Detect model internally.

---


## Phase 15 Capability Checks

Use filtered `workers`, `ready`, and `metrics` endpoints to verify the capability registry.

Detect:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/workers?model=detect&task_kind=image_async"
curl.exe "http://127.0.0.1:8080/api/v1/ready?worker_group=image_detect_gpu0"
curl.exe "http://127.0.0.1:8080/api/v1/metrics?model=detect"
```

OBB:

```powershell
curl.exe "http://127.0.0.1:8081/api/v1/workers?model=obb&task_kind=image_async"
curl.exe "http://127.0.0.1:8081/api/v1/ready?worker_group=image_obb_gpu0"
curl.exe "http://127.0.0.1:8081/api/v1/metrics?model=obb"
```

Video:

```powershell
curl.exe "http://127.0.0.1:8082/api/v1/workers?model=video&task_kind=video_file"
curl.exe "http://127.0.0.1:8082/api/v1/ready?worker_group=video_detect_gpu0"
curl.exe "http://127.0.0.1:8082/api/v1/metrics?model=video"
```

Stream:

```powershell
curl.exe "http://127.0.0.1:8083/api/v1/workers?model=stream&task_kind=live_stream"
curl.exe "http://127.0.0.1:8083/api/v1/ready?worker_group=stream_detect_gpu0"
curl.exe "http://127.0.0.1:8083/api/v1/metrics?model=stream"
```

Expected result:

```text
matched_workers = 1
alive_workers = 1
ready = true
redis_pending = 0
phase = phase15_worker_capability_registry
```

---

## Health Checks

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

Video:

```powershell
curl.exe "http://127.0.0.1:8082/api/v1/health"
curl.exe "http://127.0.0.1:8082/api/v1/ready"
curl.exe "http://127.0.0.1:8082/api/v1/workers"
curl.exe "http://127.0.0.1:8082/api/v1/metrics"
```

Stream:

```powershell
curl.exe "http://127.0.0.1:8083/api/v1/health"
curl.exe "http://127.0.0.1:8083/api/v1/ready"
curl.exe "http://127.0.0.1:8083/api/v1/workers"
curl.exe "http://127.0.0.1:8083/api/v1/metrics"
```

Expected key fields:

```text
success = true
redis_ping = ok
ready = true
alive_workers >= 1
redis_pending = 0
```

For video service, also check:

```text
phase = phase13_5_video_stability_cancel_recovery
video_input_dir = ./runtime/input/videos/detect
video_output_dir = ./runtime/output/videos/detect
video_storage = local_files
```

For stream service, also check:

```text
phase = phase14_5_stream_stability_reconnect
stream_enabled = true
stream_snapshot_dir = D:/tensorrtx/yolo11/runtime/output/streams
stream_enable_reconnect = true
stream_reconnect_max_attempts = 3
```

---

## HTTP APIs

| Method | Endpoint | Service | Description |
|---|---|---|---|
| GET | `/api/v1/health` | Detect / OBB / Video / Stream | Server and Redis health |
| GET | `/api/v1/ready?model=...` | Detect / OBB | Model-specific readiness |
| GET | `/api/v1/ready` | Video / Stream | Service readiness |
| GET | `/api/v1/workers?model=...` | Detect / OBB | Worker heartbeat status |
| GET | `/api/v1/workers` | Video / Stream | Worker heartbeat status |
| GET | `/api/v1/metrics?model=...` | Detect / OBB | Runtime metrics |
| GET | `/api/v1/metrics` | Video / Stream | Runtime metrics |
| POST | `/api/v1/detect/image/async` | Detect | Submit async detection image task |
| POST | `/api/v1/detect/obb/async` | OBB | Submit async OBB image task |
| GET | `/api/v1/result/{task_id}` | Detect / OBB | Query image async result |
| GET | `/api/v1/result/{task_id}/image` | Detect / OBB | Download result image from Redis binary storage |
| POST | `/api/v1/detect/video/async` | Video | Submit async video detection task |
| GET | `/api/v1/video/result/{task_id}` | Video | Query video task progress/result |
| GET | `/api/v1/video/result/{task_id}/file` | Video | Download result video |
| POST | `/api/v1/video/result/{task_id}/cancel` | Video | Request video task cancellation |
| POST | `/api/v1/video/result/{task_id}/cleanup` | Video | Delete local input/output video files after task finishes |
| POST | `/api/v1/stream/start` | Stream | Start one live camera/file/RTSP stream task |
| GET | `/api/v1/stream/{stream_id}/status` | Stream | Query stream state, FPS, frame count, reconnect count, and latest error |
| GET | `/api/v1/stream/{stream_id}/snapshot` | Stream | Download the latest rendered JPEG snapshot |
| POST | `/api/v1/stream/{stream_id}/stop` | Stream | Request stream stop and resource release |

---

## Submit and Query Tasks

### Submit Detect Image Task

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Query and download:

```powershell
$task_id = "replace_with_actual_task_id"

curl.exe "http://127.0.0.1:8080/api/v1/result/$task_id"
curl.exe "http://127.0.0.1:8080/api/v1/result/$task_id/image" --output detect_result.jpg
```

### Submit OBB Image Task

```powershell
curl.exe -X POST "http://127.0.0.1:8081/api/v1/detect/obb/async" `
  -H "Content-Type: image/jpeg" `
  --data-binary "@D:/tensorrtx/yolo11/images/a.jpg"
```

Query and download:

```powershell
$task_id = "replace_with_actual_task_id"

curl.exe "http://127.0.0.1:8081/api/v1/result/$task_id"
curl.exe "http://127.0.0.1:8081/api/v1/result/$task_id/image" --output obb_result.jpg
```

### Submit Video Detection Task

```powershell
curl.exe -X POST "http://127.0.0.1:8082/api/v1/detect/video/async" `
  -H "Content-Type: video/mp4" `
  --data-binary "@D:/tensorrtx/yolo11/videos/test.mp4"
```

Query progress:

```powershell
$task_id = "replace_with_actual_task_id"

curl.exe "http://127.0.0.1:8082/api/v1/video/result/$task_id"
```

Download result video:

```powershell
curl.exe -o runtime/output/videos/detect/result_video.mp4 `
  "http://127.0.0.1:8082/api/v1/video/result/$task_id/file"
```

Cancel a video task:

```powershell
curl.exe -X POST "http://127.0.0.1:8082/api/v1/video/result/$task_id/cancel"
```

Cleanup a finished video task:

```powershell
curl.exe -X POST "http://127.0.0.1:8082/api/v1/video/result/$task_id/cleanup"
```


### Start Live Stream Task

For Windows PowerShell, the most reliable way is to write the JSON body to a file first:

```powershell
@'
{
  "source_type": "camera",
  "camera_id": 0
}
'@ | Set-Content -Encoding utf8 .\stream_start_camera.json

curl.exe -X POST "http://127.0.0.1:8083/api/v1/stream/start" `
  -H "Content-Type: application/json" `
  --data-binary "@stream_start_camera.json"
```

Query stream status:

```powershell
$stream_id = "replace_with_actual_stream_id"

curl.exe "http://127.0.0.1:8083/api/v1/stream/$stream_id/status"
```

Download latest snapshot:

```powershell
curl.exe -o stream_snapshot.jpg "http://127.0.0.1:8083/api/v1/stream/$stream_id/snapshot"
```

Stop the stream:

```powershell
curl.exe -X POST "http://127.0.0.1:8083/api/v1/stream/$stream_id/stop"
```

Duplicate start requests are rejected while one stream is active:

```text
HTTP 409
error_code = STREAM_ALREADY_ACTIVE
```

Current Phase 14.5 start response uses queued lifecycle semantics:

```text
POST /api/v1/stream/start -> HTTP 202
status = queued
lifecycle_status = queued
```

The normal camera path is:

```text
queued -> starting -> running -> stopping -> stopped
```

The invalid-source path is:

```text
queued -> starting -> reconnecting -> failed
```

---

## Redis Data Model

| Key | Type | Description |
|---|---|---|
| `yolo:stream:detect` | Stream | Detect async image task queue |
| `yolo:stream:obb` | Stream | OBB async image task queue |
| `yolo:stream:video:detect` | Stream | Detect async video task queue |
| `yolo:stream:live:detect` | Stream | Stream start command queue |
| `yolo:task:{task_id}:status` | String | `queued`, `running`, `done`, `failed`, or `canceled` |
| `yolo:task:{task_id}:meta` | Hash | Worker, timing, image/video path, progress, error, metrics metadata |
| `yolo:task:{task_id}:result` | String | Final result JSON |
| `yolo:image:{task_id}:input` | Binary String | Input image bytes for image tasks |
| `yolo:image:{task_id}:result` | Binary String | Rendered result image bytes for image tasks |
| `yolo:worker:{consumer}:heartbeat` | Hash + TTL | Worker liveness record |
| `yolo:metrics:*` | Hash / ZSet | Done/failed counts, recent QPS, latency, Worker distribution |
| `yolo:metrics:yolo_stream_live_detect:global` | Hash | Stream service done/failed counters and last terminal task |
| `yolo:metrics:yolo_stream_live_detect:worker:done` | Hash | Stream done-count distribution by Worker |
| `yolo:metrics:yolo_stream_live_detect:worker:failed` | Hash | Stream failed-count distribution by Worker |
| `yolo:metrics:yolo_stream_live_detect:recent:done` | ZSet | Recent stream completions used for recent QPS |
| `yolo:streamtask:{stream_id}:status` | String | Stream state such as `queued`, `starting`, `running`, `reconnecting`, `stopping`, `stopped`, or `failed` |
| `yolo:streamtask:{stream_id}:meta` | Hash | Stream source, snapshot path, timing, frame count, FPS, reconnect count, and error metadata |
| `yolo:streamtask:yolo_stream_live_detect:active` | String | Active stream guard key used to reject duplicate starts; released on `stopped` / `failed` or stale cleanup |

Image tasks store input/result images in Redis binary keys. Video tasks store input/output videos as local files under `runtime/input/videos/detect` and `runtime/output/videos/detect`. Live stream tasks store only state and metadata in Redis and keep the latest rendered snapshot as a local JPEG under `runtime/output/streams/{stream_id}/snapshot.jpg`.

Task acknowledgement rule:

```text
markDone/markFailed/markCanceled succeeds -> XACK -> optional input cleanup
markDone/markFailed fails                 -> no XACK -> allow XAUTOCLAIM recovery
```

---

## Validation

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

### Phase 13 Video Smoke Test

```powershell
python tools\phase13_video_smoke_test.py `
  --url http://127.0.0.1:8082 `
  --video D:/tensorrtx/yolo11/videos/test.mp4 `
  --download D:/tensorrtx/yolo11/runtime/output/videos/detect/result_structure_cleanup_smoke.mp4
```

Expected:

```text
submit 202
poll: queued/running -> done
downloaded result video
```

### Phase 13.5 Invalid Video Input Test

```powershell
python tools\phase13_5_video_invalid_input_test.py `
  --url http://127.0.0.1:8082
```

Expected:

```text
HTTP 400
VIDEO_OPEN_FAILED
PASS
```

### Phase 13.5 Cancel Test

```powershell
python tools\phase13_5_video_cancel_test.py `
  --url http://127.0.0.1:8082 `
  --video D:/tensorrtx/yolo11/videos/test.mp4 `
  --cancel-delay 0.1 `
  --allow-fast-done
```

Expected:

```text
status = canceled
```

For very short videos, `done` before cancellation can be accepted when `--allow-fast-done` is used.

### Phase 13.5 Video Benchmark

```powershell
python tools\phase13_5_video_benchmark.py `
  --url http://127.0.0.1:8082 `
  --video D:/tensorrtx/yolo11/videos/test.mp4 `
  --tasks 20 `
  --concurrency 2 `
  --output-json phase13_5_video_benchmark_20.json
```

Verified result:

```text
tasks_submitted = 20
done = 20
failed = 0
canceled = 0
timeout = 0
qps_done ≈ 1.15
```

### Phase 13.5 Worker Recovery Test

```powershell
python tools\phase13_5_video_worker_recovery_test.py `
  --url http://127.0.0.1:8082 `
  --video D:/tensorrtx/yolo11/videos/test.mp4 `
  --tasks 3
```

During the test, stop and restart the video Worker:

```powershell
Get-Process yolo11_video_worker | Stop-Process -Force

cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_video_worker.exe .\config\worker_video.yaml --consumer-name video_worker_1
```

Verified result:

```text
submitted = 3
done = 3
failed = 0
canceled = 0
timeout = 0
```


### Phase 14.5 Stream Stability Test

```powershell
python tools\phase14_5_stream_stability_test.py `
  --url http://127.0.0.1:8083 `
  --source-type camera `
  --camera-id 0 `
  --repeat 3 `
  --run-seconds 5
```

Verified result:

```text
PASS Phase 14.5 stability test
3 rounds of start -> running -> snapshot -> stop -> stopped
duplicate start -> HTTP 409 STREAM_ALREADY_ACTIVE
invalid camera_id=99 -> reconnecting -> failed after reconnect_count=3
final worker status = idle
```

Final Redis validation:

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:live:detect yolo11_stream_detect_group
redis-cli -h 172.19.196.109 -p 6379 GET yolo:streamtask:yolo_stream_live_detect:active
```

Expected:

```text
XPENDING = 0
stream_worker_1 pending = 0
active key = nil
```

### Phase 14 Source Matrix Test

Use this test to verify camera/file/RTSP lifecycle coverage. In the current laptop-only environment, file and camera success paths are verified; RTSP success requires a real network camera or valid RTSP source. A placeholder RTSP URL is expected to fail and should be treated as an error-path check, not a service failure.

```powershell
python tools\phase14_stream_source_matrix_test.py `
  --url http://127.0.0.1:8083 `
  --include file,camera `
  --file-path D:/tensorrtx/yolo11/videos/test.mp4 `
  --camera-id 0 `
  --wait-seconds 30 `
  --run-seconds 5 `
  --snapshot-dir runtime\output\streams\source_matrix
```

Expected:

```text
file source  -> start -> frames -> snapshot -> stop -> stopped
camera source -> start -> frames -> snapshot -> stop -> stopped
RTSP success -> requires a real RTSP source
RTSP fake URL -> reconnecting -> failed
```

### Phase 14.5 Stream Metrics Check

The stream task is a long-running task. Its Redis Stream command is acknowledged after the Worker enters the running lifecycle, so terminal metrics must be updated in `markStreamStopped()` and `markStreamFailed()` rather than relying on the ordinary short-task metrics path.

```powershell
python tools\phase14_5_stream_metrics_check.py `
  --url http://127.0.0.1:8083 `
  --camera-id 0 `
  --invalid-camera-id 99 `
  --run-seconds 3 `
  --wait-seconds 30
```

Verified result:

```text
PASS Phase 14.5 stream metrics check
valid camera stream:   total_tasks_done = 1
invalid camera stream: total_tasks_failed = 1
metrics_found = true
worker_distribution.stream_worker_1 = 1
worker_failed_distribution.stream_worker_1 = 1
```

Direct metrics check:

```powershell
curl.exe "http://127.0.0.1:8083/api/v1/metrics?model=stream"
```

Expected key fields:

```text
metrics_found = true
total_tasks_done = 1
total_tasks_failed = 1
total_tasks = 2
```


### Phase 15 Worker Capability Registry Validation

Run these after starting all four service pairs: Detect, OBB, Video, and Stream.

```powershell
python tools\phase15_worker_registry_test.py `
  --service detect=http://127.0.0.1:8080 `
  --model detect `
  --task-kind image_async `
  --worker-group image_detect_gpu0 `
  --json-out runtime/phase15_detect_registry.json

python tools\phase15_worker_registry_test.py `
  --service obb=http://127.0.0.1:8081 `
  --model obb `
  --task-kind image_async `
  --worker-group image_obb_gpu0 `
  --json-out runtime/phase15_obb_registry.json

python tools\phase15_worker_registry_test.py `
  --service video=http://127.0.0.1:8082 `
  --model video `
  --task-kind video_file `
  --worker-group video_detect_gpu0 `
  --json-out runtime/phase15_video_registry.json

python tools\phase15_worker_registry_test.py `
  --service stream=http://127.0.0.1:8083 `
  --model stream `
  --task-kind live_stream `
  --worker-group stream_detect_gpu0 `
  --json-out runtime/phase15_stream_registry.json
```

Manual validation commands:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/workers?model=detect&task_kind=image_async"
curl.exe "http://127.0.0.1:8081/api/v1/workers?model=obb&task_kind=image_async"
curl.exe "http://127.0.0.1:8082/api/v1/workers?model=video&task_kind=video_file"
curl.exe "http://127.0.0.1:8083/api/v1/workers?model=stream&task_kind=live_stream"
```

Verified Phase 15 results:

| Service | Validation result |
|---|---|
| Detect 8080 | async image task `done`, result image downloaded, `processed_count=1`, `XPENDING=0` |
| OBB 8081 | async OBB image task `done`, result image downloaded, `processed_count=1`, `XPENDING=0` |
| Video 8082 | video task `done`, result video downloaded, `processed_count=1`, `XPENDING=0` |
| Stream 8083 | camera stream `queued -> running -> stopped`, snapshot downloaded, `total_done=1`, `XPENDING=0` |

Final capability fields verified in Worker heartbeat:

```text
model_type
runner_model_type
worker_group
worker_kind
task_kind
stream_type
gpu_id
max_concurrency
```

### Final Redis Validation

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:obb yolo11_obb_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:video:detect yolo11_video_detect_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:video:detect yolo11_video_detect_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:live:detect yolo11_stream_detect_group
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:video:detect
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:live:detect
```

Expected:

```text
XPENDING = 0 for detect / obb / video / stream
worker_1 pending = 0
obb_worker_1 pending = 0
video_worker_1 pending = 0
stream_worker_1 pending = 0
active stream key = nil
XLEN >= number of submitted video/stream records
```

---

## Logs and Runtime Files

Logs are separated by service role:

```text
runtime/logs/detect/server/
runtime/logs/detect/worker/
runtime/logs/obb/server/
runtime/logs/obb/worker/
runtime/logs/video/server/
runtime/logs/video/worker/
runtime/logs/stream/server/
runtime/logs/stream/worker/
```

Video runtime files:

```text
runtime/input/videos/detect/
runtime/output/videos/detect/
```

Cleanup scripts:

```powershell
.\scripts\clean_video_redis.ps1 -RedisHost 172.19.196.109 -RedisPort 6379
.\scripts\clean_runtime_files.ps1
```

---

## Troubleshooting


### `/workers?model=video` returns zero but `/ready?worker_group=video_detect_gpu0` is true

The video Worker is online, but its heartbeat is probably using the wrong external service type. Phase 15 expects:

```text
model_type = video
runner_model_type = detect
worker_kind = video
task_kind = video_file
worker_group = video_detect_gpu0
```

If `model_type=detect` appears in the video Worker heartbeat, update the heartbeat writing logic in `src/server/video_inference_worker.cpp`, rebuild `yolo11_video_worker`, and restart the Worker.

### Stream API returns `task_kind=stream` instead of `live_stream`

Phase 15 uses `task_kind=live_stream` for external capability filtering. Update the Stream start/status JSON response in `src/server/http_controller.cpp` so external API output matches the Worker registry:

```text
/api/v1/stream/start  -> task_kind = live_stream
/api/v1/stream/status -> task_kind = live_stream
```

The internal Redis start command may still use the older stream command type if the Worker depends on it; the public registry and HTTP response should use `live_stream`.

### PowerShell cannot find `yolo11_server.exe`

Find the actual executable path:

```powershell
cd D:\tensorrtx\yolo11

Get-ChildItem .\out\build -Recurse -Filter yolo11_server.exe | Select-Object FullName
Get-ChildItem .\out\build -Recurse -Filter yolo11_video_worker.exe | Select-Object FullName
Get-ChildItem .\out\build -Recurse -Filter yolo11_stream_worker.exe | Select-Object FullName
```

Use the returned path, for example:

```powershell
.\out\build\x64-Debug\yolo11_server.exe .\config\server_video.yaml
```

### CUDA compiler detection fails with `cudafe++` access violation

Use the x64 Visual Studio environment:

```powershell
cmd /c "`"D:\vs2019\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && set" |
ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}

where cl
```

`cl.exe` must come from `Hostx64\x64`, not `HostX86\x86`.

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
curl.exe "http://127.0.0.1:8082/api/v1/ready"
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
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:obb yolo11_obb_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:video:detect yolo11_video_detect_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group
```

### Old Redis consumers show large `inactive`

Old consumers may remain in Redis Consumer Group history. This is normal if `pending=0`.

Optional cleanup:

```bash
redis-cli -h 172.19.196.109 -p 6379 XGROUP DELCONSUMER yolo:stream:detect yolo11_group worker_2
redis-cli -h 172.19.196.109 -p 6379 XGROUP DELCONSUMER yolo:stream:obb yolo11_obb_group old_obb_worker
redis-cli -h 172.19.196.109 -p 6379 XGROUP DELCONSUMER yolo:stream:video:detect yolo11_video_detect_group old_video_worker
```

### OBB result has `num_detections=0`

This can be normal when the image does not match the DOTA/OBB domain or confidence threshold. Validate the OBB engine first with:

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

### Video `processed_frames` is lower than `total_frames`

OpenCV may report container-estimated frame count through `CAP_PROP_FRAME_COUNT`. Some MP4 files expose more estimated frames than `VideoCapture.read()` can actually decode. If the task status is `done`, `progress=1.0`, and the result video is downloadable, the task completed normally at video EOF.

---


### `/api/v1/stream/start` returns `INVALID_JSON`

PowerShell may strip quotes when JSON is passed directly through `-d`. Use a JSON file and `--data-binary`:

```powershell
@'
{
  "source_type": "camera",
  "camera_id": 0
}
'@ | Set-Content -Encoding utf8 .\stream_start_camera.json

curl.exe -X POST "http://127.0.0.1:8083/api/v1/stream/start" `
  -H "Content-Type: application/json" `
  --data-binary "@stream_start_camera.json"
```

### Stream task stays at `queued`, `starting`, or stale `running`

In the final Phase 14.5 implementation, `/stream/start` returns `queued`. Older logs may still show `created`; treat it as the same pre-worker state. Check whether the Stream Worker is idle and whether old active tasks are still running:

```powershell
curl.exe "http://127.0.0.1:8083/api/v1/workers"
```

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group
redis-cli -h 172.19.196.109 -p 6379 GET yolo:streamtask:yolo_stream_live_detect:active
```

A stuck old active stream should be stopped first. After a clean run, the Worker should show `status=idle` and `current_task_id=""`.

### Duplicate stream start returns HTTP 409

This is expected in Phase 14.5. The stream service is intentionally single-active-stream first:

```text
error_code = STREAM_ALREADY_ACTIVE
```

Stop the current stream before starting another one.

### Invalid camera or RTSP source goes to `reconnecting` then `failed`

This is expected when `enable_reconnect=true`. Example:

```text
status = reconnecting
reconnect_count = 3
status = failed
last_error = failed to open stream source
```

### `/api/v1/metrics?model=stream` stays at zero

A normal stream lifecycle can still complete while metrics remain zero if the terminal hooks are missing or the running Worker executable is old. Verify that `RedisTaskQueue::markStreamStopped()` increments `done_count` and `RedisTaskQueue::markStreamFailed()` increments `failed_count`, then rebuild and restart `yolo11_stream_worker.exe`.

Recommended checks:

```powershell
Select-String -Path .\src\server\redis_task_queue.cpp `
  -Pattern "markStreamStopped|markStreamFailed|HINCRBY %s done_count 1|HINCRBY %s failed_count 1"

redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:yolo_stream_live_detect:global
```

### CUDA clean rebuild fails with `cudafe++` access violation

This is usually an NVCC toolchain crash while recompiling CUDA sources such as `postprocess.cu`, not a stream-service logic failure. Prefer incremental single-thread builds for server/worker changes:

```powershell
cmake --build out\build\x64-Debug --config Debug --target yolo11_stream_worker --parallel 1
cmake --build out\build\x64-Debug --config Debug --target yolo11_server --parallel 1
```

Avoid `--clean-first` unless the CUDA build environment is known to be stable.

## Git Ignore

Do not upload generated files:

```text
out/
build/
.vs/
runtime/
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

---

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
Phase 13.0: Detection video-file async service minimal loop
Phase 13.5: Video stability, cancel, invalid input, recovery, benchmark
Structure cleanup: clean config layout and unified runtime directories
Phase 14.0: Live stream service minimal loop
Phase 14.5: Stream stability, duplicate-start protection, stale cleanup, reconnect/failure handling, stream metrics
Phase 15: Worker Capability Registry and capability-filtered health / readiness / workers / metrics
```

Recommended next phase:

```text
Phase 16: Production hardening preparation
```

Planned improvements:

- Storage abstraction for image/video/stream outputs
- Service scripts and supervisor-friendly process management
- Metrics alignment for future Prometheus integration
- Cleanup policies for Redis keys and runtime files
- Multi-stream lifecycle management after single-stream stability is fully sealed
- Multi-GPU / multi-model automatic scheduling after capability declaration is stable
- Storage abstraction and object storage integration
- Prometheus-style metrics
- Service supervisor and production deployment scripts
- Docker / Linux deployment path

---

## Acknowledgement

Modified from `tensorrtx/yolo11`.
