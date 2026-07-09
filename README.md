# YOLO11 TensorRT Windows C++ Inference Service

Windows-based YOLO11 TensorRT deployment project using Visual Studio 2019, CUDA, TensorRT 10, OpenCV, Crow, hiredis, Redis Stream, spdlog, yaml-cpp, nlohmann/json, and reusable C++ runtime APIs.

The current baseline is **Phase 17.5: CLS + Pose async image services on top of the Phase 16 engineering-freeze baseline**.

The project now supports:

```text
Detect image async service
OBB image async service
CLS image async service
Pose image async service
Detect video-file async service
Single live-stream detect service
Worker Capability Registry
Health / Ready / Workers / Metrics APIs
Redis Stream task queues
Redis Binary image storage
Local video/stream runtime storage
```

This README is deployment-oriented. It focuses on build, startup, validation, APIs, and troubleshooting.

---

## Current Service Matrix

| Service | Port | Server Config | Worker Config | Worker Executable | Redis Stream | Consumer Group | Worker |
|---|---:|---|---|---|---|---|---|
| Detect image | 8080 | `config/server_detect.yaml` | `config/worker_detect.yaml` | `yolo11_worker.exe` | `yolo:stream:detect` | `yolo11_group` | `worker_1` |
| OBB image | 8081 | `config/server_obb.yaml` | `config/worker_obb.yaml` | `yolo11_worker.exe` | `yolo:stream:obb` | `yolo11_obb_group` | `obb_worker_1` |
| Video file detect | 8082 | `config/server_video.yaml` | `config/worker_video.yaml` | `yolo11_video_worker.exe` | `yolo:stream:video:detect` | `yolo11_video_detect_group` | `video_worker_1` |
| Live stream detect | 8083 | `config/server_stream.yaml` | `config/worker_stream.yaml` | `yolo11_stream_worker.exe` | `yolo:stream:live:detect` | `yolo11_stream_detect_group` | `stream_worker_1` |
| CLS image | 8084 | `config/server_cls.yaml` | `config/worker_cls.yaml` | `yolo11_worker.exe` | `yolo:stream:cls` | `yolo11_cls_group` | `cls_worker_1` |
| Pose image | 8085 | `config/server_pose.yaml` | `config/worker_pose.yaml` | `yolo11_worker.exe` | `yolo:stream:pose` | `yolo11_pose_group` | `pose_worker_1` |

---

## Current Capabilities

| Area | Status |
|---|---|
| YOLO11 Detect command-line inference | Supported |
| YOLO11 OBB command-line inference | Supported |
| YOLO11 CLS command-line inference | Supported |
| YOLO11 Pose command-line inference | Supported |
| Reusable C++ Detect API | `Yolo11Detector` |
| Reusable C++ OBB API | `Yolo11ObbDetector` |
| Reusable C++ CLS API | `Yolo11ClsDetector` |
| Reusable C++ Pose API | `Yolo11PoseDetector` |
| Pure C++ HTTP service | Supported by Crow |
| Server / Worker process split | Supported |
| Redis Stream async queue | Supported |
| Redis binary image storage | Supported for image tasks |
| Local file video storage | Supported for video tasks |
| Live stream lifecycle | Single-stream `start/status/snapshot/stop` supported |
| Worker heartbeat and readiness | Supported |
| Worker-offline submission protection | Supported, returns HTTP 503 |
| Pending reclaim | Supported with `XAUTOCLAIM` |
| Stream trimming | Supported with `XTRIM MAXLEN ~` |
| Structured logs | Supported under `runtime/logs/...` |
| External labels | Supported through `labels_path` |
| Metrics API | Supported |
| Worker Capability Registry | Supported for Detect / OBB / Video / Stream / CLS / Pose |
| Model output abstraction | `ModelOutput` supports detections and classifications; Pose uses bbox + keypoints |
| Phase 16 service orchestration | `scripts/start_all.ps1`, `stop_all.ps1`, `check_all.ps1` |
| Phase 17 CLS orchestration | `scripts/start_cls.ps1`, `stop_cls.ps1` |
| Phase 17.5 Pose orchestration | `scripts/start_pose.ps1`, `stop_pose.ps1` |
| Regression suites | `tools/run_full_regression.py`, `tools/run_phase17_regression.py`, `tools/run_phase17_5_regression.py` |
| Seg HTTP service | Planned for Phase 18 |
| Multi-GPU automatic scheduling | Planned, not implemented yet |
| Docker / Linux deployment | Planned as a later deployment branch |

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
| Structure cleanup | Clean config layout and unified `runtime/` directories |
| Phase 14.0 | Live stream service minimal loop |
| Phase 14.5 | Stream stability, duplicate-start protection, TTL renewal, stale cleanup, reconnect/failure handling, stream metrics |
| Phase 15 | Worker Capability Registry and capability-filtered health / readiness / workers / metrics |
| Phase 16 | Engineering freeze: one-command start/check/regression/stop workflow, config templates, reports, and release-candidate baseline |
| Phase 17.0 | CLS async image service, `ModelOutput` abstraction, top1/topk JSON, CLS registry/smoke/regression |
| Phase 17.5 | Pose async image service, COCO17 keypoints/skeleton JSON, Pose registry/smoke, full regression over Phase 17 + Phase 16 |

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

