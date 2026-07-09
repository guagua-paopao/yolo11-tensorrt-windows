# YOLO11 TensorRT Windows C++ Inference Service

Windows-based YOLO11 TensorRT deployment project using Visual Studio 2019, CUDA, TensorRT 10, OpenCV, Crow, hiredis, Redis Stream, spdlog, yaml-cpp, nlohmann/json, and reusable C++ runtime APIs.

Current baseline: **Phase 18 / 18.5 release-candidate baseline**. The project now provides YOLO11 five-model async image services (**Detect / OBB / CLS / Pose / Seg**), plus Detect video-file async service, single live-stream Detect service, Worker Capability Registry, health/readiness/metrics endpoints, Redis Stream queues, and unified start/check/regression/stop scripts.

This README is deployment-oriented. It focuses on environment setup, build, service startup, validation, APIs, and troubleshooting.

---

## Service Matrix

| Service | Port | Main API | Server Config | Worker Config | Worker Executable | Redis Stream | Consumer Group | Worker |
|---|---:|---|---|---|---|---|---|---|
| Detect image | 8080 | `/api/v1/detect/image/async` | `config/server_detect.yaml` | `config/worker_detect.yaml` | `yolo11_worker.exe` | `yolo:stream:detect` | `yolo11_group` | `worker_1` |
| OBB image | 8081 | `/api/v1/detect/obb/async` | `config/server_obb.yaml` | `config/worker_obb.yaml` | `yolo11_worker.exe` | `yolo:stream:obb` | `yolo11_obb_group` | `obb_worker_1` |
| Video file detect | 8082 | `/api/v1/detect/video/async` | `config/server_video.yaml` | `config/worker_video.yaml` | `yolo11_video_worker.exe` | `yolo:stream:video:detect` | `yolo11_video_detect_group` | `video_worker_1` |
| Live stream detect | 8083 | `/api/v1/stream/start` | `config/server_stream.yaml` | `config/worker_stream.yaml` | `yolo11_stream_worker.exe` | `yolo:stream:live:detect` | `yolo11_stream_detect_group` | `stream_worker_1` |
| CLS image | 8084 | `/api/v1/classify/image/async` | `config/server_cls.yaml` | `config/worker_cls.yaml` | `yolo11_worker.exe` | `yolo:stream:cls` | `yolo11_cls_group` | `cls_worker_1` |
| Pose image | 8085 | `/api/v1/pose/image/async` | `config/server_pose.yaml` | `config/worker_pose.yaml` | `yolo11_worker.exe` | `yolo:stream:pose` | `yolo11_pose_group` | `pose_worker_1` |
| Seg image | 8086 | `/api/v1/segment/image/async` | `config/server_seg.yaml` | `config/worker_seg.yaml` | `yolo11_worker.exe` | `yolo:stream:seg` | `yolo11_seg_group` | `seg_worker_1` |

---

## Current Capabilities

