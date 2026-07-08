# Config layout

The active runtime configs are kept at the top level of `config/`.
Old phase-specific YAML files are archived under `config/archive/legacy_phase_configs/`.

## Active configs

| File | Role | Port | Stream | Consumer group |
|---|---|---:|---|---|
| `server_detect.yaml` | Detect HTTP producer/query | 8080 | `yolo:stream:detect` | `yolo11_group` |
| `worker_detect.yaml` | Detect worker | - | `yolo:stream:detect` | `yolo11_group` |
| `server_obb.yaml` | OBB HTTP producer/query | 8081 | `yolo:stream:obb` | `yolo11_obb_group` |
| `worker_obb.yaml` | OBB worker | - | `yolo:stream:obb` | `yolo11_obb_group` |
| `server_video.yaml` | Detect video-file HTTP producer/query | 8082 | `yolo:stream:video:detect` | `yolo11_video_detect_group` |
| `worker_video.yaml` | Detect video-file worker | - | `yolo:stream:video:detect` | `yolo11_video_detect_group` |

`server.yaml` is kept as a backward-compatible default alias for `server_detect.yaml`.

## Runtime directory policy

Local transient files are under `runtime/`:

```text
runtime/
  input/images/detect
  input/images/obb
  input/videos/detect
  output/images/detect
  output/images/obb
  output/videos/detect
  logs/detect/server
  logs/detect/worker
  logs/obb/server
  logs/obb/worker
  logs/video/server
  logs/video/worker
```

Redis still stores task status, result JSON, heartbeats and metrics. Image async tasks use Redis binary keys for input/result images; video tasks use local files for input/output videos.