Runtime DLL paths should be available in the system `Path` or current terminal session:

```powershell
$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"
```

---

## Dependencies

Install third-party dependencies with vcpkg:

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install spdlog:x64-windows --vcpkg-root D:\vcpkg
```

Configure with vcpkg:

```bat
cmake -S . -B out\build\x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
```

If CUDA compiler detection fails, start a clean x64 Visual Studio environment first:

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

---

## Project Structure

```text
yolo11/
├── api/
│   ├── yolo11_detector_api.cpp
│   ├── yolo11_obb_api.cpp
│   ├── yolo11_cls_api.cpp
│   └── yolo11_pose_api.cpp
├── config/
│   ├── server_detect.yaml
│   ├── worker_detect.yaml
│   ├── server_obb.yaml
│   ├── worker_obb.yaml
│   ├── server_video.yaml
│   ├── worker_video.yaml
│   ├── server_stream.yaml
│   ├── worker_stream.yaml
│   ├── server_cls.yaml
│   ├── worker_cls.yaml
│   ├── server_pose.yaml
│   ├── worker_pose.yaml
│   ├── *.example.yaml
│   └── archive/
├── labels/
│   ├── coco.txt
│   ├── dota.txt
│   ├── imagenet.txt
│   └── pose_coco.txt
├── include/
│   ├── server/
│   │   ├── model_output.h
│   │   ├── model_runner.h
│   │   ├── result_serializer.h
│   │   └── ...
│   ├── yolo11_detector_api.h
│   ├── yolo11_obb_api.h
│   ├── yolo11_cls_api.h
│   └── yolo11_pose_api.h
├── src/server/
├── runtime/
│   ├── input/
│   ├── output/
│   ├── logs/
│   └── pids/
├── scripts/
│   ├── start_all.ps1
│   ├── stop_all.ps1
│   ├── check_all.ps1
│   ├── start_cls.ps1
│   ├── stop_cls.ps1
│   ├── start_pose.ps1
│   ├── stop_pose.ps1
│   └── clean_runtime.ps1
├── tools/
│   ├── run_full_regression.py
│   ├── run_phase17_regression.py
│   ├── run_phase17_5_regression.py
│   ├── phase17_cls_registry_test.py
│   ├── phase17_cls_smoke_test.py
│   ├── phase17_5_pose_registry_test.py
│   ├── phase17_5_pose_smoke_test.py
│   └── ...
├── reports/
├── engines/
├── images/
├── videos/
└── CMakeLists.txt
```

Generated files should not be committed:

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

## Build

Use a Visual Studio x64 command environment, then build the required targets:

```powershell
cd D:\tensorrtx\yolo11