| Area | Status |
|---|---|
| YOLO11 command-line inference | Detect / OBB / CLS / Pose / Seg supported |
| Reusable C++ APIs | `Yolo11Detector`, `Yolo11ObbDetector`, `Yolo11ClsDetector`, `Yolo11PoseDetector`, `Yolo11SegDetector` |
| Pure C++ HTTP service | Crow-based `yolo11_server.exe` |
| Server / Worker process split | Supported |
| Redis Stream async queue | Supported with Consumer Groups, `XACK`, `XAUTOCLAIM`, `XPENDING`, and `XTRIM` |
| Redis binary image storage | Used by image tasks |
| Local video/stream runtime storage | Used by video result files and live-stream snapshots |
| Worker heartbeat and readiness | Supported through `yolo:worker:{consumer}:heartbeat` |
| Worker Capability Registry | Supported for Detect / OBB / Video / Stream / CLS / Pose / Seg |
| Metrics API | Per-service task counts, latency, recent QPS, worker distribution, Redis pending |
| Model output abstraction | `ModelOutput` supports bbox detections, OBB, classifications, pose keypoints, and segmentation mask/polygon metadata |
| Unified orchestration | `scripts/start_all.ps1`, `scripts/check_all.ps1`, `tools/run_phase18_regression.py`, `scripts/stop_all.ps1` |
| Final regression status | Phase 18 regression passed: Seg registry/smoke/invalid-input + Phase 17.5/17/16 regression chain |
| Out of scope before final freeze | Multi-GPU automatic scheduler, multi-stream RTSP platform, database/object storage, Docker/Linux production branch, WebSocket/SSE push |

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
| Phase 13.0 | Detect video-file async service minimal loop |
| Phase 13.5 | Video stability, cancellation, invalid input, Worker recovery, benchmark |
| Phase 14.0 | Live stream service minimal loop |
| Phase 14.5 | Stream stability, duplicate-start protection, TTL renewal, stale cleanup, reconnect/failure handling, stream metrics |
| Phase 15 | Worker Capability Registry and capability-filtered health / readiness / workers / metrics |
| Phase 16 | Engineering baseline: one-command start/check/regression/stop workflow, config templates, reports |
| Phase 17.0 | CLS async image service, `ModelOutput` abstraction, top1/topk JSON |
| Phase 17.5 | Pose async image service, COCO17 keypoints/skeleton JSON, full regression over Phase 17 + Phase 16 |
| Phase 18.0 | Seg async image service, mask/polygon metadata, overlay result image, Seg registry/smoke/invalid-input tests |
| Phase 18.5 | Seven-service validation and final engineering release-candidate baseline |

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

## Install Dependencies

Install C++ dependencies with vcpkg:

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install spdlog:x64-windows --vcpkg-root D:\vcpkg
```

Configure CMake with vcpkg:

```powershell
cd D:\tensorrtx\yolo11

cmake -S . -B out\build\x64-Debug `
  -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

If CUDA compiler detection fails, start a clean Visual Studio x64 environment first:

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
│   ├── yolo11_pose_api.cpp
│   └── yolo11_seg_api.cpp
├── config/
│   ├── server_detect.yaml / worker_detect.yaml
│   ├── server_obb.yaml    / worker_obb.yaml
│   ├── server_video.yaml  / worker_video.yaml
│   ├── server_stream.yaml / worker_stream.yaml
│   ├── server_cls.yaml    / worker_cls.yaml
│   ├── server_pose.yaml   / worker_pose.yaml
│   ├── server_seg.yaml    / worker_seg.yaml
│   ├── *.example.yaml
│   └── archive/
├── labels/
│   ├── coco.txt
│   ├── dota.txt
│   ├── imagenet.txt
│   └── pose_coco.txt
├── include/
│   ├── server/model_output.h
│   ├── server/model_runner.h
│   ├── server/result_serializer.h
│   ├── yolo11_detector_api.h
│   ├── yolo11_obb_api.h
│   ├── yolo11_cls_api.h
│   ├── yolo11_pose_api.h
│   └── yolo11_seg_api.h
├── src/server/
├── scripts/
│   ├── start_all.ps1
│   ├── stop_all.ps1
│   ├── check_all.ps1
│   ├── start_cls.ps1 / stop_cls.ps1
│   ├── start_pose.ps1 / stop_pose.ps1
│   ├── start_seg.ps1 / stop_seg.ps1
│   └── clean_runtime.ps1
├── tools/
│   ├── run_full_regression.py
│   ├── run_phase17_regression.py
│   ├── run_phase17_5_regression.py
│   ├── run_phase18_regression.py
│   ├── phase18_seg_registry_test.py
│   ├── phase18_seg_smoke_test.py
│   └── phase18_seg_invalid_input_test.py
├── runtime/
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

Build all required targets:

```powershell
cd D:\tensorrtx\yolo11

cmake --build out\build\x64-Debug --target myplugins --parallel 8
cmake --build out\build\x64-Debug --target yolo11_det --parallel 8
cmake --build out\build\x64-Debug --target yolo11_obb --parallel 8
cmake --build out\build\x64-Debug --target yolo11_cls --parallel 8
cmake --build out\build\x64-Debug --target yolo11_pose --parallel 8
cmake --build out\build\x64-Debug --target yolo11_seg --parallel 8
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

