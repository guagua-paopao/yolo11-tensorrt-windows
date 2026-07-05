# YOLO11 TensorRT Windows

Windows-based YOLO11 TensorRT deployment with Visual Studio 2019, CUDA, TensorRT 10, OpenCV, reusable C++ runtime APIs, and a pure C++ HTTP detection server.

Current service phase:

```text
Phase 2: Crow HTTP Server + Redis Stream async image detection queue
```

This README focuses on deployment and runtime steps. For development notes and design explanations, see `readme_CN.md`.

---

## 1. Current Status

| Area | Status |
|---|---|
| YOLO11 detection TensorRT CLI | Supported |
| YOLO11 OBB TensorRT CLI | Supported |
| C++ detection runtime API | Supported |
| C++ OBB runtime API | Supported, CPU post-processing verified |
| Synchronous HTTP image detection | Supported |
| Redis Stream async image detection | Supported |
| Result JSON query by `task_id` | Supported |
| Result image HTTP access | Supported |
| OBB / Seg / Pose HTTP service | Planned |
| Separate `yolo11_worker.exe` process | Planned next |

---

## 2. Runtime Architecture

Current Phase 2 runs HTTP service and async worker inside one executable:

```text
Client
  |
  | POST /api/v1/detect/image/async
  v
yolo11_server.exe
  |-- Crow HTTP Server
  |-- RedisTaskQueue
  |-- local async worker thread
  |-- Yolo11Detector
  v
TensorRT Engine + CUDA + OpenCV
```

Redis is used for task queue and result storage:

```text
XADD yolo:stream:detect
GET/SET yolo:task:{task_id}:status
HSET    yolo:task:{task_id}:meta
SET     yolo:task:{task_id}:result
```

Task states:

```text
queued -> running -> done
queued -> running -> failed
```

---

## 3. Tested Environment

| Dependency | Version / Path |
|---|---|
| OS | Windows + WSL Ubuntu |
| IDE | Visual Studio 2019 |
| CUDA | `D:\GPU13.3` |
| cuDNN | `D:\cuDNN9\bin\13.3\x64` |
| TensorRT | `D:\TensorRT-10.16.1.11` |
| OpenCV | `D:\libs\opencv\build` |
| vcpkg | `D:\vcpkg` |
| Python | 3.12 |
| GPU | RTX 4080 Laptop GPU |
| CUDA Architecture | `sm_89` |
| Redis | WSL Redis 8.2.1 |

Runtime DLL paths should be available in `PATH`:

```powershell
$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"
```

---

## 4. Install C++ Dependencies

Install HTTP, JSON, YAML, and Redis client dependencies through vcpkg:

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
```

Required CMake packages:

```cmake
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
```

Required server link libraries:

```cmake
target_link_libraries(yolo11_server PRIVATE
    yolo11_runtime
    myplugins
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    Crow::Crow
    hiredis::hiredis
)

if(WIN32)
    target_link_libraries(yolo11_server PRIVATE ws2_32)
endif()
```

---

## 5. Redis Setup

Recommended Redis for this project: WSL Ubuntu Redis.

Start and verify Redis in WSL:

```bash
sudo service redis-server start
redis-cli ping
hostname -I
sudo ss -lntp | grep 6379
```

Expected:

```text
PONG
LISTEN 0 511 0.0.0.0:6379
```

Example WSL Redis address used during testing:

```text
172.19.196.109:6379
```

Verify from Windows PowerShell:

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

If Windows has another Redis on `127.0.0.1:6379`, disable it to avoid confusion:

```powershell
Get-Service Redis
Stop-Service Redis
Set-Service Redis -StartupType Disabled
```

---

## 6. `config/server.yaml`

Example Phase 2 config:

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

Use an absolute `engine_path` when possible.

---

## 7. Build

Open the project folder with Visual Studio 2019:

```text
File -> Open -> Folder -> D:\tensorrtx\yolo11
```

If CMake cache is stale, delete:

```powershell
cd D:\tensorrtx\yolo11
rmdir /s /q out\build\x64-Debug
```

Build target:

```powershell
cmake --build out\build\x64-Debug --config Debug --target yolo11_server
```

Executable path:

```text
D:\tensorrtx\yolo11\out\build\x64-Debug\yolo11_server.exe
```

`myplugins.dll` must be in the same directory as `yolo11_server.exe`.

---

## 8. Run Server

```powershell
cd D:\tensorrtx\yolo11\out\build\x64-Debug
.\yolo11_server.exe D:\tensorrtx\yolo11\config\server.yaml
```

Expected startup log:

```text
Yolo11Detector initialized successfully.
Redis queue enabled: 172.19.196.109:6379, stream=yolo:stream:detect, group=yolo11_group
YOLO11 server started.
Queue backend: Redis Stream
Async inference worker started. backend=redis_stream
```

---

## 9. API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/v1/health` | Health check, model config, Redis status |
| POST | `/api/v1/detect/image` | Synchronous image detection |
| POST | `/api/v1/detect/image?debug=true` | Sync detection with raw bbox debug info |
| POST | `/api/v1/detect/image/async` | Submit async image detection task |
| GET | `/api/v1/result/{task_id}` | Query task status and result JSON |
| GET | `/api/v1/image/{filename}` | Read saved result image |