cmake --build out\build\x64-Debug --target myplugins --parallel 8
cmake --build out\build\x64-Debug --target yolo11_det --parallel 8
cmake --build out\build\x64-Debug --target yolo11_obb --parallel 8
cmake --build out\build\x64-Debug --target yolo11_cls --parallel 8
cmake --build out\build\x64-Debug --target yolo11_pose --parallel 8
cmake --build out\build\x64-Debug --target yolo11_server --parallel 8
cmake --build out\build\x64-Debug --target yolo11_worker --parallel 8
cmake --build out\build\x64-Debug --target yolo11_video_worker --parallel 8
cmake --build out\build\x64-Debug --target yolo11_stream_worker --parallel 8
```

Or build everything:

```powershell
cmake --build out\build\x64-Debug --parallel 8
```

Confirm executables:

```powershell
Get-ChildItem out\build\x64-Debug -Filter "*.exe"
```

`myplugins.dll` must be in the same directory as the executables. The CMake configuration should copy it automatically.

---

## Redis Setup

Start Redis in WSL Ubuntu:

```bash
sudo service redis-server start
redis-cli ping
hostname -I
```

Verify Redis from Windows:

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

If Windows PowerShell cannot find `redis-cli`, use WSL explicitly:

```powershell
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 ping
```

Expected:

```text
PONG
```

If Docker Desktop is the default WSL distro, always pass `-WslDistro Ubuntu` to the Phase 16 check script.

---

## Model Engines

Default configs expect these engine files:

```text
engines/yolo11n.engine
engines/yolo11n-obb.engine
engines/yolo11n-cls.engine
engines/yolo11n-pose.engine
```

Generate CLS engine if needed:

```powershell
python gen_wts.py -w .\weights\yolo11n-cls.pt -t cls -o .\engines\yolo11n-cls.wts
.\out\build\x64-Debug\yolo11_cls.exe -s .\engines\yolo11n-cls.wts .\engines\yolo11n-cls.engine n
```

Generate Pose engine if needed:

```powershell
python gen_wts.py -w .\weights\yolo11n-pose.pt -t pose -o .\engines\yolo11n-pose.wts
.\out\build\x64-Debug\yolo11_pose.exe -s .\engines\yolo11n-pose.wts .\engines\yolo11n-pose.engine n
```

After changing class count, model structure, TensorRT version, CUDA version, or GPU architecture, rebuild the project and regenerate the TensorRT engine. Do not reuse an incompatible `.engine` file.

---

## Quick Start: Phase 16 Core Services

### 1. Start Detect / OBB / Video / Stream

```powershell
cd D:\tensorrtx\yolo11

powershell -ExecutionPolicy Bypass -File .\scripts\start_all.ps1 -ExeDir .\out\build\x64-Debug
```

This starts eight processes:

```text
worker_detect        -> yolo11_worker.exe config\worker_detect.yaml --consumer-name worker_1
worker_obb           -> yolo11_worker.exe config\worker_obb.yaml --consumer-name obb_worker_1
worker_video         -> yolo11_video_worker.exe config\worker_video.yaml --consumer-name video_worker_1
worker_stream        -> yolo11_stream_worker.exe config\worker_stream.yaml --consumer-name stream_worker_1
server_detect_8080   -> yolo11_server.exe config\server_detect.yaml
server_obb_8081      -> yolo11_server.exe config\server_obb.yaml
server_video_8082    -> yolo11_server.exe config\server_video.yaml
server_stream_8083   -> yolo11_server.exe config\server_stream.yaml
```

### 2. Check all core services

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_all.ps1 -UseWslRedisCli -WslDistro Ubuntu
```

Expected:

```text
PASS: all services are ready and Redis pending looks clean.
```

### 3. Run Phase 16 full regression

```powershell
python tools\run_full_regression.py
```

Expected:

```text
Phase 16 Full Regression Summary
success=True
```

### 4. Stop core services

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\stop_all.ps1
```

Fallback cleanup:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\stop_all.ps1 -KillByName -KillByPort
```

---

## Quick Start: CLS Service