`myplugins.dll` must be in the same directory as the executables. The current CMake configuration should copy it automatically.

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
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 ping
```

Expected:

```text
PONG
```

If WSL IP changes, update `redis.host` in all active `config/server_*.yaml` and `config/worker_*.yaml` files.

---

## Model Engines

Default active configs expect these engine files:

```text
engines/yolo11n.engine
engines/yolo11n-obb.engine
engines/yolo11n-cls.engine
engines/yolo11n-pose.engine
engines/yolo11n-seg.engine
```

Generate engine files from `.wts` files when needed:

```powershell
# Detect
python gen_wts.py -w .\weights\yolo11n.pt -t detect -o .\engines\yolo11n.wts
.\out\build\x64-Debug\yolo11_det.exe -s .\engines\yolo11n.wts .\engines\yolo11n.engine n

# OBB
python gen_wts.py -w .\weights\yolo11n-obb.pt -t obb -o .\engines\yolo11n-obb.wts
.\out\build\x64-Debug\yolo11_obb.exe -s .\engines\yolo11n-obb.wts .\engines\yolo11n-obb.engine n

# CLS
python gen_wts.py -w .\weights\yolo11n-cls.pt -t cls -o .\engines\yolo11n-cls.wts
.\out\build\x64-Debug\yolo11_cls.exe -s .\engines\yolo11n-cls.wts .\engines\yolo11n-cls.engine n

# Pose
python gen_wts.py -w .\weights\yolo11n-pose.pt -t pose -o .\engines\yolo11n-pose.wts
.\out\build\x64-Debug\yolo11_pose.exe -s .\engines\yolo11n-pose.wts .\engines\yolo11n-pose.engine n

# Seg
python gen_wts.py -w .\weights\yolo11n-seg.pt -t seg -o .\engines\yolo11n-seg.wts
.\out\build\x64-Debug\yolo11_seg.exe -s .\engines\yolo11n-seg.wts .\engines\yolo11n-seg.engine n
```

After changing class count, model structure, TensorRT version, CUDA version, or GPU architecture, rebuild the project and regenerate the TensorRT engine. Do not reuse an incompatible `.engine` file.

---

## Quick Start: Seven-Service Release Candidate

### 1. Start all services

```powershell
cd D:\tensorrtx\yolo11

powershell -ExecutionPolicy Bypass `
  -File .\scripts\start_all.ps1 `
  -ExeDir .\out\build\x64-Debug
```

This starts 14 processes: seven workers and seven HTTP servers.

```text
Workers:
worker_1, obb_worker_1, video_worker_1, stream_worker_1,
cls_worker_1, pose_worker_1, seg_worker_1

Servers:
8080 detect, 8081 obb, 8082 video, 8083 stream,
8084 cls, 8085 pose, 8086 seg
```

### 2. Check all services

```powershell
powershell -ExecutionPolicy Bypass `
  -File .\scripts\check_all.ps1 `
  -UseWslRedisCli `
  -WslDistro Ubuntu
```

Expected:

```text
PASS: all services are ready and Redis pending looks clean.
```

The check must show `health=True`, `ready=True`, `workers=1`, `metrics=True`, and `pending=0` for all seven services.

### 3. Run Phase 18 full regression

```powershell
python .\tools\run_phase18_regression.py
```

Expected:

```text
Phase 18 Regression Summary: success=True
[PASS] 01_phase18_seg_registry
[PASS] 02_phase18_seg_smoke
[PASS] 03_phase18_seg_invalid_input
[PASS] 04_phase17_5_regression
```

The Phase 17.5 regression includes Pose, CLS, and the Phase 16 full regression for Detect / OBB / Video / Stream.

### 4. Stop all services

```powershell
powershell -ExecutionPolicy Bypass `
  -File .\scripts\stop_all.ps1 `
  -KillByPort
```

Confirm ports are released:

```powershell
netstat -ano | findstr ":8080"
netstat -ano | findstr ":8081"
netstat -ano | findstr ":8082"
netstat -ano | findstr ":8083"
netstat -ano | findstr ":8084"
netstat -ano | findstr ":8085"
netstat -ano | findstr ":8086"
```