---

## 10. Health Check

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
```

Expected key fields:

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

---

## 11. Synchronous Detection

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

---

## 12. Async Detection

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

Expected final status:

```json
{
  "success": true,
  "status": "done",
  "num_detections": 3,
  "queue_backend": "redis_stream",
  "result_image_url": "/api/v1/image/20260705_214026_1_result.jpg"
}
```

Download result image:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/image/20260705_214026_1_result.jpg" -o redis_async_result.jpg
start redis_async_result.jpg
```

---

## 13. Redis Validation Commands

Enter Redis CLI in WSL:

```bash
redis-cli -h 172.19.196.109 -p 6379
```

Check YOLO keys:

```redis
KEYS yolo:*
XRANGE yolo:stream:detect - +
GET yolo:task:20260705_214026_1:status
GET yolo:task:20260705_214026_1:result
HGETALL yolo:task:20260705_214026_1:meta
XINFO GROUPS yolo:stream:detect
XINFO CONSUMERS yolo:stream:detect yolo11_group
```

Expected status:

```text
"done"
```

---

## 14. Simple Concurrent Submit Test

```powershell
for ($i=1; $i -le 5; $i++) {
  curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
    -H "Content-Type: image/png" `
    --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
}
```

Then check each returned `task_id`:

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>"
```

---

## 15. Troubleshooting

### `ERR unknown command 'XGROUP'`

Usually means the program is connected to the wrong Redis instance, often Windows local Redis instead of WSL Redis.

Check Windows Redis:

```powershell
netstat -ano | findstr :6379
Get-Process -Id <PID>
```

Check WSL Redis:

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

Use the Redis instance whose `INFO server` shows:

```text
redis_version:8.2.1
os:Linux ... WSL2
executable:/usr/bin/redis-server
```

### `Fatal error: failed to connect Redis: connection refused`

Redis is not listening at the configured IP/port, or Redis was restarting.

```bash
sudo service redis-server status
sudo service redis-server start
redis-cli ping
hostname -I
```

Update `redis.host` in `config/server.yaml` if the WSL IP changed.

### `redis_error: timed out`

Check network and Redis response:

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

Also check that Windows firewall or another Redis service is not interfering.

### `timeval` or hiredis compile errors on Windows

Include WinSock before hiredis in `redis_task_queue.cpp`:

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

And link `ws2_32` in CMake.

### `Cannot open engine file`

Check `engine_path` in `config/server.yaml`. Prefer absolute path.

### `Failed to load myplugins.dll`

Make sure `myplugins.dll` is in the same directory as the executable.

---

## 16. Git Ignore

Do not commit generated files:

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

## 17. Roadmap

Next stage:

```text
Phase 3: split HTTP server and inference worker
```

Target architecture:

```text
yolo11_server.exe
  |-- HTTP upload
  |-- Redis Stream producer
  |-- result query API

yolo11_worker.exe
  |-- Redis Stream consumer
  |-- TensorRT inference
  |-- result writer
```

Future extensions:

- Multiple worker processes
- Multi-GPU scheduling
- OBB HTTP service
- Segmentation / Pose service APIs
- Video file async detection
- RTSP / camera stream service
- Redis + object storage / database productionization

---

## Acknowledgement

Modified from `tensorrtx/yolo11`.