Start CLS service:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_cls.ps1 -ExeDir .\out\build\x64-Debug
```

Check:

```powershell
curl.exe "http://127.0.0.1:8084/api/v1/health"
curl.exe "http://127.0.0.1:8084/api/v1/ready?model=cls&task_kind=image_async&worker_group=image_cls_gpu0"
curl.exe "http://127.0.0.1:8084/api/v1/workers?model=cls"
curl.exe "http://127.0.0.1:8084/api/v1/metrics?model=cls"
```

Submit a CLS task:

```powershell
curl.exe -X POST "http://127.0.0.1:8084/api/v1/classify/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Query result:

```powershell
$task_id = "replace_with_actual_task_id"
curl.exe "http://127.0.0.1:8084/api/v1/result/$task_id"
curl.exe "http://127.0.0.1:8084/api/v1/result/$task_id/image" --output cls_result.jpg
```

Run CLS tests:

```powershell
python tools\phase17_cls_registry_test.py --url http://127.0.0.1:8084
python tools\phase17_cls_smoke_test.py --url http://127.0.0.1:8084 --image .\images\bus.png
python tools\run_phase17_regression.py --skip-phase16
```

Stop CLS service:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\stop_cls.ps1
```

---

## Quick Start: Pose Service

Start Pose service:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_pose.ps1 -ExeDir .\out\build\x64-Debug
```

Check:

```powershell
curl.exe "http://127.0.0.1:8085/api/v1/health"
curl.exe "http://127.0.0.1:8085/api/v1/ready?model=pose&task_kind=image_async&worker_group=image_pose_gpu0"
curl.exe "http://127.0.0.1:8085/api/v1/workers?model=pose"
curl.exe "http://127.0.0.1:8085/api/v1/metrics?model=pose"
```

Submit a Pose task:

```powershell
curl.exe -X POST "http://127.0.0.1:8085/api/v1/pose/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Query result:

```powershell
$task_id = "replace_with_actual_task_id"
curl.exe "http://127.0.0.1:8085/api/v1/result/$task_id"
curl.exe "http://127.0.0.1:8085/api/v1/result/$task_id/image" --output pose_result.jpg
```

Run Pose tests:

```powershell
python tools\phase17_5_pose_registry_test.py --url http://127.0.0.1:8085
python tools\phase17_5_pose_smoke_test.py --url http://127.0.0.1:8085 --image .\images\bus.png
python tools\run_phase17_5_regression.py --skip-phase17
```

Stop Pose service:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\stop_pose.ps1
```

---

## Full Phase 17.5 Regression

Start all services:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_all.ps1 -ExeDir .\out\build\x64-Debug
powershell -ExecutionPolicy Bypass -File .\scripts\start_cls.ps1 -ExeDir .\out\build\x64-Debug
powershell -ExecutionPolicy Bypass -File .\scripts\start_pose.ps1 -ExeDir .\out\build\x64-Debug
```

Run full regression:

```powershell
python tools\run_phase17_5_regression.py
```

Expected final output:

```text
Phase 17.5 Regression Summary: success=True
[PASS] 01_phase17_5_pose_registry
[PASS] 02_phase17_5_pose_smoke
[PASS] 03_phase17_cls_regression
```

The Phase 17 CLS regression also runs Phase 16 full regression, so this validates:

```text
Detect / OBB / Video / Stream / CLS / Pose
```

Stop all services:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\stop_pose.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\stop_cls.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\stop_all.ps1
```

---

## Health and Metrics APIs

Each service exposes the same observation endpoints:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
curl.exe "http://127.0.0.1:8080/api/v1/ready"
curl.exe "http://127.0.0.1:8080/api/v1/workers"
curl.exe "http://127.0.0.1:8080/api/v1/metrics"
```

Use ports:

```text
8080 detect
8081 obb
8082 video
8083 stream
8084 cls
8085 pose
```

Expected key fields:

```text
success = true
ready = true
redis_ping = ok
redis_pending = 0
alive_workers >= 1
matched_workers >= 1
```

Capability-filtered examples:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/workers?model=detect&task_kind=image_async"
curl.exe "http://127.0.0.1:8081/api/v1/workers?model=obb&task_kind=image_async"
curl.exe "http://127.0.0.1:8082/api/v1/workers?model=video&task_kind=video_file"
curl.exe "http://127.0.0.1:8083/api/v1/workers?model=stream&task_kind=live_stream"
curl.exe "http://127.0.0.1:8084/api/v1/workers?model=cls&task_kind=image_async"
curl.exe "http://127.0.0.1:8085/api/v1/workers?model=pose&task_kind=image_async"
```