---

## Single-Service Smoke Tests

### Detect image

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

### OBB image

```powershell
curl.exe -X POST "http://127.0.0.1:8081/api/v1/detect/obb/async" `
  -H "Content-Type: image/jpeg" `
  --data-binary "@D:/tensorrtx/yolo11/images/a.jpg"
```

### CLS image

```powershell
curl.exe -X POST "http://127.0.0.1:8084/api/v1/classify/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

### Pose image

```powershell
curl.exe -X POST "http://127.0.0.1:8085/api/v1/pose/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

### Seg image

```powershell
curl.exe -X POST "http://127.0.0.1:8086/api/v1/segment/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

A compatible alias may also be available:

```powershell
curl.exe -X POST "http://127.0.0.1:8086/api/v1/detect/seg/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

Query any image task result:

```powershell
$task_id = "replace_with_actual_task_id"
curl.exe "http://127.0.0.1:<port>/api/v1/result/$task_id"
curl.exe "http://127.0.0.1:<port>/api/v1/result/$task_id/image" --output result.jpg
```

### Video file detect

```powershell
curl.exe -X POST "http://127.0.0.1:8082/api/v1/detect/video/async" `
  -H "Content-Type: video/mp4" `
  --data-binary "@D:/tensorrtx/yolo11/videos/test.mp4"

curl.exe "http://127.0.0.1:8082/api/v1/video/result/<task_id>"
curl.exe "http://127.0.0.1:8082/api/v1/video/result/<task_id>/file" --output result_video.mp4
```

### Live stream detect

Use a JSON file to avoid PowerShell quote issues:

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

curl.exe "http://127.0.0.1:8083/api/v1/stream/<stream_id>/status"
curl.exe "http://127.0.0.1:8083/api/v1/stream/<stream_id>/snapshot" --output stream_snapshot.jpg
curl.exe -X POST "http://127.0.0.1:8083/api/v1/stream/<stream_id>/stop"
```

---

## Observation APIs

Each service exposes:

```powershell
curl.exe "http://127.0.0.1:<port>/api/v1/health"
curl.exe "http://127.0.0.1:<port>/api/v1/ready"
curl.exe "http://127.0.0.1:<port>/api/v1/workers"
curl.exe "http://127.0.0.1:<port>/api/v1/metrics"
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
curl.exe "http://127.0.0.1:8086/api/v1/workers?model=seg&task_kind=image_async"
```

---

## HTTP API Reference

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
| POST | `/api/v1/segment/image/async` | Seg | Submit async image segmentation task |
| POST | `/api/v1/detect/seg/async` | Seg | Compatible Seg async image endpoint |
| GET | `/api/v1/result/{task_id}` | Image services | Query image task result |
| GET | `/api/v1/result/{task_id}/image` | Image services | Download result image |
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

## Result Format Examples

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
    "confidence": 0.2945
  },
  "topk": [
    {
      "rank": 1,
      "class_id": 608,
      "class_name": "class_608",
      "confidence": 0.2945
    }
  ]
}
```