---

## HTTP APIs

| Method | Endpoint | Service | Description |
|---|---|---|---|
| GET | `/api/v1/health` | All | Health status |
| GET | `/api/v1/ready` | All | Readiness status |
| GET | `/api/v1/workers` | All | Worker heartbeat and capability metadata |
| GET | `/api/v1/metrics` | All | Runtime metrics |
| POST | `/api/v1/detect/image/async` | Detect | Submit async image detection task |
| POST | `/api/v1/detect/obb/async` | OBB | Submit async OBB image task |
| POST | `/api/v1/classify/image/async` | CLS | Submit async image classification task |
| POST | `/api/v1/pose/image/async` | Pose | Submit async image pose-estimation task |
| GET | `/api/v1/result/{task_id}` | Detect / OBB / CLS / Pose | Query image task result |
| GET | `/api/v1/result/{task_id}/image` | Detect / OBB / CLS / Pose | Download result image |
| POST | `/api/v1/detect/video/async` | Video | Submit async video detection task |
| GET | `/api/v1/video/result/{task_id}` | Video | Query video progress/result |
| GET | `/api/v1/video/result/{task_id}/file` | Video | Download result video |
| POST | `/api/v1/video/result/{task_id}/cancel` | Video | Cancel video task |
| POST | `/api/v1/video/result/{task_id}/cleanup` | Video | Cleanup local video files |
| POST | `/api/v1/stream/start` | Stream | Start one camera/file/RTSP stream |
| GET | `/api/v1/stream/{stream_id}/status` | Stream | Query stream status |
| GET | `/api/v1/stream/{stream_id}/snapshot` | Stream | Download latest snapshot |
| POST | `/api/v1/stream/{stream_id}/stop` | Stream | Stop stream |

---

## API Result Formats

### CLS result

```json
{
  "success": true,
  "status": "done",
  "model_type": "cls",
  "classification_format": "top1_and_topk",
  "top1": {
    "class_id": 608,
    "class_name": "class_608",
    "confidence": 0.2945369780063629
  },
  "topk": [
    {
      "rank": 1,
      "class_id": 608,
      "class_name": "class_608",
      "confidence": 0.2945369780063629
    }
  ],
  "queue_wait_ms": 2,
  "inference_ms": 13.2195,
  "total_ms": 30
}
```

`labels/imagenet.txt` currently may contain placeholder labels such as `class_0` to `class_999`. Replace it with a real ImageNet label file for production.

### Pose result

```json
{
  "success": true,
  "status": "done",
  "model_type": "pose",
  "pose_coordinate_system": "original_image_pixels",
  "keypoint_format": "coco17_xy_conf",
  "skeleton_format": "coco17_pairs",
  "num_poses": 3,
  "detections": [
    {
      "class_id": 0,
      "class_name": "person",
      "confidence": 0.9124361872673035,
      "bbox": {
        "x": 255,
        "y": 61,
        "w": 75,
        "h": 202
      },
      "keypoints": [
        {
          "id": 0,
          "name": "nose",
          "x": 289.95,
          "y": 79.12,
          "confidence": 0.9887,
          "visible": true,
          "in_image": true
        }
      ],
      "valid_keypoints": 16
    }
  ]
}
```

Pose coordinates are mapped back to original image pixels.

---

## Redis Validation

```powershell
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:obb yolo11_obb_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:video:detect yolo11_video_detect_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:cls yolo11_cls_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:pose yolo11_pose_group
```

Expected after a clean validation run:

```text
XPENDING = 0 for Detect / OBB / Video / Stream / CLS / Pose
```

---

## Model and Label Notes

Low-level TensorRT class count is configured in:

```text
include/config.h
```

Typical values:

```cpp
const static int kNumClass = 80;          // COCO Detect
constexpr static int kObbNumClass = 15;  // DOTA OBB labels used by this project
```

HTTP JSON class names are loaded from `labels_path`:

```yaml
model:
  labels_path: "./labels/coco.txt"      # detect / video / stream
  labels_path: "./labels/dota.txt"      # obb
  labels_path: "./labels/imagenet.txt"  # cls
  labels_path: "./labels/pose_coco.txt" # pose
```

Pose usually uses a single class label:

```text
person
```

---

## Troubleshooting

### `check_all.ps1` cannot find `redis-cli`

PowerShell may not have Redis CLI installed. Use WSL mode:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_all.ps1 -UseWslRedisCli -WslDistro Ubuntu
```

If `wsl redis-cli` enters Docker Desktop instead of Ubuntu, check distro names:

```powershell
wsl -l -v
```

Then use:

```powershell
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 ping
```

### WSL prints a localhost proxy warning

This warning can appear before Redis output:

```text
wsl: 检测到 localhost 代理配置，但未镜像到 WSL。NAT 模式下的 WSL 不支持 localhost 代理。
```

It is not a Redis failure if the command still returns `PONG` or `XPENDING=0`.

### Port already in use

```powershell
netstat -ano | findstr ":8080"
netstat -ano | findstr ":8081"
netstat -ano | findstr ":8082"
netstat -ano | findstr ":8083"
netstat -ano | findstr ":8084"
netstat -ano | findstr ":8085"
```

Fallback cleanup:

```powershell
Get-Process yolo11_server,yolo11_worker,yolo11_video_worker,yolo11_stream_worker -ErrorAction SilentlyContinue | Stop-Process -Force
```

### `Cannot open engine file`

Check `engine_path` in the relevant server/worker YAML. For local development, absolute paths are often safer.

### `labels_loaded=false`

Check the configured labels file:

```powershell
Test-Path D:\tensorrtx\yolo11\labels\coco.txt
Test-Path D:\tensorrtx\yolo11\labels\dota.txt
Test-Path D:\tensorrtx\yolo11\labels\imagenet.txt
Test-Path D:\tensorrtx\yolo11\labels\pose_coco.txt
```

### CLS returns `class_608`

That means the service is using placeholder ImageNet labels. Replace `labels/imagenet.txt` with a real ImageNet-1000 label file.

### Pose GPU postprocess

Pose service currently uses CPU post-processing because the original pose GPU postprocess path is not supported in this project. This is expected for Phase 17.5.

### Video `processed_frames` is lower than `total_frames`

OpenCV may report a container-estimated frame count. If the task status is `done`, `progress=1.0`, and the result video downloads correctly, the task completed normally at video EOF.

### Stream start returns `INVALID_JSON`

Use a JSON file and `--data-binary` instead of inline JSON in PowerShell.

---

## Release Tagging

After Phase 17.5 passes:

```powershell
git add .
git commit -m "phase17.5 add pose image async service and full regression pass"
git tag phase17_5_pose_pass
```

Archive:

```powershell
Compress-Archive -Path D:\tensorrtx\yolo11\* -DestinationPath D:\tensorrtx\yolo11_phase17_5_pose_pass.zip
```

---

## Roadmap

Current baseline:

```text
Phase 17.5: CLS + Pose async image services passed with full regression.
```

Recommended next phase:

```text
Phase 18.0: Seg async image service
```

Planned final engineering steps before full freeze:

```text
Phase 18.0: Seg image async service
Phase 18.5: Five-model stability regression and engineering freeze
Phase 19+: Real-world scenario research, dataset work, visual post-processing, and algorithm improvement
```

Out of scope before the final freeze unless a real scenario requires them:

```text
Multi-GPU automatic scheduler
Multi-stream RTSP platform
WebSocket / SSE push
Database persistence
Object storage integration
Docker / Linux production branch
Prometheus / Grafana monitoring
```

---

## Acknowledgement

Modified from `tensorrtx/yolo11`.