`labels/imagenet.txt` may contain placeholder labels such as `class_0` to `class_999`. Replace it with a real ImageNet label file for production.

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
      "confidence": 0.9124,
      "bbox": { "x": 255, "y": 61, "w": 75, "h": 202 },
      "keypoints": [
        { "id": 0, "name": "nose", "x": 289.95, "y": 79.12, "confidence": 0.9887, "visible": true }
      ],
      "valid_keypoints": 16
    }
  ]
}
```

### Seg result

```json
{
  "success": true,
  "status": "done",
  "model_type": "seg",
  "segmentation_coordinate_system": "original_image_pixels",
  "segmentation_format": "bbox_polygon_mask_metadata",
  "mask_format": "thresholded_polygon_metadata",
  "num_segmentations": 3,
  "segmentations": [
    {
      "class_id": 0,
      "class_name": "person",
      "confidence": 0.8921,
      "bbox": { "x": 254, "y": 60, "w": 75, "h": 203, "x1": 254, "y1": 60, "x2": 329, "y2": 263 },
      "mask": {
        "coordinate_system": "original_image_pixels",
        "threshold": 0.5,
        "area_pixels": 7418,
        "has_polygon": true,
        "bbox": { "x1": 254, "y1": 58, "x2": 330, "y2": 264 },
        "polygon": [[275, 58], [254, 132], [274, 200]],
        "polygons": [[[275, 58], [254, 132], [274, 200]]]
      }
    }
  ],
  "result_image_url": "/api/v1/result/seg_xxx/image"
}
```

Seg first-stage service returns bbox + polygon/mask metadata and an overlay result image. It does not return full raw mask matrices in JSON.

---

## Redis Validation

```powershell
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:obb yolo11_obb_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:video:detect yolo11_video_detect_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:cls yolo11_cls_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:pose yolo11_pose_group
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:seg yolo11_seg_group
```

Expected after a clean validation run:

```text
XPENDING = 0 for Detect / OBB / Video / Stream / CLS / Pose / Seg
```

---

## Troubleshooting

### `check_all.ps1` cannot find `redis-cli`

Use WSL mode:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_all.ps1 -UseWslRedisCli -WslDistro Ubuntu
```

### WSL prints a localhost proxy warning

This warning can appear before Redis output:

```text
wsl: 检测到 localhost 代理配置，但未镜像到 WSL。NAT 模式下的 WSL 不支持 localhost 代理。
```

It is not a Redis failure if the command still returns `PONG` or `XPENDING=0`.

### Port already in use

```powershell
netstat -ano | findstr ":8086"
taskkill /PID <PID> /F
```

Or use the project cleanup script:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\stop_all.ps1 -KillByPort
```

### `Cannot open engine file`

Check the active YAML file and confirm the engine exists:

```powershell
Test-Path .\engines\yolo11n-seg.engine
Get-Content .\config\worker_seg.yaml
```

### `labels_loaded=false`

Check the configured labels file:

```powershell
Test-Path .\labels\coco.txt
Test-Path .\labels\dota.txt
Test-Path .\labels\imagenet.txt
Test-Path .\labels\pose_coco.txt
```

### `model.type=seg` is rejected by server or worker

Make sure the Phase 18 code was built. `src/server/main_server.cpp` and `src/server/main_worker.cpp` must allow `model.type=seg`, and `yolo11_server.exe` / `yolo11_worker.exe` must be rebuilt:

```powershell
cmake --build .\out\build\x64-Debug --target yolo11_server yolo11_worker --config Debug
```

### CLS returns `class_608`

That means the service is using placeholder ImageNet labels. Replace `labels/imagenet.txt` with a real ImageNet-1000 label file.

### Pose GPU postprocess

Pose service currently uses CPU post-processing because the original pose GPU postprocess path is not supported in this project. This is expected for the current service baseline.

### Video `processed_frames` is lower than `total_frames`

OpenCV may report a container-estimated frame count. If the task status is `done`, `progress=1.0`, and the result video downloads correctly, the task completed normally at video EOF.

### Stream start returns `INVALID_JSON`

Use a JSON file and `--data-binary` instead of inline JSON in PowerShell.

---

## Release Tagging

After Phase 18 validation passes:

```powershell
git add .
git commit -m "phase18 add seg image async service and seven-service regression pass"
git tag phase18_5_final_engineering_freeze
```

Archive:

```powershell
Compress-Archive -Path D:\tensorrtx\yolo11\* -DestinationPath D:\tensorrtx\yolo11_phase18_5_final_engineering_freeze.zip
```

---

## Roadmap

Current baseline:

```text
Phase 18.5: Detect / OBB / CLS / Pose / Seg image services, Video file service, Stream service, and seven-service regression passed.
```

Recommended next stage:

```text
Phase 19+: Real-world scenario research, dataset construction, visual post-processing, algorithm improvement, and deployment trade-off studies.
```

Do not expand the platform before the first real scenario requires it. The following should remain out of scope unless a concrete project needs them:

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
