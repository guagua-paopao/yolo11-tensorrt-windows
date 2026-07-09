# YOLO11 TensorRT Windows

本项目用于在 Windows 平台部署 YOLO11 TensorRT 推理，支持 Visual Studio 2019、CUDA、TensorRT 10、OpenCV、可复用 C++ Runtime API、纯 C++ HTTP 图片检测服务、Redis Stream 异步任务队列、Server / Worker 独立进程部署。当前项目已经推进到 Phase 17.5，已在 Phase 16 工程封板基线上继续接入 YOLO11 Classification 图片异步服务和 YOLO11 Pose 图片异步服务。当前系统已经支持 Detection 图片异步服务、OBB 图片异步服务、CLS 图片异步服务、POSE 图片异步服务、Detect 视频文件异步检测服务、单路 RTSP / 摄像头 / 本地文件流任务服务化，以及 Detect / OBB / Video / Stream / CLS / Pose 六类 Worker 的能力注册与统一观测。项目已经具备 Worker 心跳、ready 检查、Redis Binary 图片存储、视频本地文件存储、流任务状态管理、最新帧 snapshot 输出、TTL 清理、Pending 恢复、日志、labels_path、metrics、多模型 Runner 抽象、ModelOutput 统一输出抽象、视频任务进度、取消、异常输入拒绝、Worker 恢复、重复启动保护、流任务重连失败处理、批量压测、统一 runtime 目录管理，以及 `model_type / runner_model_type / worker_group / worker_kind / task_kind / stream_type / gpu_id` 等 Worker capability 字段。

这个 README 中文版主要用于记录项目演进过程、踩坑原因、修改逻辑和阶段性反思。

## 当前阶段结论

当前项目已经完成：

```text
Phase 17.5：POSE 图片异步服务接入与全量回归通过
```


最终补充说明：当前 Phase 17.5 已完成 POSE 图片异步服务接入，并在 Phase 17 CLS 图片异步服务基础上通过了完整回归。当前系统已经形成 Detect / OBB / CLS / POSE 四类图片异步服务，加上 Video 文件异步服务和 Stream 实时流服务。Phase 17.5 最终验收中，POSE 服务 `health=true`、`ready=true`、`workers=1`、`metrics=true`，`/api/v1/pose/image/async` 可返回 `bbox + COCO17 keypoints + skeleton`，Redis `XPENDING yolo:stream:pose yolo11_pose_group = 0`，POSE metrics 显示 `total_done=3`、`total_failed=0`。随后执行 `run_phase17_5_regression.py`，串联 POSE、CLS 与 Phase 16 Detect / OBB / Video / Stream 全量回归，最终 `Phase 17.5 Regression Summary: success=True`。

最终补充说明：Phase 17.0 已完成 CLS 图片异步服务接入。CLS 独立运行于 8084 端口，使用 `yolo:stream:cls` 和 `yolo11_cls_group`，支持 `POST /api/v1/classify/image/async`、`GET /api/v1/result/{task_id}` 和 `GET /api/v1/result/{task_id}/image`。结果 JSON 返回 `top1 / topk`，并写入 cls 维度 metrics。Phase 17.0 的关键不是单独接一个分类接口，而是引入 `ModelOutput` 最小抽象，让 Detect / OBB / CLS / Pose 后续可以在统一 Worker 与 ResultSerializer 框架下输出不同结构，避免所有模型都硬塞进 `std::vector<Detection>`。

最终补充说明：当前 Phase 16 已完成工程封板与上线前整理。项目已经具备 `start_all.ps1 -> check_all.ps1 -> run_full_regression.py -> stop_all.ps1` 的标准闭环；Detect / OBB / Video / Stream 四类服务可一键启动、统一检查、全量回归和一键停止。最终验收中，四类服务 `health=true`、`ready=true`、`workers=1`、`metrics=true`，Redis `PING=PONG`，四条 Redis Stream 的 `XPENDING=0`，全量回归 `success=True`。Phase 16 不修改 C++ 核心推理链路，因此旧服务 `/health` 中的 `phase=phase15_worker_capability_registry` 属于预期现象。

最终补充说明：当前 Phase 15 已完成 Detect / OBB / Video / Stream 四类服务的 Worker Capability Registry。`/health`、`/ready`、`/workers`、`/metrics` 已能按 `model`、`task_kind`、`worker_group`、`worker_kind`、`stream_type`、`gpu_id` 等维度过滤和观测 Worker。四类服务均已完成真实任务验证：Detect 图片任务 done，OBB 图片任务 done，Video 文件任务 done 并可下载结果视频，Stream 摄像头流完成 start / snapshot / stop / metrics。最终四条 Redis Stream 均 `XPENDING=0`。


已经验证通过的能力：

- 原始 `yolo11_det.exe` Detection 命令行推理
- 原始 `yolo11_obb.exe` OBB 命令行推理
- `Yolo11Detector` C++ API 封装
- `Yolo11ObbDetector` C++ API 封装
- `demo_image.exe` 图片检测
- `demo_video.exe` 视频 / 摄像头检测
- `demo_obb_image.exe` OBB 图片检测
- `yolo11_server.exe` 常驻 HTTP 服务
- `GET /api/v1/health` 健康检查
- `POST /api/v1/detect/image` 同步图片检测
- `GET /api/v1/image/<filename>` 结果图访问
- `POST /api/v1/detect/image/async` Redis 异步图片检测任务提交
- `GET /api/v1/result/{task_id}` 异步任务状态与结果查询
- Redis Stream Consumer Group 多 Worker 推理池
- `worker_num=3` 多线程 TensorRT 推理实例
- 任务性能指标记录：`queue_wait_ms`、`inference_ms`、`total_ms`
- Pending 任务 `XAUTOCLAIM` 恢复机制
- Redis Stream `XTRIM MAXLEN ~` 长度控制
- `tools/benchmark_async.py` 异步压测脚本
- Redis 连接复用：HTTP Producer 与每个 Worker 复用长期 `redisContext`
- Redis 命令失败后自动 reconnect 一次
- Phase 6 极限压测 `3000 tasks / concurrency=20` 全部完成，Failed=0，Timeout=0
- Redis Stream 任务入队、Worker 消费、结果写回 Redis
- Redis 中保存 `status`、`meta`、`result` 三类任务信息
- JSON 返回原图像素坐标 bbox
- JSON 返回 `class_name`
- 普通模式不返回调试坐标
- `?debug=true` 时返回 `raw_model_bbox`
- `yolo11_worker.exe` 独立 Worker 进程
- `yolo11_server.exe` 与 `yolo11_worker.exe` 分离部署
- Redis Binary 图片存储：`yolo:image:{task_id}:input` 与 `yolo:image:{task_id}:result`
- 新增结果图访问接口：`GET /api/v1/result/{task_id}/image`
- Phase 7 标准压测：`1000 tasks / concurrency=10`，`1000/0/0`，QPS=68.261
- Phase 7 极限压测：`3000 tasks / concurrency=20`，`3000/0/0`，QPS=60.948
- Worker 强杀恢复测试：`5000 tasks / concurrency=20`，中途终止一个 Worker，最终 `5000/0/0`
- Debug 版本下 `Ctrl+C` 结束 Worker 的 Visual C++ Runtime abort 弹窗修复
- Phase 8 健康检查增强：新增 `/api/v1/ready` 和 `/api/v1/workers`
- Phase 8 Worker 心跳：`yolo:worker:{consumer_name}:heartbeat`，支持 TTL 自动过期
- Phase 8 Redis 图片 TTL：input image 完成后删除，result image 按 TTL 保存
- Phase 8 Redis 内存保护：超过 `max_redis_used_memory_mb` 时 `/ready=false`
- Phase 8 Worker 离线保护：Worker 不足时异步提交返回 HTTP 503，不再继续入队
- Phase 8 修正任务确认逻辑：`markDone/markFailed` 成功后才 `XACK`
- Phase 8 标准压测：`1000 tasks / concurrency=10`，`1000/0/0`，QPS=60.373
- Phase 8.5 引入 spdlog：`logs/server.log` 和 `logs/worker.log` 独立写入
- Phase 8.5 新增 `labels_path`：`class_name` 从 `labels/coco.txt` 加载，不再依赖硬编码数组
- Phase 8.5 新增 `/api/v1/metrics`：统计累计任务、失败数、近期 QPS、平均耗时和 Worker 分布
- Phase 8.5 标准压测：`1000 tasks / concurrency=10`，`1000/0/0`，QPS=59.177
- Phase 10.0 OBB 图片异步服务最小闭环：新增 `POST /api/v1/detect/obb/async`
- Phase 10.0 OBB 独立队列：`yolo:stream:obb` + `yolo11_obb_group`
- Phase 10.0 OBB Worker：`yolo11_worker.exe` 可通过 `model.type=obb` 加载 `Yolo11ObbDetector`
- Phase 10.0 OBB 结果 JSON：返回 `obb_coordinate_system`、`obb_format`、`points`、`angle`、`bbox_axis_aligned` 等字段
- Phase 10.0 OBB 结果图：`GET /api/v1/result/{task_id}/image` 可返回旋转框结果图
- Phase 10.5 多模型配置整理：新增 `server_multimodel.yaml`、`worker_detect.yaml`、`worker_obb.yaml`
- Phase 10.5 新增 `IModelRunner` 抽象，Worker 不再直接依赖具体 Detector 分支
- Phase 10.5 新增 `DetectModelRunner` / `ObbModelRunner`，为后续更多模型类型扩展做铺垫
- Phase 10.5 `/ready`、`/workers`、`/metrics` 支持 `?model=detect` / `?model=obb` 过滤
- Phase 11.0 OBB 稳定性测试：100 tasks `100/0/0`，QPS≈33.047
- Phase 11.0 OBB 稳定性测试：500 tasks `500/0/0`，QPS≈34.336
- Phase 11.0 非法输入测试：HTTP 400 明确拒绝，`PASS_HTTP_REJECTED`
- Phase 11.0 Worker 恢复测试：中断 Worker 后重启，100 个任务最终全部 done
- Phase 12.0 Detect + OBB 双服务并行：Detect Server 运行于 8080，OBB Server 运行于 8081
- Phase 12.0 Detect + OBB 独立 Worker：`worker_1` 与 `obb_worker_1` 同时在线
- Phase 12.0 双服务 smoke test：Detect 与 OBB 均可提交、查询和下载结果图
- Phase 12.0 双服务 benchmark：Detect 50/50 done，OBB 50/50 done，failed=0，timeout=0，QPS≈31.866
- Phase 13.0 Detect 视频文件异步服务最小闭环：新增 `POST /api/v1/detect/video/async`
- Phase 13.0 视频任务查询：新增 `GET /api/v1/video/result/{task_id}`，支持 `queued/running/done/failed` 状态查询
- Phase 13.0 结果视频下载：新增 `GET /api/v1/video/result/{task_id}/file`
- Phase 13.0 视频任务独立队列：`yolo:stream:video:detect` + `yolo11_video_detect_group`
- Phase 13.0 视频 Worker：新增 `yolo11_video_worker.exe`，负责逐帧读取视频、调用 Detect Runner、绘制检测框并写出结果视频
- Phase 13.0 视频存储策略：视频不放 Redis Binary，采用本地文件保存；Redis 只保存任务状态、进度、路径、耗时和结果元信息
- Phase 13.5 视频取消：新增 `POST /api/v1/video/result/{task_id}/cancel`，支持 running 任务取消
- Phase 13.5 视频清理：新增 `POST /api/v1/video/result/{task_id}/cleanup`，用于删除已完成任务的本地 input/output 视频文件
- Phase 13.5 异常输入测试：非法视频 HTTP 400 拒绝，`VIDEO_OPEN_FAILED`，不会进入 Worker
- Phase 13.5 视频批量压测：20 tasks / concurrency=2，done=20，failed=0，timeout=0
- Phase 13.5 Worker 恢复测试：提交 3 个视频任务，中断并重启 Worker 后全部 done
- Phase 13.5 Redis 验收：`XPENDING=0`，`video_worker_1 pending=0`
- 项目结构整理：config 根目录只保留当前配置，旧 phase YAML 归档到 `config/archive/legacy_phase_configs/`
- runtime 目录统一：Detect / OBB / Video 的 input、output、logs 统一归入 `runtime/`
- Phase 14.0 单路流任务最小闭环：新增 `POST /api/v1/stream/start`
- Phase 14.0 流任务状态查询：新增 `GET /api/v1/stream/{stream_id}/status`
- Phase 14.0 最新帧截图访问：新增 `GET /api/v1/stream/{stream_id}/snapshot`
- Phase 14.0 流任务停止：新增 `POST /api/v1/stream/{stream_id}/stop`
- Phase 14.0 新增 `yolo11_stream_worker.exe`，负责打开 camera/file/RTSP source、逐帧推理、写出最新 snapshot
- Phase 14.0 流任务独立队列：`yolo:stream:live:detect` + `yolo11_stream_detect_group`
- Phase 14.0 流任务本地 snapshot 存储：`runtime/output/streams/{stream_id}/snapshot.jpg`
- Phase 14.0 本地摄像头测试通过：`camera_id=0`，`running -> snapshot -> stopped`，Redis `XPENDING=0`
- Phase 14.5 重复启动保护：已有 active stream 时再次 start 返回 HTTP 409，`STREAM_ALREADY_ACTIVE`
- Phase 14.5 新增流任务重连状态：`reconnecting`，并记录 `reconnect_count`、`no_frame_count`、`last_error`
- Phase 14.5 无效摄像头测试：`camera_id=99` 触发 3 次重连后进入 `failed`
- Phase 14.5 稳定性脚本：`tools/phase14_5_stream_stability_test.py`
- Phase 14.5 三轮 start / snapshot / stop 测试通过，Worker 最终回到 `idle`
- Phase 14.5 Redis 验收：`XPENDING=0`，`stream_worker_1 pending=0`，active key 为 `nil`
- Phase 14.5 状态语义统一：`/stream/start` 最终返回 `status=queued`、`lifecycle_status=queued`，旧日志里的 `created` 仅作为历史记录保留
- Phase 14.5 stale stream cleanup：Worker 崩溃或 active lock 残留时，Server 可根据 heartbeat/current_task_id/last_update_ms 判断 stale 并释放单活锁
- Phase 14.5 stream metrics 验收：正常 camera stop 后 `done_count+1`，无效 camera failed 后 `failed_count+1`，`/metrics?model=stream` 返回 `metrics_found=true`
- Phase 15 Worker Capability Registry：新增统一 Worker 能力声明，用于描述 Worker 能处理什么任务、属于哪个分组、绑定哪个 GPU、当前状态是什么
- Phase 15 heartbeat 字段扩展：新增 `model_type`、`runner_model_type`、`worker_group`、`worker_kind`、`task_kind`、`stream_type`、`engine_path`、`labels_path`、`max_concurrency`
- Phase 15 `/workers` 过滤增强：支持 `?model=detect/obb/video/stream`、`?task_kind=image_async/video_file/live_stream`、`?worker_group=...`
- Phase 15 `/ready` 过滤增强：可按 `worker_group` 判断某一类服务是否可接收任务
- Phase 15 `/metrics` 维度增强：metrics 返回 `models.detect/obb/video/stream` 分组，并携带 `worker_group`、`worker_kind`、`task_kind`、`stream_type`
- Phase 15 Detect 验收：8080 `model_type=detect`、`runner_model_type=detect`、`worker_group=image_detect_gpu0`、异步图片任务 done、结果图可下载、`XPENDING=0`
- Phase 15 OBB 验收：8081 `model_type=obb`、`runner_model_type=obb`、`worker_group=image_obb_gpu0`、OBB 图片任务 done、结果图可下载、`XPENDING=0`
- Phase 15 Video 验收：8082 `model_type=video`、`runner_model_type=detect`、`worker_group=video_detect_gpu0`、视频任务 done、结果视频可下载、`XPENDING=0`
- Phase 15 Stream 验收：8083 `model_type=stream`、`runner_model_type=detect`、`worker_group=stream_detect_gpu0`、`task_kind=live_stream`、`stream_type=long_running_stream`、camera stream stopped、snapshot 可下载、metrics `total_done=1`、`XPENDING=0`
- Phase 15 修复 Video heartbeat 语义：Video Worker 对外应声明 `model_type=video`，底层推理模型为 `runner_model_type=detect`
- Phase 15 修复 Stream API 语义：`/stream/start` 和 `/stream/status` 的对外 `task_kind` 统一为 `live_stream`



- Phase 16 工程封板脚本：新增 `scripts/start_all.ps1`，可统一启动 Detect / OBB / Video / Stream 的 4 个 Server 与 4 个 Worker
- Phase 16 停止脚本：新增并修复 `scripts/stop_all.ps1`，支持根据 PID 文件停止服务，并支持 `-KillByName`、`-KillByPort` 兜底清理
- Phase 16 检查脚本：新增并修复 `scripts/check_all.ps1`，统一检查四个端口的 `/health`、`/ready`、`/workers`、`/metrics` 以及 Redis `PING` 和四条 Stream 的 `XPENDING`
- Phase 16 支持 WSL Redis CLI：`check_all.ps1 -UseWslRedisCli -WslDistro Ubuntu` 可避免 Windows 没有 `redis-cli` 或默认 WSL 指向 `docker-desktop` 的问题
- Phase 16 全量回归入口：新增 `tools/run_full_regression.py`，串联 Phase 12 / 13 / 14 / 15 的核心 smoke、cancel、source matrix 和 registry 测试
- Phase 16 报告归档：全量回归报告写入 `reports/phase16/<timestamp>/phase16_full_regression_summary.json`
- Phase 16 PID 与进程日志归档：启动后生成 `runtime/pids/phase16_services.json`，进程 stdout / stderr 写入 `runtime/logs/phase16_process/`
- Phase 16 配置模板整理：新增 `config/*.example.yaml`，让 Redis host、engine path、labels path、runtime path 等部署字段更容易迁移
- Phase 16 最终验收：`check_all.ps1` 返回 PASS，`run_full_regression.py` 返回 `success=True`，`stop_all.ps1` 正常完成
- Phase 17.0 ModelOutput 最小抽象：新增 `include/server/model_output.h`，让模型输出从单一 `std::vector<Detection>` 升级为可承载 detections / classifications / keypoints 等多类型结果的统一结构
- Phase 17.0 CLS API 封装：新增 `Yolo11ClsDetector`，复用原始 `yolo11_cls.cpp` 的 TensorRT 分类推理逻辑，并服务化为可被 Worker 调用的 Runtime API
- Phase 17.0 CLS Runner：新增 `ClsModelRunner`，接入 `IModelRunner` 工厂体系，支持 `model.type=cls`
- Phase 17.0 CLS 序列化：新增 `ResultSerializer::serializeCls`，返回 `classification_format=top1_and_topk`、`top1`、`topk`、`num_classifications`
- Phase 17.0 CLS 独立配置：新增 `config/server_cls.yaml`、`config/worker_cls.yaml`，服务端口 8084，Redis Stream 为 `yolo:stream:cls`，Consumer Group 为 `yolo11_cls_group`
- Phase 17.0 CLS 独立脚本：新增 `scripts/start_cls.ps1` 与 `scripts/stop_cls.ps1`
- Phase 17.0 CLS 自动测试：新增 `tools/phase17_cls_registry_test.py`、`tools/phase17_cls_smoke_test.py`、`tools/run_phase17_regression.py`
- Phase 17.0 CLS 验收：`/health`、`/ready`、`/workers`、`/metrics` 正常，`POST /api/v1/classify/image/async` 返回 queued，结果查询返回 done + top1/topk，`XPENDING=0`，`run_phase17_regression.py --skip-phase16` 返回 `success=True`
- Phase 17.0 CLS metrics 验收：`metrics_found=true`，`total_tasks=3`，`total_tasks_done=3`，`total_tasks_failed=0`，`worker_distribution.cls_worker_1=3`
- Phase 17.5 POSE API 封装：新增 `Yolo11PoseDetector`，复用原始 `yolo11_pose.cpp` 的 TensorRT pose 推理逻辑，并服务化为 Runtime API
- Phase 17.5 POSE Runner：新增 `PoseModelRunner`，接入 `IModelRunner` 工厂体系，支持 `model.type=pose`
- Phase 17.5 POSE 序列化：结果 JSON 返回 `bbox`、`keypoints`、`skeleton`、`num_keypoints`、`valid_keypoints`、`pose_coordinate_system=original_image_pixels`、`keypoint_format=coco17_xy_conf`
- Phase 17.5 POSE 独立配置：新增 `config/server_pose.yaml`、`config/worker_pose.yaml`，服务端口 8085，Redis Stream 为 `yolo:stream:pose`，Consumer Group 为 `yolo11_pose_group`
- Phase 17.5 POSE 独立脚本：新增 `scripts/start_pose.ps1` 与 `scripts/stop_pose.ps1`
- Phase 17.5 POSE 自动测试：新增 `tools/phase17_5_pose_registry_test.py`、`tools/phase17_5_pose_smoke_test.py`、`tools/run_phase17_5_regression.py`
- Phase 17.5 POSE 单项验收：`/health`、`/ready`、`/workers`、`/metrics` 正常，`POST /api/v1/pose/image/async` 返回 queued，结果查询返回 done + 3 个 person pose，每个 pose 包含 17 个 COCO keypoints
- Phase 17.5 POSE metrics 验收：`metrics_found=true`，`total_tasks=3`，`total_tasks_done=3`，`total_tasks_failed=0`，`worker_distribution.pose_worker_1=3`
- Phase 17.5 Redis 验收：`XPENDING yolo:stream:pose yolo11_pose_group = 0`
- Phase 17.5 全量回归验收：`run_phase17_5_regression.py` 同时跑通 POSE registry/smoke、CLS 回归和 Phase 16 Full Regression，最终 `Phase 17.5 Regression Summary: success=True`


当前还没有做：

- Segmentation 图片异步服务接入
- 多路 RTSP / 摄像头并发流管理
- WebSocket / SSE 实时推送检测结果
- 多 GPU / 多模型 Worker 调度
- ImageStorage / VideoStorage 抽象层（当前图片主要使用 Redis Binary，视频主要使用 LocalFile）
- Prometheus 指标、服务守护、Docker / Linux 部署路径
- 更严格的长视频压测、磁盘空间保护和历史任务清理策略

当前开发原则是：**Detect 图片、OBB 图片、Detect 视频文件异步服务和单路流任务服务已经形成稳定基础；下一阶段进入 SEG 图片异步服务之前，优先保证 Detect / OBB / CLS / POSE / Video / Stream 六类服务的配置、目录、脚本、Redis 状态和测试入口足够清晰，避免服务类型增多后工程结构失控。**

---

## 功能特点

- 支持 YOLO11 Detection 模型在 Windows 上进行 TensorRT 部署
- 支持 YOLO11 OBB 旋转框模型在 Windows 上进行 TensorRT 部署
- 支持 TensorRT 10 API
- 支持 Visual Studio 2019 + CMake 编译
- 支持 CUDA 图像预处理
- Detection 支持 CPU 后处理和可选 GPU 后处理路径
- 通过 `myplugins.dll` 支持自定义 TensorRT 插件
- 提供 C++ Runtime API 封装
  - `Yolo11Detector`
  - `Yolo11ObbDetector`
  - `Yolo11ClsDetector`
  - `Yolo11PoseDetector`
- 保留原始命令行 demo，不破坏原项目验证路径
- 新增纯 C++ HTTP Server 模块
  - `yolo11_server.exe`
  - Crow HTTP 框架
  - YAML 配置读取
  - JSON 返回检测结果
  - 结果图保存与 HTTP 访问
  - Redis Stream 异步队列
  - 独立 `yolo11_worker.exe` 消费任务
  - Redis Binary Key 保存输入图和结果图
  - Redis 保存任务状态、元信息和结果 JSON
  - Redis Stream Consumer Group 多 Worker 消费
  - Pending 任务恢复与 `XAUTOCLAIM`
  - Stream 长度控制与 `XTRIM MAXLEN ~`
  - 压测统计与 worker 分布统计
  - Worker 强杀恢复测试
  - Debug 版本 Ctrl+C 优雅退出与弹窗抑制
  - 独立 `yolo11_stream_worker.exe` 处理实时流任务
  - 流任务 start / stop / status / snapshot 管理
  - 单活流保护，避免单 Worker 下重复 start 造成任务堆积
  - 断流 / 打开失败后进入 reconnecting，并在最大重连次数后 failed
  - 最新检测帧 snapshot 本地保存与 HTTP 下载
  - Phase 14.5 稳定性自动测试脚本
  - Phase 14.5 source matrix 测试脚本：验证 file / camera / RTSP 输入路径
  - Phase 14.5 stream metrics 测试脚本：验证 stopped/failed 终态统计写入 Redis
  - Phase 15 Worker Capability Registry：统一描述 Detect / OBB / Video / Stream Worker 能力
  - Phase 15 `/workers` / `/ready` / `/metrics` 支持能力过滤
  - Worker heartbeat 中包含 `model_type`、`runner_model_type`、`worker_group`、`worker_kind`、`task_kind`、`stream_type`、`gpu_id`
  - 支持按 `worker_group=image_detect_gpu0/image_obb_gpu0/video_detect_gpu0/stream_detect_gpu0` 进行就绪检查
  - 支持区分外部服务类型与内部推理模型，例如 `model_type=video`、`runner_model_type=detect`
  - Phase 17 ModelOutput 最小抽象：支持分类 top1/topk 与姿态 bbox/keypoints/skeleton 等非检测框结构
  - Phase 17 CLS 图片异步服务：`POST /api/v1/classify/image/async`，独立 8084 端口、独立 Redis Stream、独立 Worker
  - Phase 17.5 POSE 图片异步服务：`POST /api/v1/pose/image/async`，独立 8085 端口、独立 Redis Stream、独立 Worker
  - Phase 17 / 17.5 `/workers`、`/ready`、`/metrics` 支持 `model=cls` 与 `model=pose` 过滤

---

## 测试环境

| 依赖 | 版本 / 路径 |
|---|---|
| 操作系统 | Windows |
| IDE | Visual Studio 2019 |
| CUDA | `D:\GPU13.3` |
| cuDNN | `D:\cuDNN9\bin\13.3\x64` |
| TensorRT | `D:\TensorRT-10.16.1.11` |
| OpenCV | `D:\libs\opencv\build` |
| vcpkg | `D:\vcpkg` |
| Python | 3.12 |
| Redis | WSL Ubuntu Redis 8.2.1 |
| GPU | RTX 4080 Laptop GPU |
| CUDA 架构 | `sm_89` |

运行前需要确保以下目录在系统 `Path` 或当前终端 `PATH` 中：

```text
D:\GPU13.3\bin
D:\cuDNN9\bin\13.3\x64
D:\TensorRT-10.16.1.11\lib
D:\libs\opencv\build\x64\vc16\bin
```

PowerShell 临时设置示例：

```powershell
$env:PATH="D:\TensorRT-10.16.1.11\lib;D:\GPU13.3\bin;D:\libs\opencv\build\x64\vc16\bin;$env:PATH"
```

---

## HTTP Server 第三方依赖

为了支持纯 C++ HTTP 服务，当前新增了三个主要依赖：

- Crow：HTTP Server
- nlohmann/json：JSON 生成
- yaml-cpp：读取 `config/server.yaml`
- asio：Crow 依赖，安装 Crow 时会自动处理
- hiredis：C++ 连接 Redis，支持 Redis Stream 异步任务队列
- spdlog：结构化日志，拆分 `server.log` 和 `worker.log`

通过 vcpkg 安装：

```bat
cd /d D:\vcpkg

.\vcpkg.exe install nlohmann-json:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install yaml-cpp:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install crow:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
.\vcpkg.exe install spdlog:x64-windows --vcpkg-root D:\vcpkg
```

如果手动配置 CMake，需要带上 vcpkg toolchain：

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

Visual Studio 的 CMake 配置也需要确保使用同一个 toolchain，否则会出现找不到：

```text
crow.h
nlohmann/json.hpp
yaml-cpp/yaml.h
```

---

## 项目结构

```text
yolo11-tensorrt-windows
├── api
│   ├── yolo11_detector_api.cpp
│   └── yolo11_obb_api.cpp
├── config
│   ├── server.yaml
│   └── worker.yaml（可选，Worker 独立配置）
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
├── labels
│   └── coco.txt
├── images
├── engine
│   └── yolo11n.engine
├── output
├── temp
├── tools
│   └── benchmark_async.py
├── gen_wts.py
├── yolo11_det.cpp
├── yolo11_obb.cpp
├── CMakeLists.txt
├── README_EN.md
└── README_CN.md
```

注意：

- `src/server/*.cpp` 是服务化新增代码
- `api/*.cpp` 是 C++ Runtime API
- `examples/*.cpp` 是 API 使用示例
- `yolo11_det.cpp` / `yolo11_obb.cpp` 是原始命令行程序
- 旧版 `output/`、`temp/`、`logs/` 已逐步迁移到统一的 `runtime/` 目录
- `engine/` 或 `engines/` 目录里的 `.engine` 不应该提交

当前 Phase 14.5 之后推荐的运行目录结构：

```text
yolo11/
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
│   └── archive/
│       └── legacy_phase_configs/
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
```

这次目录整理的原则是：`config/` 只放当前要用的配置，历史配置归档；运行时产生的输入、输出和日志统一放入 `runtime/`，避免 `temp/`、`output/`、`logs/` 和各阶段目录混杂。

---

## 支持情况

| 任务 | 原始命令行程序 | C++ API 封装 | Demo 程序 | HTTP 服务 | 状态 |
|---|---|---|---|---|---|
| Detection | `yolo11_det.exe` | `Yolo11Detector` | `demo_image.exe`, `demo_video.exe` | 同步 + Redis 异步 + 多 Worker + 视频文件异步服务 + 单路流服务 | 已支持 |
| OBB | `yolo11_obb.exe` | `Yolo11ObbDetector` | `demo_obb_image.exe` | 异步 HTTP 服务 + 独立 Worker + 双服务并行 | Phase 10-12 已验证 |
| Classification | `yolo11_cls.exe` | `Yolo11ClsDetector` | 原始命令行 demo 保留 | 图片异步 HTTP 服务 + 独立 Worker + top1/topk JSON | Phase 17 已验证 |
| Pose | `yolo11_pose.exe` | `Yolo11PoseDetector` | 原始命令行 demo 保留 | 图片异步 HTTP 服务 + 独立 Worker + bbox/keypoints/skeleton JSON | Phase 17.5 已验证 |
| Segmentation | `yolo11_seg.exe` | 计划封装 | 原始命令行 demo 保留 | Phase 18 计划接入图片异步服务 | 暂未服务化 |

OBB 当前推荐优先使用 CPU 后处理路径进行验证。OBB 的 GPU 后处理路径保留为后续完善和测试方向。

---

## 配置说明

### 模型配置

主要模型配置文件：

```text
include/config.h
```

如果使用 COCO Detection 模型：

```cpp
const static int kNumClass = 80;
```

如果使用官方 YOLO11 OBB / DOTA 模型：

```cpp
const static int kNumClass = 16;
```

如果使用自定义单类别模型：

```cpp
const static int kNumClass = 1;
```

修改 `config.h` 后，必须重新编译项目，并重新生成 TensorRT engine。

如果修改了类别数、模型结构、TensorRT 版本、CUDA 版本或 GPU 架构，不要继续复用旧的 `.engine` 文件。

### 服务配置

HTTP 服务配置文件：

```text
config/server.yaml
```

推荐写法：

```yaml
server:
  host: "0.0.0.0"
  port: 8080
  threads: 4

model:
  type: "detect"
  engine_path: "D:/tensorrtx/yolo11/engine/yolo11n.engine"
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

这里建议 `engine_path` 使用绝对路径，避免相对路径混乱。
---

## 编译方法

使用 Visual Studio 2019 打开项目文件夹：

```text
File -> Open -> Folder
```

打开项目目录，例如：

```text
D:\tensorrtx\yolo11
```

如果已经存在旧的 CMake 缓存，建议删除：

```text
out/
```

然后重新配置并编译目标。

可执行文件通常生成在：

```text
out\build\x64-Debug
```

常用编译目标：

```text
myplugins
yolo11_det
yolo11_obb
yolo11_cls
yolo11_pose
demo_image
demo_video
demo_obb_image
yolo11_server
yolo11_worker
yolo11_video_worker
yolo11_stream_worker
```

运行时必须有：

```text
myplugins.dll
```

`myplugins.dll` 必须和可执行文件位于同一目录。当前 CMake 配置会在编译后自动复制该 DLL。

---

## CMake 关键注意事项

新增 server 后，最容易出问题的地方不是 CUDA 或 TensorRT，而是 CMake target 划分。

错误方式：

```cmake
file(GLOB_RECURSE SRCS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.cu
)
```

然后把 `${SRCS}` 同时加入：

```text
yolo11_det
yolo11_obb
yolo11_runtime
yolo11_server
yolo11_worker
yolo11_video_worker
yolo11_stream_worker
```

这样会导致 `src/server/*.cpp` 被编进原来的 `yolo11_det`、`yolo11_obb`，产生莫名其妙的编译错误。

正确做法是排除 server 目录：

```cmake
file(GLOB_RECURSE SRCS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.cu
)

list(FILTER SRCS EXCLUDE REGEX ".*src[/\\\\]server[/\\\\].*")
```

然后 server 单独编译：

```cmake
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)

set(YOLO11_HIREDIS_TARGET hiredis::hiredis)

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

这一点是服务化改造的第一个重要经验：**不要破坏原始 demo；新增 server 模块必须和原始命令行 target 隔离。**

---

## 下载 YOLO11 模型

示例脚本：

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

`yolo11n.pt` 用于普通检测，`yolo11n-obb.pt` 用于 OBB 旋转框检测。

---

## `.pt` 转 `.wts`

进入项目目录：

```bat
cd /d D:\tensorrtx\yolo11
```

Detection 模型：

```bat
python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
```

OBB 模型：

```bat
python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
```

自定义 Detection 模型：

```bat
python gen_wts.py -w weights/best.pt -o best.wts -t detect
```

自定义 OBB 模型：

```bat
python gen_wts.py -w weights/best.pt -o best-obb.wts -t obb
```

如果出现 `C3k2` 相关错误，请更新 Ultralytics：

```bat
python -m pip install -U ultralytics
```

---

## 生成 TensorRT Engine

将 `.wts` 文件复制到可执行文件目录：

```bat
copy /Y yolo11n.wts out\build\x64-Debug\
copy /Y yolo11n-obb.wts out\build\x64-Debug\
```

进入可执行文件目录：

```bat
cd /d D:\tensorrtx\yolo11\out\build\x64-Debug
```

生成 Detection engine：

```bat
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
```

生成 OBB engine：

```bat
yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
```

模型规模参数：

| YOLO11 模型 | 参数 |
|---|---|
| YOLO11n | `n` |
| YOLO11s | `s` |
| YOLO11m | `m` |
| YOLO11l | `l` |
| YOLO11x | `x` |

如果自定义模型是基于 YOLO11n 训练的，最后一个参数使用 `n`。

---

## 原始命令行推理

### Detection

对图片文件夹进行检测推理：

```bat
yolo11_det.exe -d yolo11n.engine D:\tensorrtx\yolo11\images c
```

最后一个参数表示后处理模式：

| 参数 | 含义 |
|---|---|
| `c` | CPU 后处理 |
| `g` | GPU 后处理 |

结果会保存到当前可执行文件目录。


### Stream 实时流服务

```powershell
cd D:\tensorrtx\yolo11

cmake --build out\build\x64-Debug --config Debug --target yolo11_server
cmake --build out\build\x64-Debug --config Debug --target yolo11_stream_worker

# 窗口 1：Stream Server
.\out\build\x64-Debug\yolo11_server.exe .\config\server_stream.yaml

# 窗口 2：Stream Worker
.\out\build\x64-Debug\yolo11_stream_worker.exe .\config\worker_stream.yaml --consumer-name stream_worker_1

# 窗口 3：检查
curl.exe "http://127.0.0.1:8083/api/v1/health"
curl.exe "http://127.0.0.1:8083/api/v1/ready"
curl.exe "http://127.0.0.1:8083/api/v1/workers"

# 推荐用脚本测试
python tools\phase14_5_stream_stability_test.py `
  --url http://127.0.0.1:8083 `
  --source-type camera `
  --camera-id 0 `
  --repeat 3 `
  --run-seconds 5
```

进一步验证 stopped / failed 终态 metrics：

```powershell
python tools\phase14_5_stream_metrics_check.py `
  --url http://127.0.0.1:8083 `
  --camera-id 0 `
  --invalid-camera-id 99 `
  --run-seconds 3 `
  --wait-seconds 30
```

期望：

```text
PASS Phase 14.5 stream metrics check
total_tasks_done = 1
total_tasks_failed = 1
```


### OBB

对图片文件夹进行 OBB 推理：

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

建议先使用 `c` 验证 OBB 推理流程。OBB 测试图片应尽量匹配模型领域，例如 DOTA 风格的航拍图像。

---

## C++ Detection API

头文件：

```cpp
#include "yolo11_detector_api.h"
```

调用示例：

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

运行图片 demo：

```bat
demo_image.exe yolo11n.engine D:\tensorrtx\yolo11\images\a.jpg det_result.jpg
```

运行视频 demo：

```bat
demo_video.exe yolo11n.engine D:\tensorrtx\yolo11\test.mp4 result_video.mp4
```

运行摄像头 demo：

```bat
demo_video.exe yolo11n.engine 0
```

---

## C++ OBB API

头文件：

```cpp
#include "yolo11_obb_api.h"
```

调用示例：

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

运行 OBB 图片 demo：

```bat
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

最后一个参数表示 OBB demo 的后处理模式：

| 参数 | 含义 |
|---|---|
| `cpu` | CPU OBB 后处理 |
| `gpu` | GPU OBB 后处理路径，实验性 |

推荐验证命令：

```bat
demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

---

## 纯 C++ HTTP Detection Server

当前服务阶段：

```text
Phase 8.5：Server / Worker 分离 + Redis Binary 图片存储 + 健康检查 + Worker 心跳 + 日志 + labels_path + metrics
```

当前支持接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| GET | `/api/v1/health` | Server 和 Redis 基础健康检查 |
| GET | `/api/v1/ready` | 判断系统是否可接收新异步任务 |
| GET | `/api/v1/workers` | 查看 Worker 心跳和在线状态 |
| GET | `/api/v1/metrics` | 查看累计任务、近期 QPS、平均耗时和 Worker 分布 |
| POST | `/api/v1/detect/image` | 同步图片检测 |
| POST | `/api/v1/detect/image?debug=true` | 同步图片检测，并返回模型原始 bbox |
| POST | `/api/v1/detect/image/async` | 提交异步图片检测任务，返回 `task_id` |
| GET | `/api/v1/result/{task_id}` | 查询异步任务状态和检测结果 |
| GET | `/api/v1/image/<filename>` | 读取保存后的结果图 |

### 编译 server

```powershell
cd D:\tensorrtx\yolo11
cmake --build out\build\x64-Debug --config Debug --target yolo11_server
cmake --build out\build\x64-Debug --config Debug --target yolo11_worker
cmake --build out\build\x64-Debug --config Debug --target yolo11_video_worker
cmake --build out\build\x64-Debug --config Debug --target yolo11_stream_worker
```

### 启动 server

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml
```

如果找不到 exe，可以先搜索：

```powershell
Get-ChildItem -Recurse -Filter yolo11_server.exe | Select-Object FullName
```

### 健康检查

```powershell
curl.exe http://127.0.0.1:8080/api/v1/health
```

正常返回类似：

```json
{
    "success": true,
    "status": "ok",
    "service": "yolo11_server",
    "phase": "phase6_redis_connection_reuse",
    "model_type": "detect",
    "queue_backend": "redis_stream",
    "redis_ping": "ok",
    "async_worker": "running"
}
```

### 图片检测

PNG 图片：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

JPEG 图片：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/jpeg" --data-binary "@D:/tensorrtx/yolo11/images/bus.jpg"
```

正常返回示例：

```json
{
    "success": true,
    "model_type": "detect",
    "image": {
        "width": 474,
        "height": 316,
        "channels": 3
    },
    "bbox_coordinate_system": "original_image_pixels",
    "bbox_format": "xywh_and_xyxy",
    "num_detections": 3,
    "detections": [
        {
            "class_id": 0,
            "class_name": "person",
            "confidence": 0.9210741519927979,
            "clipped": false,
            "bbox": {
                "x": 140,
                "y": 68,
                "w": 63,
                "h": 204,
                "x1": 140,
                "y1": 68,
                "x2": 203,
                "y2": 272
            }
        }
    ],
    "result_image_url": "/api/v1/image/result_xxx.jpg",
    "result_image_url_full": "http://127.0.0.1:8080/api/v1/image/result_xxx.jpg"
}
```

这里的 `bbox` 是**原始输入图片像素坐标**，不是模型输入坐标，也不是 640×640/letterbox 坐标。


### 异步图片检测

提交异步任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

正常返回示例：

```json
{
    "queue_backend": "redis_stream",
    "result_url": "/api/v1/result/20260705_214026_1",
    "status": "queued",
    "success": true,
    "task_id": "20260705_214026_1"
}
```

查询异步结果：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260705_214026_1"
```

任务完成后返回中的关键字段：

```json
{
    "success": true,
    "status": "done",
    "queue_backend": "redis_stream",
    "num_detections": 3,
    "result_image_url": "/api/v1/image/20260705_214026_1_result.jpg"
}
```

### Debug 模式

调试请求：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image?debug=true" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

会额外返回：

```json
{
    "debug": true,
    "debug_note": "raw_model_bbox is returned only when debug=true or debug=1.",
    "detections": [
        {
            "raw_model_bbox": {
                "x1": 189.4034423828125,
                "y1": 198.76870727539063,
                "x2": 274.6661071777344,
                "y2": 474.3380432128906
            }
        }
    ]
}
```

`raw_model_bbox` 是模型原始输出坐标，用于排查坐标映射问题；正式前端应该只使用 `bbox` 字段。

### 访问结果图

浏览器打开：

```text
http://127.0.0.1:8080/api/v1/image/result_xxx.jpg
```

或者下载：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/image/result_xxx.jpg" --output test_result.jpg
start .\test_result.jpg
```

---


## Phase 2 Redis Stream 异步图片检测队列（历史记录）

下面内容保留为 Phase 2 的阶段记录，用来回顾异步队列最初接入时的设计和踩坑。

当前服务阶段曾经从：

```text
Phase 1.5：同步 HTTP 图片检测服务
```

升级为：

```text
Phase 2：HTTP 图片检测服务 + Redis Stream 异步任务队列
```

这一阶段的核心目标不是单纯“能检测”，而是把一次 HTTP 请求变成可排队、可查询、可追踪的任务。

### 当前 Phase 2 架构

目前仍然是一个可执行程序：

```text
yolo11_server.exe
├── Crow HTTP Server
├── Yaml 配置读取
├── RedisTaskQueue
├── 本地 Async Worker Thread
├── Yolo11Detector TensorRT 推理
├── 结果图保存
└── Redis 结果写回
```

也就是说，HTTP Server 和 Worker 目前还在同一个进程中，只是通过 Redis Stream 完成了任务队列化。下一阶段才会拆成独立的 `yolo11_server.exe` 和 `yolo11_worker.exe`。

### 异步检测数据流

```text
Client 上传图片
↓
POST /api/v1/detect/image/async
↓
HTTP Server 生成 task_id
↓
保存原图到 ./temp/input
↓
写入 Redis Stream: yolo:stream:detect
↓
Worker 使用 XREADGROUP 消费任务
↓
更新状态 queued -> running
↓
调用 Yolo11Detector 推理
↓
保存结果图到 ./output
↓
写入 Redis result/meta/status
↓
Client 通过 GET /api/v1/result/{task_id} 查询结果
```

这一步的意义是：客户端不需要一直阻塞等待推理完成，后端也具备了后续扩展多 Worker、多进程、多机器的基础。

### Phase 2 新增接口

| 方法 | 接口 | 说明 |
|---|---|---|
| POST | `/api/v1/detect/image/async` | 上传图片并提交异步检测任务，立即返回 `task_id` |
| GET | `/api/v1/result/{task_id}` | 查询任务状态和检测结果 |
| GET | `/api/v1/image/{filename}` | 读取同步或异步生成的结果图 |
| GET | `/api/v1/health` | 健康检查，新增 Redis 状态字段 |

健康检查中 Redis 正常时应看到：

```json
{
    "async_worker": "running",
    "phase": "phase2_redis_stream_queue",
    "queue_backend": "redis_stream",
    "redis_enabled": true,
    "redis_ping": "ok",
    "redis_stream_key": "yolo:stream:detect",
    "success": true
}
```

### Redis Key 设计

| Redis Key | 类型 | 作用 |
|---|---|---|
| `yolo:stream:detect` | Stream | 异步检测任务队列 |
| `yolo:task:{task_id}:status` | String | 保存任务状态：`queued` / `running` / `done` / `failed` |
| `yolo:task:{task_id}:meta` | Hash | 保存任务路径、时间戳、错误信息、结果图路径等元信息 |
| `yolo:task:{task_id}:result` | String | 保存最终检测 JSON |

任务状态机：

```text
queued -> running -> done
queued -> running -> failed
```

这个状态机是异步服务的核心。后续拆分 Worker、多 Worker 并发、任务失败重试，都要围绕这套状态设计继续扩展。

### Phase 2 运行命令

启动 Redis，当前使用 WSL Ubuntu 中的 Redis 8.2.1：

```bash
sudo service redis-server start
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

Windows 侧确认能连到 WSL Redis：

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

启动服务：

```powershell
cd D:\tensorrtx\yolo11\out\build\x64-Debug
.\yolo11_server.exe D:\tensorrtx\yolo11\config\server.yaml
```

健康检查：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/health"
```

提交异步任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

查询任务结果：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260705_214026_1"
```

下载结果图：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/image/20260705_214026_1_result.jpg" -o redis_async_result.jpg
start .\redis_async_result.jpg
```

连续提交 5 个异步任务：

```powershell
for ($i=1; $i -le 5; $i++) {
  curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
    -H "Content-Type: image/png" `
    --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
}
```

### Redis 验收命令

进入 Redis：

```bash
redis-cli -h 172.19.196.109 -p 6379
```

查看项目相关 key：

```redis
KEYS yolo:*
```

查看 Stream 任务记录：

```redis
XRANGE yolo:stream:detect - +
```

查看任务状态：

```redis
GET yolo:task:20260705_214026_1:status
```

查看检测结果：

```redis
GET yolo:task:20260705_214026_1:result
```

查看任务元信息：

```redis
HGETALL yolo:task:20260705_214026_1:meta
```

查看消费组：

```redis
XINFO GROUPS yolo:stream:detect
XINFO CONSUMERS yolo:stream:detect yolo11_group
```

本阶段实际验收结果中，Redis 已经能看到：

```text
yolo:stream:detect
yolo:task:20260705_214026_1:status = done
yolo:task:20260705_214026_1:result = 检测 JSON
yolo:task:20260705_214026_1:meta = input/result 路径和时间戳
```

并且 `POST /api/v1/detect/image/async` 可以连续提交多个任务，Worker 能正常消费并完成推理。

---

---

## Phase 4 / Phase 5 Redis Stream 多 Worker 推理池与压测验证

### 阶段定位

在 Phase 2 和 Phase 3 中，系统已经具备了 Redis Stream 异步任务队列能力；本阶段进一步完成了多 Worker 推理池、性能指标采集、压测脚本、Pending 恢复和 Redis Stream 长度控制。当前工程已经从“异步任务能跑通”进入到“多 Worker 能稳定消费、性能数据可量化、异常任务可恢复、队列不会无限增长”的阶段。

当前阶段可以概括为：

```text
Phase 4：Redis Stream Consumer Group 多 Worker 推理池
Phase 5：压测统计、Pending 恢复、XTRIM 清理与稳定性验证
```

### 最新架构

当前仍然是一个可执行程序 `yolo11_server.exe`，但内部已经拆分出 HTTP 生产者和多 Worker 消费者：

```text
yolo11_server.exe
├── Crow HTTP Server
│   ├── GET  /api/v1/health
│   ├── POST /api/v1/detect/image
│   ├── POST /api/v1/detect/image/async
│   └── GET  /api/v1/result/{task_id}
├── RedisTaskQueue
│   ├── XADD 提交任务
│   ├── XREADGROUP 消费任务
│   ├── XACK 确认任务
│   ├── XAUTOCLAIM 回收 Pending
│   └── XTRIM 控制 Stream 长度
├── InferenceService
│   └── 管理多个 InferenceWorker
├── InferenceWorker × 3
│   ├── 每个 Worker 独立加载 TensorRT Engine
│   ├── 每个 Worker 使用独立 consumer_name
│   ├── 消费 Redis Stream 任务
│   ├── 执行 YOLO11 TensorRT 推理
│   ├── 保存结果图
│   └── 写回 Redis result/meta/status
└── Yolo11Detector
    └── 同步接口和 Worker 内部推理复用同一套 Runtime API
```

### 核心配置

本阶段推荐配置：

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

其中：

| 配置 | 作用 |
|---|---|
| `worker_num: 3` | 启动 3 个 C++ 推理 Worker |
| `consumer_name_prefix: "worker_"` | 自动生成 `worker_1`、`worker_2`、`worker_3` |
| `stream_max_len: 10000` | Redis Stream 近似最大长度 |
| `enable_pending_reclaim: true` | 开启 Pending 任务恢复 |
| `pending_min_idle_ms: 60000` | Pending 任务 idle 超过 60 秒后允许被回收 |
| `log_task_done: false` | 压测时关闭每个任务完成日志，减少 Windows 控制台 I/O 干扰 |

### Redis Stream 核心命令理解

| 命令 | 项目中的作用 |
|---|---|
| `XADD` | HTTP 接口提交异步任务到 `yolo:stream:detect` |
| `XGROUP CREATE` | 创建消费组 `yolo11_group` |
| `XREADGROUP` | Worker 从消费组中读取任务 |
| `XACK` | Worker 完成任务后确认消息 |
| `XPENDING` | 查看未确认任务数量 |
| `XAUTOCLAIM` | 回收长时间未 ack 的 pending 任务 |
| `XINFO CONSUMERS` | 查看每个 worker 的 pending 和 idle 状态 |
| `XLEN` | 查看 Stream 当前长度 |
| `XTRIM MAXLEN ~` | 近似裁剪 Stream，防止无限增长 |

这次真正理解到：Redis Stream 不只是“缓存结果”，而是承担了消息队列、消费组、异常恢复和运行状态观测的职责。

### 异步任务状态机

```text
queued -> running -> done
queued -> running -> failed
queued/running -> pending -> reclaimed -> running -> done
```

本阶段新增了以下任务指标：

| 字段 | 含义 |
|---|---|
| `worker_id` | 处理该任务的 Worker 编号 |
| `consumer_name` | Redis Consumer 名称，例如 `worker_2` |
| `queue_wait_ms` | 从任务创建到 Worker 开始处理的排队等待时间 |
| `inference_ms` | TensorRT 推理和结果图生成耗时 |
| `total_ms` | 从任务创建到任务完成的总耗时 |
| `create_time_ms` | 任务创建时间 |
| `start_time_ms` | Worker 开始处理时间 |
| `finish_time_ms` | 任务完成时间 |

这些字段让压测不再只看“能不能返回”，而是能区分到底是模型慢、队列慢，还是 HTTP / Redis / 文件 I/O 慢。

### Health 接口新增字段

当前 `/api/v1/health` 会返回 Redis 和 Worker 状态，例如：

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
  "redis_pending_min_idle_ms": 60000,
  "worker_num": 3
}
```

重点观察：

```text
redis_ping = ok
redis_pending = 0
redis_stream_len ≈ redis_stream_max_len
worker_num = 3
```

### 压测脚本

本阶段新增：

```text
tools/benchmark_async.py
```

标准压测命令：

```powershell
python tools\benchmark_async.py --url http://127.0.0.1:8080 --image D:/tensorrtx/yolo11/images/bus.png --tasks 1000 --concurrency 10 --timeout 240
```

极限压测命令：

```powershell
python tools\benchmark_async.py --url http://127.0.0.1:8080 --image D:/tensorrtx/yolo11/images/bus.png --tasks 3000 --concurrency 20 --timeout 300
```

脚本统计：

- 提交成功数
- done / failed / timeout 数量
- QPS
- worker 分布
- `total_ms` 平均值、p50、p95、p99
- `queue_wait_ms` 平均值、p50、p95、p99
- `inference_ms` 平均值、p50、p95、p99
- 客户端观测延迟

### 实测结果

`worker_num=3` 下的稳定压测结果：

| tasks | concurrency | Done/Failed/Timeout | QPS | avg total_ms | avg queue_wait_ms | avg inference_ms |
|---:|---:|---:|---:|---:|---:|---:|
| 500 | 5 | 500/0/0 | 34.424 | 3756.384 | 3729.490 | 7.529 |
| 1000 | 10 | 1000/0/0 | 34.545 | 8165.548 | 8139.791 | 7.035 |
| 1000 | 20 | 1000/0/0 | 33.784 | 8542.974 | 8514.400 | 8.937 |

结论：

```text
worker_num=3 下，当前稳定吞吐约为 33–35 QPS。
concurrency 从 10 增加到 20 后 QPS 没有继续提升，说明 3-worker 推理池已经接近当前吞吐上限。
继续增加并发主要会增加 queue_wait_ms，而不是提升服务能力。
```

### 极限压测与 XTRIM 验证

极限压测：

```text
Tasks: 3000
Concurrency: 20
Submitted ok: 2999/3000
Done/Failed/Timeout: 2975/0/24
avg total_ms = 64823.343 ms
avg queue_wait_ms = 64787.961 ms
avg inference_ms = 11.221 ms
```

压测后 Health：

```json
{
  "redis_pending": 0,
  "redis_stream_len": 10012,
  "redis_stream_max_len": 10000,
  "worker_num": 3
}
```

这说明两件事：

1. `XTRIM MAXLEN ~ 10000` 生效，Stream 没有无限增长，而是被控制在 10000 附近。由于 `~` 是近似裁剪，`10012` 是正常结果。
2. 即使极限压测中出现 pending 和 Redis 连接错误，最终 `XPENDING = 0`，说明 Pending 恢复机制最终把任务清理干净。

### 本阶段遇到的问题与解决方案

#### 1. `kNumClass = 16` 配置隐患

启动日志一度出现：

```text
kNumClass = 16
```

但当前运行的是 COCO Detection 模型 `yolo11n.engine`，应该是：

```text
kNumClass = 80
```

解决方式：修改 `include/config.h`，重新编译并重新生成 engine。修正后启动日志显示：

```text
kNumClass = 80
```

反思：模型类别数、engine 文件和运行接口必须一致。`person` 这类 `class_id=0` 的结果能跑出来，并不代表类别配置一定正确。

#### 2. Crow INFO 日志影响压测

Crow 默认会输出大量请求日志，压测时每个 POST / GET 都会刷屏。Windows 控制台输出很慢，会干扰性能数据。

解决方式是在 `main_server.cpp` 中设置：

```cpp
crow::SimpleApp app;
app.loglevel(crow::LogLevel::Warning);
```

反思：压测时日志也是性能瓶颈。服务端 benchmark 要尽量减少控制台 I/O。

#### 3. Worker 每任务日志影响压测

即使关闭 Crow INFO 日志，Worker 仍可能输出：

```text
Worker 1 task done: ...
```

压测时 1000 个任务就是 1000 行日志。建议通过配置控制：

```yaml
worker:
  log_task_done: false
```

反思：调试日志和压测日志应该分级，不能混在一起。

#### 4. 高压下 Redis 出现 `address in use`

极限压测中出现：

```text
failed to submit task to Redis
Redis markDone failed: address in use
Redis ackTask failed: address in use
Redis claimPendingTask failed: address in use
```

判断原因：当前 Redis 操作可能频繁创建短连接。高压下，Windows 本地端口 / TCP 连接资源出现压力。

短期处理：

- 不把 `tasks=3000, concurrency=20` 作为常规部署压力。
- 将该组作为极限边界测试记录。
- 观察 `XPENDING` 是否最终回到 0。

长期优化：

- 每个 Worker 持有长期 Redis 连接。
- HTTP Producer 持有长期 Redis 连接。
- 命令失败时自动 reconnect。
- 必要时实现 Redis connection pool。

#### 5. 队列等待才是主要瓶颈

在稳定压测中：

```text
avg queue_wait_ms 远大于 avg inference_ms
```

例如：

```text
1000 tasks / concurrency=10
avg queue_wait_ms = 8139.791 ms
avg inference_ms  = 7.035 ms
```

说明 TensorRT 单次推理很快，真正的耗时来自任务堆积后的排队等待。

反思：当模型推理时间已经降到毫秒级后，系统瓶颈往往会转移到队列、I/O、状态更新、结果轮询和连接管理。

### 本阶段最终结论

```text
Phase 5 压测与稳定性验证完成。
系统在 worker_num=3 下可稳定完成 500/1000 级异步检测任务，稳定吞吐约 33–35 QPS。
Redis Stream Consumer Group 多 Worker 调度均衡，worker_1、worker_2、worker_3 基本平均分配任务。
Redis pending 最终可恢复至 0，Pending reclaim 机制有效。
Redis Stream 长度在 stream_max_len=10000 配置下被控制在 10000 附近，XTRIM 清理策略有效。
当前系统主要瓶颈已经不是 TensorRT 推理本身，而是高并发下的队列等待、Redis 状态更新和连接管理。
```

---

---

## Phase 6 Redis 连接复用与生产化稳定性优化

### 阶段定位

Phase 6 的核心目标不是继续增加新接口，而是解决 Phase 5 极限压测中暴露出的 Redis 连接压力问题。上一阶段在 `tasks=3000, concurrency=20` 场景下曾出现 `address in use`、`markDone failed`、`ackTask failed` 等现象，说明系统虽然具备 Pending 恢复能力，但 Redis 命令执行方式还不够生产化。

本阶段将 Redis 操作从“频繁创建短连接”优化为“连接复用 + 命令失败自动重连”，让 HTTP Producer 和各个 InferenceWorker 在高压下保持更稳定的 Redis 访问能力。

当前阶段可以概括为：

```text
Phase 6：Redis 连接复用 + 自动重连 + 高压稳定性验证
```

### 本阶段新增能力

- `RedisTaskQueue` 内部长期持有 `redisContext*`，避免每条 Redis 命令都重新建立 TCP 连接。
- Redis 命令统一通过连接复用入口执行，并使用 `std::mutex` 保护 `redisContext`。
- Redis 命令失败后支持自动 `reconnect` 一次，提高瞬时网络/连接异常下的恢复能力。
- HTTP Producer 使用一个长期 Redis 连接。
- 每个 InferenceWorker 各自持有独立 `RedisTaskQueue` 和独立 Redis 长连接。
- `/api/v1/health` 的 `phase` 更新为 `phase6_redis_connection_reuse`。
- `server.yaml` 显式保留 `stream_max_len`、`enable_pending_reclaim`、`pending_min_idle_ms` 和 `log_task_done` 等稳定性配置。
- 修复 `markFailed()` 中 Redis 命令参数数量不匹配的隐藏问题。
- 完成 `1000/concurrency=10`、`1000/concurrency=20` 和 `3000/concurrency=20` 三组压测验收。

### Phase 6 当前架构

```text
yolo11_server.exe
├── Crow HTTP Server
│   ├── 同步检测接口
│   ├── 异步任务提交接口
│   ├── 结果查询接口
│   └── Health 状态接口
├── RedisTaskQueue for HTTP Producer
│   ├── 长期 redisContext
│   ├── XADD 提交任务
│   ├── GET/HGET 查询状态
│   └── 命令失败自动 reconnect
├── InferenceService
│   └── 管理 3 个 InferenceWorker
├── InferenceWorker × 3
│   ├── 每个 Worker 独立 Yolo11Detector
│   ├── 每个 Worker 独立 RedisTaskQueue
│   ├── 每个 Worker 独立 redisContext
│   ├── XREADGROUP / XAUTOCLAIM 获取任务
│   ├── markRunning / markDone / markFailed
│   └── XACK 确认任务
└── Redis Stream
    ├── yolo:stream:detect
    ├── yolo11_group
    └── worker_1 / worker_2 / worker_3
```

### 连接复用前后的差异

| 对比项 | Phase 5 | Phase 6 |
|---|---|---|
| Redis 连接方式 | 多数命令临时创建连接 | 每个 RedisTaskQueue 复用长期连接 |
| 高压下风险 | 可能出现 `address in use` | 标准与极限压测均未出现任务失败 |
| Worker Redis 使用 | 任务处理链路存在连接压力 | 每个 Worker 独立连接，职责更清楚 |
| 异常恢复 | 依靠 Pending reclaim 兜底 | 连接复用 + reconnect + Pending reclaim |
| 吞吐表现 | 约 33–35 QPS | 标准压测提升到约 78 QPS |
| 极限测试 | 3000 任务出现 timeout | 3000 任务全部 done |

这一步的意义是：系统不再只是“能通过 Redis Stream 跑通异步任务”，而是开始关注连接生命周期、异常恢复和高压下的资源复用。

### 推荐配置

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

注意：如果 WSL IP 变化，需要重新执行：

```bash
hostname -I
```

然后同步修改 `redis.host`。

### Health 验收

Phase 6 中，健康检查应看到：

```json
{
  "success": true,
  "status": "ok",
  "phase": "phase6_redis_connection_reuse",
  "queue_backend": "redis_stream",
  "redis_ping": "ok",
  "redis_pending": 0,
  "redis_stream_len": 10012,
  "redis_stream_max_len": 10000,
  "redis_pending_reclaim": true,
  "redis_pending_min_idle_ms": 60000,
  "worker_num": 3
}
```

重点观察：

```text
phase = phase6_redis_connection_reuse
redis_ping = ok
redis_pending = 0
worker_num = 3
redis_stream_len ≈ 10000
```

### 单任务验证结果

单任务查询示例：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260706_142058_1"
```

关键返回：

```text
status = done
num_detections = 3
worker_id = 1
consumer_name = worker_1
queue_wait_ms = 2
total_ms = 71
bbox_coordinate_system = original_image_pixels
```

这说明异步链路完整闭环：

```text
HTTP 提交任务
-> Redis Stream 入队
-> worker_1 消费
-> TensorRT 推理
-> 保存结果图
-> 写回 Redis result/meta/status
-> 查询接口返回 done
```

### Phase 6 压测结果

| 场景 | Submitted | Done/Failed/Timeout | QPS | avg total_ms | avg queue_wait_ms | avg inference_ms | Worker 分布 |
|---:|---:|---:|---:|---:|---:|---:|---|
| 1000 tasks / concurrency=10 | 1000/1000 | 1000/0/0 | 78.354 | 268.695 | 253.579 | 3.724 | 332 / 335 / 333 |
| 1000 tasks / concurrency=20 | 1000/1000 | 1000/0/0 | 77.986 | 274.207 | 259.444 | 3.738 | 332 / 338 / 330 |
| 3000 tasks / concurrency=20 | 3000/3000 | 3000/0/0 | 71.856 | 544.305 | 528.604 | 3.931 | 999 / 1006 / 995 |

Phase 6 的最关键变化是：`3000 tasks / concurrency=20` 极限测试中，任务全部完成，没有 failed，也没有 timeout。

### Redis 最终验收

极限压测后执行：

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:detect
```

最终结果：

```text
XPENDING = 0

worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0

XLEN = 10026
```

其中 `XLEN=10026` 是正常结果，因为项目使用的是：

```text
XTRIM MAXLEN ~ 10000
```

`~` 表示近似裁剪，Stream 长度会稳定在 10000 附近，而不是严格等于 10000。

### Phase 6 结论

```text
Phase 6 已完成并通过验收。
```

本阶段通过 Redis 连接复用优化，将标准压测吞吐从 Phase 5 的约 33–35 QPS 提升至约 78 QPS；在 `3000 tasks / concurrency=20` 极限场景下，3000 个任务全部完成，Failed=0，Timeout=0，XPENDING=0，三个 Worker 的 pending 均为 0。说明当前 Redis Stream 异步推理链路在连接复用后具备更好的高压稳定性。

### Phase 6 学习反思

1. Redis Stream 不只是队列，Redis 连接本身也是系统资源。高压下频繁创建短连接会成为瓶颈。
2. Pending reclaim 是异常兜底机制，但不能代替正常的连接生命周期管理。
3. 工程优化不能只看模型推理耗时，Redis、HTTP、文件 I/O、日志和连接管理都会影响整体吞吐。
4. `queue_wait_ms` 仍然大于 `inference_ms`，说明系统瓶颈已经从 TensorRT 推理转向服务链路。
5. 每个 Worker 独立 Detector、独立 Redis 连接，是当前单进程多 Worker 架构下更清晰、更稳妥的设计。
6. 压测时必须同时看 QPS、Failed、Timeout、XPENDING、Worker 分布和 Stream 长度，不能只看一个数字。
7. Phase 6 是从“功能跑通”走向“工程稳定”的关键阶段。

---

## Phase 7 Server / Worker 独立进程拆分与 Redis Binary 图片存储

### 阶段定位

Phase 7 的核心目标是把前面阶段已经跑通的 all-in-one 服务进一步拆成更接近生产部署的两类进程：

```text
yolo11_server.exe  -> HTTP Producer / Query
 yolo11_worker.exe -> Redis Stream Consumer / TensorRT Inference Worker
```

在 Phase 6 中，HTTP Server、Redis Producer、InferenceService 和多个 InferenceWorker 仍然位于同一个进程中。虽然功能和压测已经稳定，但这种结构在部署层面仍然偏本地化：HTTP 接口进程会和 GPU 推理 Worker 绑定在一起，Worker 也依赖 Server 保存的本地图片路径。

Phase 7 的工程意义是：把“一个能跑的 C++ 服务”继续升级为“可拆分、可单独启动、可单独崩溃恢复、可为后续多机器部署打基础的推理系统”。

### 本阶段完成内容

- 新增 `yolo11_worker.exe`，作为独立 Worker 进程入口。
- `yolo11_server.exe` 默认只负责 HTTP 接入、异步任务提交、任务结果查询和结果图返回。
- HTTP Server 生产环境下可以不加载 TensorRT engine，减少 GPU 资源占用。
- 异步图片输入从本地路径传递升级为 Redis Binary Key 存储。
- 新增 Redis 图片 Key：
  - `yolo:image:{task_id}:input`
  - `yolo:image:{task_id}:result`
- 新增结果图接口：`GET /api/v1/result/{task_id}/image`。
- Worker 从 Redis 读取图片 bytes，OpenCV 解码后执行 TensorRT 推理。
- Worker 推理完成后将结果图重新编码为 JPEG bytes 写回 Redis。
- 完成 1000、3000、5000 级别压测。
- 完成 Worker 强杀恢复测试。
- 修复 Debug 版本下 Ctrl+C 结束 Worker 触发 Visual C++ Runtime abort 弹窗的问题。

### Phase 7 新架构

```text
yolo11_server.exe
├── Crow HTTP Server
│   ├── GET  /api/v1/health
│   ├── POST /api/v1/detect/image/async
│   ├── GET  /api/v1/result/{task_id}
│   └── GET  /api/v1/result/{task_id}/image
├── RedisTaskQueue for HTTP Producer / Query
│   ├── SETEX yolo:image:{task_id}:input
│   ├── XADD yolo:stream:detect
│   ├── GET yolo:task:{task_id}:result
│   └── GET yolo:image:{task_id}:result
└── 不再默认加载 TensorRT Detector

Redis Stream / Redis Binary Keys
├── yolo:stream:detect
├── yolo:task:{task_id}:status
├── yolo:task:{task_id}:meta
├── yolo:task:{task_id}:result
├── yolo:image:{task_id}:input
└── yolo:image:{task_id}:result

yolo11_worker.exe
├── InferenceWorker
│   ├── 独立 consumer_name，例如 worker_1
│   ├── 独立 RedisTaskQueue 长连接
│   ├── 独立 Yolo11Detector
│   ├── XREADGROUP / XAUTOCLAIM
│   ├── GET input image bytes
│   ├── TensorRT 推理
│   ├── SETEX result image bytes
│   ├── markDone / markFailed
│   └── XACK
└── Ctrl+C 优雅退出
```

### 为什么要从本地路径改成 Redis Binary 图片存储

Phase 6 之前的异步任务中，Worker 主要通过 `input_image_path` 找到 HTTP Server 保存到 `./temp/input` 的图片。这在单机单进程或同一机器上没有问题，但一旦 Server 和 Worker 拆成两个进程，尤其后续放到不同机器或容器中，本地路径就会失效。

Phase 7 改成 Redis Binary Key 后，任务传递变成：

```text
HTTP Server 接收图片 bytes
↓
OpenCV 校验图片有效性
↓
Redis SETEX yolo:image:{task_id}:input
↓
XADD yolo:stream:detect
↓
Worker XREADGROUP 领取任务
↓
Redis GET yolo:image:{task_id}:input
↓
OpenCV imdecode
↓
TensorRT 推理
↓
OpenCV imencode 结果图
↓
Redis SETEX yolo:image:{task_id}:result
↓
HTTP Server 通过 task_id 返回结果图
```

这个设计的价值是：Worker 不再依赖 Server 的本地磁盘路径，为后续多进程、多机器、容器化、对象存储替换打基础。

### Phase 7 推荐启动方式

先启动 Redis：

```bash
sudo service redis-server start
redis-cli ping
hostname -I
```

启动 HTTP Server：

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml
```

分别启动 3 个独立 Worker。推荐用三个 PowerShell 窗口：

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --consumer-name worker_1
```

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --consumer-name worker_2
```

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --consumer-name worker_3
```

本地调试也可以用一个 Worker 进程启动多个内部 Worker：

```powershell
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --worker-num 3
```

但做崩溃恢复测试时，更推荐三个独立 Worker 进程，因为这样可以只强杀其中一个 Worker，不影响其他 Worker。

### Phase 7 API 变化

异步提交任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

返回示例：

```json
{
  "image_storage": "redis_binary_keys",
  "input_image_key": "yolo:image:20260706_152358_460_1:input",
  "result_image_key": "yolo:image:20260706_152358_460_1:result",
  "queue_backend": "redis_stream",
  "result_image_url": "/api/v1/result/20260706_152358_460_1/image",
  "result_url": "/api/v1/result/20260706_152358_460_1",
  "status": "queued",
  "success": true,
  "task_id": "20260706_152358_460_1"
}
```

查询结果：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260706_152358_460_1"
```

下载结果图：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/result/20260706_152358_460_1/image" -o result.jpg
start .\result.jpg
```

注意：不要把 `<task_id>` 原样复制到命令里，要换成实际返回的任务 ID。

### Phase 7 压测结果

| 场景 | Submitted | Done/Failed/Timeout | QPS | avg total_ms | avg queue_wait_ms | avg inference_ms | Worker 分布 |
|---:|---:|---:|---:|---:|---:|---:|---|
| 1000 tasks / concurrency=10 | 1000/1000 | 1000/0/0 | 68.261 | 467.554 ms | 446.909 ms | 5.279 ms | 332 / 333 / 335 |
| 3000 tasks / concurrency=20 | 3000/3000 | 3000/0/0 | 60.948 | 649.988 ms | 628.005 ms | 4.202 ms | 997 / 1002 / 1001 |
| 5000 tasks / concurrency=20，中途强杀一个 Worker | 5000/5000 | 5000/0/0 | 57.398 | 15732.293 ms | 15709.110 ms | 5.928 ms | 3507 / 182 / 1311 |

从结果可以看到，Phase 7 的 QPS 比 Phase 6 略低，这是合理的。因为 Phase 7 不再通过本地路径传图，而是把输入图和结果图都写入 Redis Binary Key。这个设计增加了 Redis 网络传输、OpenCV 编解码和二进制读写开销，但换来了进程解耦和后续分布式部署能力。

### Worker 强杀恢复测试

测试方法：启动 1 个 `yolo11_server.exe` 和 3 个独立 `yolo11_worker.exe`，压测过程中强杀 `worker_2` 进程：

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.Name -eq "yolo11_worker.exe" -and $_.CommandLine -like "*worker_2*" } |
  Select-Object ProcessId, CommandLine

Stop-Process -Id <PID> -Force
```

最终压测结果：

```text
Submitted ok:        5000/5000
Done/Failed/Timeout: 5000/0/0
Throughput QPS:      57.398
Worker distribution: {'worker_1': 3507, 'worker_2': 182, 'worker_3': 1311}
```

这个结果说明：`worker_2` 中途退出后，剩余 Worker 继续消费任务，Pending 任务能够被其他 Worker 回收并最终完成。`queue_wait_ms` 明显升高是正常现象，因为 Worker 数量减少且 Pending 回收需要等待 `pending_min_idle_ms`。

### Redis 最终验收

压测结束后执行：

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XLEN yolo:stream:detect
```

最终应看到：

```text
XPENDING = 0
worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0
XLEN ≈ 10000
```

`XLEN` 不需要严格等于 10000，因为项目使用的是近似裁剪：

```text
XTRIM MAXLEN ~ 10000
```

### Debug 弹窗修复

测试中发现，Debug 版本下直接 Ctrl+C 结束 `yolo11_worker.exe` 可能弹出：

```text
Microsoft Visual C++ Runtime Library
Debug Error!
abort() has been called
```

这个弹窗不适合无人值守服务和生产部署，因为它会阻塞进程退出。Phase 7 后续补充了以下修复：

- 在 `main_worker.cpp` 中配置 Windows `SetErrorMode`，避免错误弹窗阻塞。
- 通过 `_set_abort_behavior` 抑制 CRT abort 弹窗。
- Debug 模式下将 CRT report 输出到 stderr。
- 使用 `condition_variable` 处理 Ctrl+C 停止信号。
- `InferenceWorker::stop()`、析构和 Detector 释放流程改为 `noexcept` 兜底。
- `detector.release()` 放入安全释放函数，避免析构阶段异常触发 `std::terminate`。

修复后的区别：

```text
Ctrl+C                  -> 测试优雅退出，不应该弹窗
Stop-Process -Force     -> 测试崩溃恢复，进程直接死亡是正常现象
```

### Phase 7 学习反思

1. Server / Worker 分离不是简单多一个 exe，而是服务职责边界的重划分。
2. HTTP Server 不应该长期占用 GPU；生产部署中它更适合只做任务提交和查询。
3. 本地文件路径在单机阶段很方便，但不是跨进程、跨机器部署的稳定接口。
4. Redis Binary 图片存储牺牲了一部分 QPS，但换来了进程解耦和部署弹性。
5. `queue_wait_ms` 仍然远大于 `inference_ms`，说明系统瓶颈主要在队列和服务链路，不在 TensorRT 单次推理。
6. 崩溃恢复测试要用 `Stop-Process -Force`，Ctrl+C 应该用于测试优雅退出。
7. Debug 版本下的弹窗也需要处理，因为它会影响自动化测试和无人值守服务。
8. 生产化不是只看功能能不能跑，而是看能不能退出、能不能恢复、能不能观测、能不能长期运行。

### Phase 7 结论

```text
Phase 7 已完成核心验收。
```

本阶段将系统从 all-in-one 单进程架构升级为 Server / Worker 分离架构；`yolo11_server.exe` 负责 HTTP 接入、任务提交、状态查询和结果图返回，`yolo11_worker.exe` 负责 Redis Stream 消费、TensorRT 推理、结果写回和 XACK 确认。异步图片通过 Redis Binary Key 存储，避免 Worker 依赖 Server 本地路径。1000、3000、5000 级别压测均全部完成，Worker 强杀恢复测试通过，Redis 最终 `XPENDING=0`，说明当前 detect 图片异步服务已经具备较好的工程化部署基础。


---


## Phase 8 / Phase 8.5 健康检查、Worker 心跳、Redis TTL、日志、labels_path 与 metrics

### 阶段定位

Phase 8 和 Phase 8.5 的目标不是继续堆新模型接口，而是把前面已经跑通的 detect 图片异步服务进一步做成“能观测、能拒绝异常请求、能控制 Redis 内存、能定位问题、能给部署人员看懂状态”的工程化服务。

可以概括为：

```text
Phase 8：健康检查增强 + Worker 心跳 + Redis 图片 TTL / 内存控制 + Worker 离线拒绝入队
Phase 8.5：spdlog 日志系统 + labels_path 标签外置 + ResultSerializer 收口 + /api/v1/metrics 指标接口
```

这两个阶段的意义是：系统不再只是“能完成异步推理”，而是开始具备生产服务常见的健康检查、就绪判断、运行指标、日志追踪和资源保护能力。

### Phase 8 完成内容

Phase 8 在 Phase 7 的 Server / Worker 分离基础上，新增了以下能力：

- `/api/v1/health`：Server 基础健康检查，包含 Redis ping、Stream 长度、Pending 数量、Redis 内存和当前 phase。
- `/api/v1/ready`：系统是否可以接收新任务，综合判断 Server、Redis、Worker 和 Redis 内存状态。
- `/api/v1/workers`：查看 Worker 心跳、PID、host、gpu_id、status、current_task_id、processed_count、failed_count 等信息。
- Worker 心跳：每个 Worker 定期写入 `yolo:worker:{consumer_name}:heartbeat`，并设置 TTL。
- Worker 掉线检测：heartbeat key 过期后，Server 能判断该 Worker 不再 alive。
- Worker 离线保护：当 `alive_workers < min_alive_workers` 时，异步提交接口返回 HTTP 503，不再继续写入 Redis Stream。
- Redis 图片 TTL：input image 和 result image 使用不同 TTL，避免 Redis 图片无限堆积。
- input image 清理：任务完成并成功 `XACK` 后删除 `yolo:image:{task_id}:input`。
- Redis 内存保护：当 `used_memory` 超过 `max_redis_used_memory_mb` 时，`/ready=false`，异步提交应拒绝新任务。
- 任务确认逻辑修正：`markDone/markFailed` 成功后才执行 `XACK`，避免“结果没写好但消息已确认”的任务丢失风险。

### Phase 8 核心配置

```yaml
redis:
  task_ttl_seconds: 1800
  input_image_ttl_seconds: 600
  result_image_ttl_seconds: 1800
  max_image_bytes: 5242880
  max_result_image_bytes: 5242880
  delete_input_after_done: true
  max_redis_used_memory_mb: 2048

worker:
  worker_num: 3
  min_alive_workers: 1
  heartbeat_enabled: true
  heartbeat_interval_ms: 3000
  heartbeat_ttl_seconds: 15
```

这些配置的核心含义：

| 配置 | 含义 |
|---|---|
| `input_image_ttl_seconds` | 输入图最长保留时间，避免 Worker 异常时图片永久残留 |
| `result_image_ttl_seconds` | 结果图保留时间，给客户端下载留窗口 |
| `delete_input_after_done` | 任务成功完成后删除输入图 |
| `max_image_bytes` | 限制上传图片大小 |
| `max_redis_used_memory_mb` | Redis 内存保护阈值 |
| `heartbeat_interval_ms` | Worker 写心跳间隔 |
| `heartbeat_ttl_seconds` | Worker 心跳 key 过期时间 |
| `min_alive_workers` | 系统最少需要多少 Worker 在线才算 ready |

### Phase 8 Worker 心跳设计

每个 Worker 会写入类似：

```text
yolo:worker:worker_1:heartbeat
yolo:worker:worker_2:heartbeat
yolo:worker:worker_3:heartbeat
```

字段包括：

```text
consumer_name
pid
host
worker_id
gpu_id
model_type
status
current_task_id
processed_count
failed_count
start_time_ms
last_heartbeat_ms
last_error
```

验证命令：

```bash
redis-cli -h 172.19.196.109 -p 6379 KEYS 'yolo:worker:*:heartbeat'
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:worker:worker_1:heartbeat
redis-cli -h 172.19.196.109 -p 6379 TTL yolo:worker:worker_1:heartbeat
```

阶段测试中，3 个 Worker 心跳均存在，TTL 为 15 秒。停止 Worker 后，heartbeat key 自动过期，`/workers` 显示 `alive=false`，`/ready` 返回 `ready=false`。

### Phase 8 Worker 离线拒绝入队

初版 Phase 8 测试时发现一个重要问题：

```text
/ready = false
alive_workers = 0
但是 POST /api/v1/detect/image/async 仍然返回 queued
```

这说明 Server 已经能判断 Worker 不在线，但异步提交接口还没有使用该判断。修复后，当 Worker 全部离线时：

```powershell
curl.exe -i -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

返回：

```text
HTTP/1.1 503 Service Unavailable
```

JSON：

```json
{
  "success": false,
  "error_code": "NOT_ENOUGH_ALIVE_WORKERS",
  "error": "async task rejected because not enough alive workers",
  "ready": false,
  "reason": "not enough alive workers",
  "alive_workers": 0,
  "min_alive_workers": 1,
  "worker_status": "no_enough_alive_workers"
}
```

这个修复让 Phase 8 形成完整闭环：

```text
Server ok + Redis ok + Worker 足够 + Redis 内存正常 -> 允许入队
Server ok + Redis ok + Worker 不足 -> 拒绝入队，返回 503
Server ok + Redis 内存超限 -> 拒绝入队，返回 503
```

### Phase 8 Redis 图片 TTL 与内存控制验证

完成任务后验证：

```bash
redis-cli -h 172.19.196.109 -p 6379 EXISTS yolo:image:20260706_165348_773_4:input
redis-cli -h 172.19.196.109 -p 6379 EXISTS yolo:image:20260706_165348_773_4:result
redis-cli -h 172.19.196.109 -p 6379 TTL yolo:image:20260706_165348_773_4:result
```

结果：

```text
input exists = 0
result exists = 1
result TTL = 1726
```

说明：

```text
任务完成后 input image 已被删除
result image 继续保留一段时间供客户端下载
Redis Binary 图片不会无限堆积
```

### Phase 8 标准压测结果

```text
Tasks:       1000
Concurrency: 10
Submitted ok: 1000/1000
Done/Failed/Timeout: 1000/0/0
Throughput QPS: 60.373
Worker distribution: worker_1=336, worker_2=331, worker_3=333
```

服务端耗时：

```text
total_ms avg = 52.225 ms
queue_wait_ms avg = 30.287 ms
inference_ms avg = 3.995 ms
```

Redis 验收：

```text
XPENDING = 0
worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0
XLEN ≈ 10000
Redis used_memory ≈ 53 MB
```

Phase 8 结论：健康检查、Worker 心跳、Redis 图片 TTL、Redis 内存控制、Worker 离线拒绝入队和任务确认逻辑修正均已通过。

---

### Phase 8.5 完成内容

Phase 8.5 主要做工程收口和可观测性增强：

- 引入 `spdlog`。
- 拆分 `logs/server.log` 与 `logs/worker.log`。
- 新增 `labels/coco.txt`。
- 新增 `model.labels_path` 配置项。
- 新增 `LabelMap`，将 `class_id -> class_name` 从代码硬编码中抽离。
- 统一 `ResultSerializer`，让同步接口和 Worker 结果 JSON 使用同一套序列化逻辑。
- 新增 `/api/v1/metrics`，返回累计任务、失败数、近期 QPS、平均耗时、Worker 分布和 Redis 状态。
- 新增 Redis metrics key：`yolo:metrics:global`、`yolo:metrics:worker:done`、`yolo:metrics:worker:failed`、`yolo:metrics:recent:done`。

### Phase 8.5 日志系统

新增依赖：

```bat
.\vcpkg.exe install spdlog:x64-windows --vcpkg-root D:\vcpkg
```

日志文件：

```text
logs/server.log
logs/worker.log
```

验证命令：

```powershell
Get-ChildItem .\logs
Get-Content .\logs\server.log -Tail 20
Get-Content .\logs\worker.log -Tail 20
```

实际结果：

```text
server.log = 2095 bytes
worker.log = 1923 bytes
```

日志拆分效果：

| 日志文件 | 主要内容 |
|---|---|
| `server.log` | HTTP Server 启动、Redis Producer、异步任务提交、API 信息 |
| `worker.log` | Worker 进程启动、Redis 配置、TensorRT engine 加载、labels 加载、worker_1/2/3 启动 |

这一步的意义是：后续出问题时不再只能看控制台，而是可以通过日志追踪 Server 和 Worker 各自的行为。

### Phase 8.5 labels_path 标签外置

之前 `class_name` 如果写死在代码里，换自定义模型或 OBB 模型时容易出现 `class_id` 对但 `class_name` 错的问题。

现在配置为：

```yaml
model:
  labels_path: "D:/tensorrtx/yolo11/labels/coco.txt"
```

Health 验证：

```json
{
  "labels_loaded": true,
  "label_count": 80,
  "labels_path": "D:/tensorrtx/yolo11/labels/coco.txt"
}
```

检测结果：

```json
{
  "class_id": 0,
  "class_name": "person"
}
```

现在 `class_name=person` 来自 `labels/coco.txt`，不再来自硬编码数组。

### Phase 8.5 metrics 指标接口

新增接口：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/metrics"
```

返回关键字段：

```json
{
  "success": true,
  "phase": "phase8_5_logging_labels_metrics",
  "metrics_enabled": true,
  "total_tasks_done": 1101,
  "total_tasks_failed": 0,
  "qps_recent": 16.666666666666668,
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

这里要注意：`qps_recent` 是按照最近 60 秒窗口计算的，例如：

```text
1000 / 60 = 16.666
```

而压测脚本里的 QPS 是按本次压测实际完成耗时计算的，所以二者口径不同，不冲突。

Redis metrics 验证：

```bash
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:global
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:worker:done
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:worker:failed
redis-cli -h 172.19.196.109 -p 6379 ZCARD yolo:metrics:recent:done
```

实际结果：

```text
done_count = 1101
worker_1 = 368
worker_2 = 369
worker_3 = 364
failed = empty
```

### Phase 8.5 标准压测结果

```powershell
python .\tools\benchmark_async.py `
  --url http://127.0.0.1:8080 `
  --image D:/tensorrtx/yolo11/images/bus.png `
  --tasks 1000 `
  --concurrency 10
```

结果：

```text
Submitted ok: 1000/1000
Done/Failed/Timeout: 1000/0/0
Throughput QPS: 59.177
Worker distribution: worker_1=334, worker_2=335, worker_3=331
```

服务端耗时：

```text
total_ms avg = 272.631 ms
queue_wait_ms avg = 251.634 ms
inference_ms avg = 4.335 ms
```

Redis 最终状态：

```text
XPENDING = 0
worker_1 pending = 0
worker_2 pending = 0
worker_3 pending = 0
XLEN = 10000
Redis used_memory ≈ 53.37 MB
```

### Phase 8 / 8.5 关键问题与反思

#### 1. `/health` 和 `/ready` 不是一回事

`/health` 只能说明 Server 进程和基础依赖还活着，不能说明系统能接任务。真正能不能接任务，要看 `/ready`。

例如 Worker 全部离线时：

```text
server_status = ok
redis_status = ok
alive_workers = 0
ready = false
```

反思：生产服务里“活着”和“可服务”必须分开判断。

#### 2. Worker 心跳比 `XINFO CONSUMERS inactive` 更适合判断 Worker 是否活着

Redis Stream 的 `XINFO CONSUMERS` 可以看 pending 和 idle，但 Worker 进程是否真的在线，更适合用 heartbeat key 判断。

反思：消息队列里的 consumer 记录不等于真实进程状态，心跳机制是服务治理中非常重要的一环。

#### 3. XACK 的时机必须非常谨慎

如果结果 JSON、meta、status 或 result image 还没写好就 `XACK`，消息会从 Pending 中消失，后续无法恢复。

正确逻辑是：

```text
markDone/markFailed 成功 -> XACK
markDone/markFailed 失败 -> 不 XACK，等待 XAUTOCLAIM
```

反思：消息确认代表“系统已经可靠完成该任务”，不能过早确认。

#### 4. Redis Binary 图片一定要有 TTL

1000 个任务压测后 Redis 内存约 53 MB，这还是图片较小的情况下。如果没有 TTL，长时间运行后 Redis 会不断膨胀。

反思：把 Redis 当图片存储用可以快速实现进程解耦，但必须配合 TTL、大小限制和内存保护。

#### 5. Worker 离线时继续入队是一种隐性故障

初版 Phase 8 中，Worker 全部离线时异步提交仍返回 `queued`。表面看请求成功了，实际任务没人处理，会导致队列堆积和用户等待。

修复后，Worker 不足直接返回 503。

反思：服务端应该尽早失败，而不是制造“看起来成功”的假成功。

#### 6. 日志不是锦上添花，而是工程系统的基础设施

Phase 8.5 后，Server 和 Worker 有了独立日志文件。后续排查问题时可以区分：

```text
请求有没有进 Server？
任务有没有入 Redis？
Worker 有没有加载 engine？
Worker 有没有消费任务？
labels 有没有加载成功？
```

反思：没有日志的服务，后续越复杂越难维护。

#### 7. class_name 不应该硬编码在业务逻辑里

检测模型、OBB 模型和自定义模型的类别表不同。如果 `class_name` 写死在代码中，换模型时容易出现语义错误。

反思：模型权重、engine、类别数、labels_path 必须作为一组配置统一管理。

#### 8. metrics 让系统从“能跑”变成“能解释”

通过 `/api/v1/metrics` 可以看到：

```text
总任务数
失败数
Worker 分布
平均 queue_wait_ms
平均 inference_ms
平均 total_ms
Redis pending
Redis memory
```

这样才能判断瓶颈到底在模型推理、队列等待、Redis、还是 Worker 数量。

反思：工程优化不能靠感觉，必须靠指标。

### Phase 8.5 阶段结论

```text
Phase 8.5 已完成并通过验收。
```

本阶段在 Phase 8 健康检查、Worker 心跳和 Redis 图片 TTL 的基础上，进一步完成了 spdlog 日志系统、labels_path 标签外置、ResultSerializer 收口和 `/api/v1/metrics` 指标接口。标准压测 `1000 tasks / concurrency=10` 结果为 `1000/0/0`，QPS=59.177，Redis `XPENDING=0`，三个 Worker pending 均为 0，Worker 分布均衡，说明当前 detect 图片异步推理服务已经具备较完整的可观测性和工程稳定性。

下一阶段建议进入：

```text
Phase 9：ImageStorage 抽象与存储层解耦
```

也就是把当前写在业务逻辑中的 Redis Binary 图片读写抽象为统一接口，为后续 LocalFileStorage、RedisImageStorage、MinIO / S3 或共享磁盘存储打基础。


---

## Phase 10 / 10.5 / 11 / 12：OBB 服务化、多模型 Runner、稳定性验证与双服务并行

### 阶段总定位

从 Phase 8.5 之后，项目跳过原先计划中的独立 Phase 9，直接进入 OBB 图片异步服务化。这里的核心不是“把 OBB demo 接到 HTTP 上这么简单”，而是让 OBB 复用前面已经验证过的 Server / Worker、Redis Stream、Redis Binary Image、heartbeat、ready、TTL、logging、labels_path、ResultSerializer 和 metrics 机制。

这几个阶段可以概括为：

```text
Phase 10.0：OBB 图片异步服务最小闭环
Phase 10.5：多模型配置与 Runner 抽象整理
Phase 11.0：OBB 稳定性、异常输入与 Worker 恢复验证
Phase 12.0：Detect + OBB 双服务并行部署
```

### Phase 10.0：OBB 图片异步服务最小闭环

Phase 10.0 的目标是让 OBB 图片检测像 Detection 图片检测一样，通过 HTTP 异步提交任务，由独立 Worker 消费 Redis Stream，执行 TensorRT OBB 推理，并返回结构化 JSON 与结果图。

新增核心接口：

```text
POST /api/v1/detect/obb/async
GET  /api/v1/result/{task_id}
GET  /api/v1/result/{task_id}/image
```

推荐配置：

```yaml
model:
  type: "obb"
  engine_path: "D:/tensorrtx/yolo11/engines/yolo11n-obb.engine"
  labels_path: "D:/tensorrtx/yolo11/labels/dota.txt"
  gpu_id: 0
  use_gpu_postprocess: false

redis:
  stream_key: "yolo:stream:obb"
  consumer_group: "yolo11_obb_group"

worker:
  consumer_name_prefix: "obb_worker_"
```

启动方式：

```powershell
# OBB Server
.\out\build\x64-Debug\yolo11_server.exe .\config\server_obb.yaml

# OBB Worker
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker_obb.yaml --consumer-name obb_worker_1
```

提交 OBB 异步任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/obb/async" `
  -H "Content-Type: image/jpeg" `
  --data-binary "@D:/tensorrtx/yolo11/images/a.jpg"
```

结果 JSON 中新增 OBB 语义字段：

```json
{
  "model_type": "obb",
  "obb_coordinate_system": "original_image_pixels",
  "obb_format": "cxcywh_angle_points",
  "detections": [
    {
      "class_id": 0,
      "class_name": "plane",
      "confidence": 0.91,
      "obb": {
        "cx": 512.4,
        "cy": 336.7,
        "w": 128.2,
        "h": 42.5,
        "angle": -18.3,
        "points": [[460,330],[582,292],[594,342],[472,380]]
      },
      "bbox_axis_aligned": { "x1": 460, "y1": 292, "x2": 594, "y2": 380 }
    }
  ]
}
```

阶段反思：OBB 第一版不应该追求极限性能，而应该优先确认类别数、labels、engine、后处理、旋转框坐标、结果图和 Redis 状态流转是否正确。

### Phase 10.5：多模型配置与 Runner 抽象整理

Phase 10.0 跑通后，如果继续在 `InferenceWorker` 里堆：

```cpp
if (model_type == "detect") { ... }
else if (model_type == "obb") { ... }
```

后面再接 Seg、Pose、Video 时，Worker 会越来越乱。因此 Phase 10.5 引入多模型 Runner 抽象。

新增核心设计：

```cpp
class IModelRunner {
public:
    virtual ~IModelRunner() = default;
    virtual bool load(const ModelConfig& config) = 0;
    virtual InferenceResult infer(const cv::Mat& image) = 0;
    virtual cv::Mat drawResult(const cv::Mat& image, const InferenceResult& result) = 0;
    virtual std::string modelType() const = 0;
};
```

具体实现：

```text
DetectModelRunner -> 内部使用 Yolo11Detector
ObbModelRunner    -> 内部使用 Yolo11ObbDetector
```

这样 Worker 的职责变成：

```text
读取 Redis 任务
-> 读取 input image
-> runner_->infer(image)
-> runner_->drawResult(image, result)
-> ResultSerializer 输出 JSON
-> 写回 Redis
-> XACK
```

而不再直接关心具体模型后处理细节。

新增配置文件：

```text
config/server_multimodel.yaml
config/worker_detect.yaml
config/worker_obb.yaml
```

接口增强：

```text
GET /api/v1/ready?model=detect
GET /api/v1/ready?model=obb
GET /api/v1/workers?model=detect
GET /api/v1/workers?model=obb
GET /api/v1/metrics?model=detect
GET /api/v1/metrics?model=obb
```

阶段反思：抽象不是为了“看起来高级”，而是为了让后续扩展不会继续复制粘贴。Phase 10.5 的价值在于把模型差异收敛到 Runner 和 Serializer，不让 Worker 主循环膨胀。

### Phase 11.0：OBB 稳定性、异常输入与 Worker 恢复验证

Phase 11.0 不继续增加功能，而是验证 OBB 单服务是否稳定。新增测试脚本：

```text
tools/phase11_obb_stability_suite.py
tools/phase11_invalid_input_test.py
tools/phase11_worker_recovery_test.py
```

100 任务稳定性测试：

```powershell
python tools\phase11_obb_stability_suite.py `
  --url http://127.0.0.1:8080 `
  --endpoint /api/v1/detect/obb/async `
  --image D:/tensorrtx/yolo11/images/a.jpg `
  --tasks 100 `
  --concurrency 5 `
  --timeout 180 `
  --download-results 3
```

500 任务稳定性测试：

```powershell
python tools\phase11_obb_stability_suite.py `
  --url http://127.0.0.1:8080 `
  --endpoint /api/v1/detect/obb/async `
  --image D:/tensorrtx/yolo11/images/a.jpg `
  --tasks 500 `
  --concurrency 5 `
  --timeout 600 `
  --download-results 5
```

实测结果：

| 测试项 | 结果 |
|---|---|
| OBB 单任务 | `done`，result JSON 与结果图正常返回 |
| 100 tasks / concurrency=5 | `100/0/0`，QPS≈33.047 |
| 500 tasks / concurrency=5 | `500/0/0`，QPS≈34.336 |
| 非法输入 | HTTP 400，`IMAGE_DECODE_FAILED`，`PASS_HTTP_REJECTED` |
| Worker 恢复测试 | 中断 Worker 后重启，100 个任务最终全部 done |
| 最终 Redis | `XPENDING=0`，`XLEN=743` |

非法输入测试：

```powershell
python tools\phase11_invalid_input_test.py `
  --url http://127.0.0.1:8080 `
  --endpoint /api/v1/detect/obb/async
```

返回：

```json
{
  "submit_http_status": 400,
  "submit_response": {
    "error_code": "IMAGE_DECODE_FAILED",
    "success": false
  },
  "verdict": "PASS_HTTP_REJECTED"
}
```

Worker 恢复测试：

```powershell
python tools\phase11_worker_recovery_test.py `
  --url http://127.0.0.1:8080 `
  --endpoint /api/v1/detect/obb/async `
  --image D:/tensorrtx/yolo11/images/a.jpg `
  --tasks 100 `
  --concurrency 10 `
  --kill-window 10 `
  --timeout 240
```

测试过程中关闭 OBB Worker，再重启 Worker。最终：

```text
[PASS] Worker recovery test passed.
```

阶段反思：稳定性测试不能只看 20 个任务能不能 done，还要看非法输入、Worker 中断、Pending 恢复、ready 恢复和 Redis 是否有残留。Phase 11 证明 OBB 服务不仅“能跑”，而且具备基本恢复能力。

### Phase 12.0：Detect + OBB 双服务并行部署

Phase 12 的目标是让 Detect 与 OBB 同时运行，并且互不干扰。最终采用最稳的双 Server + 双 Worker 方案。

部署结构：

| 服务 | 端口 | Stream | Consumer Group | Worker |
|---|---:|---|---|---|
| Detect | 8080 | `yolo:stream:detect` | `yolo11_group` | `worker_1` |
| OBB | 8081 | `yolo:stream:obb` | `yolo11_obb_group` | `obb_worker_1` |

启动四个窗口：

```powershell
# Detect Server
.\out\build\x64-Debug\yolo11_server.exe .\config\server_detect_phase12.yaml

# OBB Server
.\out\build\x64-Debug\yolo11_server.exe .\config\server_obb_phase12.yaml

# Detect Worker
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker_detect_phase12.yaml --consumer-name worker_1

# OBB Worker
.\out\build\x64-Debug\yolo11_worker.exe .\config\worker_obb_phase12.yaml --consumer-name obb_worker_1
```

健康检查：

```powershell
# Detect
curl.exe "http://127.0.0.1:8080/api/v1/health"
curl.exe "http://127.0.0.1:8080/api/v1/ready?model=detect"
curl.exe "http://127.0.0.1:8080/api/v1/workers?model=detect"
curl.exe "http://127.0.0.1:8080/api/v1/metrics?model=detect"

# OBB
curl.exe "http://127.0.0.1:8081/api/v1/health"
curl.exe "http://127.0.0.1:8081/api/v1/ready?model=obb"
curl.exe "http://127.0.0.1:8081/api/v1/workers?model=obb"
curl.exe "http://127.0.0.1:8081/api/v1/metrics?model=obb"
```

双服务 smoke test：

```powershell
python tools\phase12_dual_service_test.py `
  --detect-url http://127.0.0.1:8080 `
  --obb-url http://127.0.0.1:8081 `
  --detect-image D:/tensorrtx/yolo11/images/bus.png `
  --obb-image D:/tensorrtx/yolo11/images/a.jpg `
  --timeout 120
```

实测结果：

```text
[PASS] Phase 12 dual-service smoke test passed.
Detect: status=done, model_type=detect, num_detections=3
OBB:    status=done, model_type=obb,    num_detections=0
```

双服务 benchmark：

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

实测结果：

```text
Detect: 50 done / 0 failed / 0 timeout
OBB:    50 done / 0 failed / 0 timeout
Total:  100 done, QPS≈31.866
```

Redis 验收：

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:obb yolo11_obb_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:detect yolo11_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:obb yolo11_obb_group
```

最终：

```text
Detect XPENDING = 0
OBB XPENDING = 0
worker_1 pending = 0
obb_worker_1 pending = 0
```

阶段反思：双服务并行不是把两个模型塞进同一个队列，而是要让端口、配置、Stream、Consumer Group、Worker、labels、metrics 都清晰隔离。这样后续扩展视频或多 GPU 时，系统边界才不会糊成一团。

### Phase 10 到 12 阶段总反思

1. OBB 服务化的难点不在 HTTP 路由，而在类别数、labels、engine、后处理、结果图和 JSON 字段的一致性。
2. 多模型扩展不能靠复制粘贴堆 `if/else`，需要尽早抽象 `IModelRunner`。
3. `/ready?model=...` 和 `/metrics?model=...` 非常重要，因为多模型服务中“某一个模型不可用”不等于整个 Server 死了。
4. 非法输入测试是必要的。坏图应该在 HTTP 层明确拒绝，而不是交给 Worker 崩溃。
5. Worker 恢复测试是异步服务的底线。只要任务可能进入 Pending，就必须验证 XAUTOCLAIM 和 XACK 顺序。
6. Detect 与 OBB 应使用独立 Stream 和 Consumer Group，避免不同模型互相影响。
7. `num_detections=0` 不等于服务失败，要结合 `status=done`、`model_type`、结果图和测试图片领域判断。
8. Redis 中旧 consumer 的 `inactive` 很大不是问题，只要 `pending=0` 就不会影响当前服务。
9. 脚本里 Windows 找不到 `redis-cli` 不影响服务本身，可以在 WSL 中手动查 Redis，或后续让脚本支持 `wsl redis-cli`。
10. Phase 12 之后，项目已经具备 Detect + OBB 双模型图片服务基础，下一步再进入视频服务才更稳。


## Phase 13 / 13.5：Detect 视频文件异步检测、稳定性增强与目录结构整理

### 阶段总定位

Phase 13 是从“图片异步服务”进入“视频文件异步服务”的阶段。前面 Phase 12 已经完成 Detect + OBB 双服务并行，说明图片任务的 Server / Worker、Redis Stream、心跳、ready、metrics、labels 和结果图下载都比较稳定。Phase 13 的目标不是一上来做 RTSP，而是先做更可控的视频文件异步任务。

这几个阶段可以概括为：

```text
Phase 13.0：Detect 视频文件异步检测最小闭环
Phase 13.5：视频任务稳定性、取消、异常输入、Worker 恢复与批量压测
Structure Cleanup：config 清理与 runtime 目录统一
```

核心原则是：**视频文件不放 Redis Binary，Redis 只保存任务状态和元信息；真实 input/output 视频保存在本地 runtime 目录。**

---

### Phase 13.0：视频文件异步检测最小闭环

Phase 13.0 新增了一个独立的视频异步服务：

```text
Video Server：yolo11_server.exe + config/server_video.yaml，端口 8082
Video Worker：yolo11_video_worker.exe + config/worker_video.yaml
Redis Stream：yolo:stream:video:detect
Consumer Group：yolo11_video_detect_group
Consumer Name：video_worker_1
```

新增接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| POST | `/api/v1/detect/video/async` | 上传视频文件并提交异步检测任务 |
| GET | `/api/v1/video/result/{task_id}` | 查询视频任务状态、进度、耗时和结果路径 |
| GET | `/api/v1/video/result/{task_id}/file` | 下载带检测框的结果视频 |
| POST | `/api/v1/video/result/{task_id}/cancel` | 请求取消视频任务 |

视频任务状态机：

```text
queued -> running -> done
queued -> running -> failed
queued/running -> canceled
```

与图片任务不同，视频任务的耗时更长，所以结果查询必须包含进度字段：

```text
total_frames
processed_frames
progress
fps
width
height
duration_ms
queue_wait_ms
process_ms
total_ms
current_frame_index
```

### 为什么视频不放进 Redis Binary

图片任务中使用 Redis Binary Key：

```text
yolo:image:{task_id}:input
yolo:image:{task_id}:result
```

这是合理的，因为图片通常比较小，Redis TTL 可以控制内存。但是视频文件可能几十 MB、几百 MB，甚至更大。如果直接把视频 bytes 放入 Redis，会导致：

```text
Redis 内存快速膨胀
网络传输成本增加
大 key 影响 Redis 响应
任务结果视频也难以长期保存
```

所以 Phase 13 采用：

```text
HTTP Server 接收视频
↓
保存到 runtime/input/videos/detect
↓
Redis Stream 只保存 task_id、input_video_path、output_video_path、视频元信息
↓
Video Worker 从本地路径读取视频
↓
逐帧推理、画框、写入结果视频
↓
输出到 runtime/output/videos/detect
↓
Redis 保存状态、进度、结果 URL 和统计信息
```

这个设计牺牲了一部分跨机器弹性，但非常适合当前 Windows 单机工程验证。后续如果要多机器部署，可以再抽象 `VideoStorage`，把本地文件替换成共享磁盘、NAS、MinIO 或 S3。

### Phase 13.0 启动方式

启动 Server：

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server_video.yaml
```

启动 Worker：

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_video_worker.exe .\config\worker_video.yaml --consumer-name video_worker_1
```

健康检查：

```powershell
curl.exe "http://127.0.0.1:8082/api/v1/health"
curl.exe "http://127.0.0.1:8082/api/v1/ready"
curl.exe "http://127.0.0.1:8082/api/v1/workers"
curl.exe "http://127.0.0.1:8082/api/v1/metrics"
```

期望看到：

```text
phase = phase13_5_video_stability_cancel_recovery
video_enabled = true
video_storage = local_files
ready = true
alive_workers = 1
redis_pending = 0
```

提交视频任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8082/api/v1/detect/video/async" `
  -H "Content-Type: video/mp4" `
  --data-binary "@D:/tensorrtx/yolo11/videos/test.mp4"
```

查询任务：

```powershell
curl.exe "http://127.0.0.1:8082/api/v1/video/result/<task_id>"
```

下载结果视频：

```powershell
curl.exe -o result_phase13.mp4 "http://127.0.0.1:8082/api/v1/video/result/<task_id>/file"
```

### Phase 13.0 实测结果

单视频任务实测结果：

```text
task_id = video_detect_20260708_105450_576_1
status = done
queue_wait_ms = 2 ms
process_ms = 1147 ms
total_ms = 1180 ms
total_detections = 98
max_objects_per_frame = 2
result video = 961 KB
```

Smoke test 结果：

```text
submit 202
poll status=running progress=0.540541 frames=60/111
poll status=done progress=1.0 frames=87/111
downloaded result video: result_phase13_smoke.mp4
```

Redis 验收：

```text
XPENDING yolo:stream:video:detect yolo11_video_detect_group = 0
video_worker_1 pending = 0
XLEN >= 1
```

这说明 Server 入队、Video Worker 消费、逐帧推理、结果视频写出、结果查询和结果视频下载完整闭环。

---

### Phase 13.5：视频稳定性、取消、异常输入、Worker 恢复与批量压测

Phase 13.5 在 Phase 13.0 能跑通的基础上，补齐稳定性能力。

新增接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| POST | `/api/v1/video/result/{task_id}/cancel` | 请求取消 queued/running 视频任务 |
| POST | `/api/v1/video/result/{task_id}/cleanup` | 删除完成后的本地 input/output 视频文件 |

新增测试脚本：

```text
tools/phase13_5_video_invalid_input_test.py
tools/phase13_5_video_cancel_test.py
tools/phase13_5_video_benchmark.py
tools/phase13_5_video_worker_recovery_test.py
```

### 异常输入测试

命令：

```powershell
python tools\phase13_5_video_invalid_input_test.py `
  --url http://127.0.0.1:8082
```

实测结果：

```json
{
  "error": "failed to open uploaded video. Please upload a valid mp4/avi video file.",
  "error_code": "VIDEO_OPEN_FAILED",
  "success": false
}
```

结论：

```text
非法视频在 HTTP 层返回 400
不会进入 Redis Stream
不会影响 Worker
```

反思：坏输入必须在入口尽早拒绝，不能把垃圾任务交给 Worker 处理，更不能让 Worker 因为 OpenCV 解码失败而崩溃。

### 取消任务测试

命令：

```powershell
python tools\phase13_5_video_cancel_test.py `
  --url http://127.0.0.1:8082 `
  --video D:/tensorrtx/yolo11/videos/test.mp4 `
  --cancel-delay 0.1 `
  --allow-fast-done
```

实测结果：

```text
cancel 200
status = running
poll status = canceled
frames = 4/111
PASS: task canceled
```

结论：running 视频任务可以被取消，Worker 会在处理过程中检查 cancel 标记，并将状态写为 `canceled`。

反思：视频任务和图片任务最大区别之一是“中途可控”。图片推理太快，取消意义不大；视频是长任务，必须支持用户停止任务，避免 GPU 和磁盘继续浪费。

### 批量压测

命令：

```powershell
python tools\phase13_5_video_benchmark.py `
  --url http://127.0.0.1:8082 `
  --video D:/tensorrtx/yolo11/videos/test.mp4 `
  --tasks 20 `
  --concurrency 2 `
  --output-json phase13_5_video_benchmark_20.json
```

实测结果：

```json
{
  "tasks_requested": 20,
  "tasks_submitted": 20,
  "submit_errors": 0,
  "done": 20,
  "failed": 0,
  "canceled": 0,
  "timeout": 0,
  "wall_seconds": 17.3267,
  "qps_done": 1.1543,
  "latency_ms": {
    "avg_total": 8580.45,
    "p50_total": 8963.0,
    "p95_total": 15635.0,
    "avg_process": 823.95,
    "avg_queue": 7733.3
  }
}
```

结论：20 个视频任务全部完成，没有 failed 和 timeout。

这里 `avg_queue` 远大于 `avg_process` 是正常的，因为当前只有一个 `video_worker_1`，视频任务只能串行消费。视频服务的吞吐不是单帧推理 QPS，而是“视频任务级吞吐”。如果后续要提升视频任务并发，需要增加多个 Video Worker，或者做更复杂的 GPU 调度。

### Worker 恢复测试

命令：

```powershell
python tools\phase13_5_video_worker_recovery_test.py `
  --url http://127.0.0.1:8082 `
  --video D:/tensorrtx/yolo11/videos/test.mp4 `
  --tasks 3
```

测试过程：提交 3 个视频任务，中途强杀 `yolo11_video_worker.exe`，然后重启：

```powershell
Get-Process yolo11_video_worker | Stop-Process -Force

cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_video_worker.exe .\config\worker_video.yaml --consumer-name video_worker_1
```

实测结果：

```json
{
  "submitted": 3,
  "done": 3,
  "failed": 0,
  "canceled": 0,
  "timeout": 0
}
```

最终 metrics：

```text
total_tasks_done = 26
total_tasks_failed = 0
redis_pending = 0
worker_distribution.video_worker_1 = 26
```

Redis 验收：

```text
XPENDING = 0
video_worker_1 pending = 0
XLEN = 27
```

结论：视频任务 Worker 中断后可以恢复，最终没有 Pending 残留。

---

### config 清理与 runtime 目录统一

在 Phase 13.5 验收后，对项目目录做了一次工程整理。原因是随着阶段增多，`config/` 中历史 YAML 过多，`temp/`、`output/`、`logs/` 也开始混乱；尤其 video 输出一开始在 `temp/video/output`，没有和 Detect、OBB 统一。

整理后当前配置文件：

```text
config/
├── server.yaml
├── server_detect.yaml
├── worker_detect.yaml
├── server_obb.yaml
├── worker_obb.yaml
├── server_video.yaml
├── worker_video.yaml
└── archive/
    └── legacy_phase_configs/
```

运行时目录：

```text
runtime/
├── input/
│   ├── images/detect/
│   ├── images/obb/
│   └── videos/detect/
├── output/
│   ├── images/detect/
│   ├── images/obb/
│   └── videos/detect/
└── logs/
    ├── detect/server/
    ├── detect/worker/
    ├── obb/server/
    ├── obb/worker/
    ├── video/server/
    └── video/worker/
```

目录整理后 smoke test 已验证，视频任务实际路径变成：

```text
input_video_path = ./runtime/input/videos/detect\video_detect_xxx.mp4
output_video_path = ./runtime/output/videos/detect\video_detect_xxx_result.mp4
download = runtime/output/videos/detect/result_structure_cleanup_smoke.mp4
```

反思：工程越往后，目录规范越重要。早期 `temp/`、`output/` 随便放没问题；但当 Detect、OBB、Video 三类服务同时存在时，必须按服务和数据类型分层，否则后面日志、结果文件、测试产物会很难追踪。

---

### Phase 13 / 13.5 总结反思

1. 视频任务是长任务，不能照搬图片任务的“快速推理 + 立即返回”思路。
2. Redis 很适合保存任务状态、进度和元信息，但不适合直接保存大视频文件。
3. 视频任务必须支持 progress，否则客户端不知道任务是卡住了还是正在处理。
4. cancel 对视频任务非常重要，它是长任务服务的基本能力。
5. 异常视频应该在 HTTP 层拒绝，而不是进入队列污染 Worker。
6. 单 Video Worker 下，`queue_wait_ms` 远大于 `process_ms` 是正常现象，说明瓶颈是任务排队，不是单次推理。
7. Worker 恢复测试仍然是异步系统的底线，最终必须看 `XPENDING=0`。
8. `processed_frames < total_frames` 不一定是 bug，OpenCV 对 MP4 的 `CAP_PROP_FRAME_COUNT` 可能是容器估计值，实际可读帧数可能更少。
9. 目录结构整理不是“美化项目”，而是为了后续 RTSP、多 Worker、多模型和生产部署降低维护成本。
10. Phase 13 之后，项目已经从图片异步服务进入视频异步服务，下一步做 RTSP 才有更稳的基础。

### Phase 13.5 阶段结论

```text
Phase 13.5 已完成并通过验收。
```

当前系统已经支持 Detect 图片异步服务、OBB 图片异步服务和 Detect 视频文件异步服务。视频任务具备提交、查询、进度、取消、下载、清理、异常输入拒绝、Worker 恢复、metrics 和 Redis Pending 验收能力。目录结构也已经完成整理，当前配置和运行时产物都更加清晰。Phase 13.5 当时的下一阶段目标是进入 RTSP / 摄像头流服务化；该目标已在 Phase 14.0 / 14.5 中完成单路流最小闭环与稳定性增强。

---


## Phase 14 / 14.5：RTSP / 摄像头流任务最小闭环与稳定性增强

### 阶段总定位

Phase 14 是从“视频文件异步任务”进入“实时流任务管理”的阶段。Phase 13 处理的是有限长度的视频文件，任务会自然结束；Phase 14 处理的是持续输入的视频流，任务不会自动结束，因此必须新增流任务生命周期管理。

这两个阶段可以概括为：

```text
Phase 14.0：RTSP / 摄像头流任务最小闭环
Phase 14.5：流任务稳定性增强、重复启动保护与重连失败处理
```

Phase 14 的核心不是一次性做完整视频平台，而是先验证单路流的生命周期：

```text
start -> created -> starting -> running -> snapshot -> stopping -> stopped
```

Phase 14.5 则进一步补齐：

```text
重复 start 保护
打开失败 / 断流重连
无效 source 最终 failed
Worker 空闲恢复
Redis active key 清理
XPENDING 验收
```

---

### Phase 14.0：单路流任务最小闭环

Phase 14.0 新增独立流服务：

```text
Stream Server：yolo11_server.exe + config/server_stream.yaml，端口 8083
Stream Worker：yolo11_stream_worker.exe + config/worker_stream.yaml
Redis Stream：yolo:stream:live:detect
Consumer Group：yolo11_stream_detect_group
Consumer Name：stream_worker_1
Snapshot 目录：runtime/output/streams/{stream_id}/snapshot.jpg
```

新增接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| POST | `/api/v1/stream/start` | 启动一个 camera / file / rtsp 流任务 |
| GET | `/api/v1/stream/{stream_id}/status` | 查询流任务状态、帧数、FPS、错误、重连次数 |
| GET | `/api/v1/stream/{stream_id}/snapshot` | 下载最新画框后的 JPEG 帧 |
| POST | `/api/v1/stream/{stream_id}/stop` | 请求停止流任务并释放 VideoCapture |

Phase 14.0 的数据流：

```text
Client
  -> POST /api/v1/stream/start
  -> Server 创建 stream_id
  -> 写入 Redis stream start command
  -> 写 yolo:streamtask:{stream_id}:status/meta/latest
  -> Stream Worker XREADGROUP 领取任务
  -> 打开 VideoCapture(camera/file/rtsp)
  -> 逐帧读取
  -> Detect Runner 推理
  -> draw 后覆盖写 snapshot.jpg
  -> 更新 frame_count / fps / last_num_detections / latest_snapshot_path
  -> Client 查询 status 或下载 snapshot
  -> POST stop
  -> Worker 检测 stop_requested
  -> 释放 VideoCapture
  -> status=stopped
```

### Phase 14.0 启动方式

启动 Stream Server：

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_server.exe .\config\server_stream.yaml
```

启动 Stream Worker：

```powershell
cd D:\tensorrtx\yolo11
.\out\build\x64-Debug\yolo11_stream_worker.exe .\config\worker_stream.yaml --consumer-name stream_worker_1
```

健康检查：

```powershell
curl.exe "http://127.0.0.1:8083/api/v1/health"
curl.exe "http://127.0.0.1:8083/api/v1/ready"
curl.exe "http://127.0.0.1:8083/api/v1/workers"
```

期望看到：

```text
phase = phase14_5_stream_stability_reconnect
stream_enabled = true
ready = true
alive_workers = 1
stream_worker_1 alive = true
redis_pending = 0
```

### PowerShell 中提交 JSON 的推荐方式

直接写：

```powershell
$body = '{"source_type":"camera","camera_id":0}'
curl.exe -X POST ... -d $body
```

可能会出现 JSON 引号被 PowerShell 处理掉，导致后端返回：

```json
{
  "error_code": "INVALID_JSON"
}
```

最稳的方式是先写 JSON 文件，再用 `--data-binary` 上传：

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

返回示例：

```json
{
  "success": true,
  "stream_id": "stream_detect_20260708_135555_612_1",
  "status": "created",
  "source_type": "camera",
  "camera_id": 0,
  "status_url": "/api/v1/stream/stream_detect_20260708_135555_612_1/status",
  "snapshot_url": "/api/v1/stream/stream_detect_20260708_135555_612_1/snapshot",
  "stop_url": "/api/v1/stream/stream_detect_20260708_135555_612_1/stop"
}
```

查询状态：

```powershell
curl.exe "http://127.0.0.1:8083/api/v1/stream/<stream_id>/status"
```

下载 snapshot：

```powershell
curl.exe -o stream_snapshot.jpg "http://127.0.0.1:8083/api/v1/stream/<stream_id>/snapshot"
```

停止流任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8083/api/v1/stream/<stream_id>/stop"
```

### Phase 14.0 实测结果

本地摄像头 `camera_id=0` 测试通过：

```text
status = running
frame_count = 1125 -> 1470
width = 640
height = 480
fps = 30.0
last_num_detections = 1
snapshot 下载成功，约 39 KB
stop 后 status = stopped
stream_worker_1 pending = 0
XPENDING = 0
```

这说明 Phase 14.0 的最小闭环成立：

```text
start 成功
Worker 能消费任务
摄像头能打开
帧数持续增长
YOLO 推理结果写入状态
snapshot 能下载
stop 能释放资源
Redis 无 Pending 残留
```

---

### Phase 14.5：流任务稳定性增强

Phase 14.0 跑通后，测试中暴露出一个重要问题：单个 `stream_worker_1` 本质上只能稳定处理一个长时间流任务，但 Server 允许连续多次 `/stream/start`，导致多个任务都处于 `created`，后续容易产生排队混乱。

Phase 14.5 重点解决这些稳定性问题：

- 单活流保护：已有 active stream 时，重复 start 返回 HTTP 409。
- 流任务 active key：`yolo:streamtask:yolo_stream_live_detect:active`。
- 重连配置：`enable_reconnect`、`reconnect_max_attempts`、`reconnect_delay_ms`。
- 打开失败或断流后进入 `reconnecting`。
- 重连次数超过上限后进入 `failed`。
- 状态中增加 `reconnect_count` 和 `no_frame_count`。
- 无效摄像头或错误 RTSP URL 不再无声卡住，而是明确失败。
- Worker 最终必须回到 `idle`，`current_task_id` 清空。
- Redis 最终 `XPENDING=0`，active key 为 `nil`。

Phase 14.5 配置字段：

```yaml
stream:
  enabled: true
  snapshot_dir: "D:/tensorrtx/yolo11/runtime/output/streams"
  target_fps: 10
  snapshot_interval_frames: 5
  max_no_frame_count: 30
  enable_reconnect: true
  reconnect_max_attempts: 3
  reconnect_delay_ms: 1000
  max_runtime_seconds: 0
```

关键状态机：

```text
created -> starting -> running -> stopping -> stopped
created -> starting -> reconnecting -> failed
running -> reconnecting -> running
running -> reconnecting -> failed
running -> stopping -> stopped
```

注意：Phase 14.5 只做单路流稳定性，不做多路 RTSP 并发，也不做 WebSocket 推送。

### 重复 start 保护

当已有 active stream 时，再次提交 `/api/v1/stream/start` 会返回：

```json
{
  "success": false,
  "error_code": "STREAM_ALREADY_ACTIVE",
  "error": "another stream task is already active; stop it before starting a new stream",
  "active_stream_id": "stream_detect_20260708_143702_825_3"
}
```

这一步很重要。实时流任务和普通图片 / 视频文件任务不同：它不是短任务，也不会自然快速结束。单 Worker 单路流阶段必须先禁止重复 start，避免任务堆积和摄像头资源冲突。

### 无效摄像头重连失败处理

测试 `camera_id=99`：

```powershell
@'
{
  "source_type": "camera",
  "camera_id": 99
}
'@ | Set-Content -Encoding utf8 .\stream_start_bad_camera.json

curl.exe -X POST "http://127.0.0.1:8083/api/v1/stream/start" `
  -H "Content-Type: application/json" `
  --data-binary "@stream_start_bad_camera.json"
```

实际状态变化：

```text
created
-> starting
-> reconnecting, reconnect_count=1
-> reconnecting, reconnect_count=2
-> reconnecting, reconnect_count=3
-> failed
```

最终错误：

```text
last_error = failed to open stream source: type=camera, uri=99, reconnect_count=3
```

这个测试证明：错误 source 不会让 Worker 崩溃，也不会永久卡在 created/running，而是进入明确的 failed 状态。

### Phase 14.5 自动测试脚本

新增：

```text
tools/phase14_5_stream_stability_test.py
```

测试命令：

```powershell
python tools\phase14_5_stream_stability_test.py `
  --url http://127.0.0.1:8083 `
  --source-type camera `
  --camera-id 0 `
  --repeat 3 `
  --run-seconds 5
```

脚本覆盖：

```text
1. health / ready 检查
2. 连续 3 轮 start -> running -> snapshot -> stop -> stopped
3. 每一轮重复 start，验证 409 STREAM_ALREADY_ACTIVE
4. snapshot 是否返回 image/jpeg
5. 无效 camera_id=99 是否 reconnecting 后 failed
6. Worker 最终是否 idle
```

实测结果：

```text
PASS Phase 14.5 stability test
```

三轮结果：

```text
stream_detect_20260708_143702_825_3 frame_count=55 snapshot OK
stream_detect_20260708_143711_961_5 frame_count=50 snapshot OK
stream_detect_20260708_143720_141_7 frame_count=50 snapshot OK
```

Worker 最终状态：

```text
alive = true
status = idle
current_task_id = ""
processed_count = 3
failed_count = 1
last_error = failed to open stream source: type=camera, uri=99, reconnect_count=3
```

Redis 验收：

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group
redis-cli -h 172.19.196.109 -p 6379 XINFO CONSUMERS yolo:stream:live:detect yolo11_stream_detect_group
redis-cli -h 172.19.196.109 -p 6379 GET yolo:streamtask:yolo_stream_live_detect:active
```

期望结果：

```text
XPENDING = 0
stream_worker_1 pending = 0
active key = nil
```

### Phase 14 / 14.5 遇到的问题与解决方案

#### 1. PowerShell JSON 被吃掉，导致 `INVALID_JSON`

现象：

```json
{
  "error": "stream/start expects JSON body or an empty body",
  "error_code": "INVALID_JSON"
}
```

原因：PowerShell 中直接通过变量加 `curl.exe -d $body` 时，JSON 引号可能被处理掉。

解决：使用 here-string 写入 `.json` 文件，再用 `--data-binary "@file.json"` 提交。

反思：Windows 下测试 HTTP JSON 时，命令行本身也会成为 bug 来源。复杂 JSON 建议用文件提交。

#### 2. 新任务一直停在 `created`

现象：

```text
status = created
consumer_name = ""
start_time_ms = 0
frame_count = 0
```

原因：Worker 还在处理旧的长时间流任务，没有领取新任务；或者 active key 与 Worker 心跳状态不一致。

解决：

```powershell
curl.exe -X POST "http://127.0.0.1:8083/api/v1/stream/<old_stream_id>/stop"
curl.exe "http://127.0.0.1:8083/api/v1/workers"
```

确保：

```text
status = idle
current_task_id = ""
```

必要时重启 `yolo11_stream_worker.exe`。

反思：实时流任务不是短任务，不能像图片任务一样连续提交多个任务等待排队。单 Worker 单路流阶段必须先做 active stream 保护。

#### 3. 重复 start 导致多个 created 任务堆积

现象：多次 `/stream/start` 都返回 success=true，多个 stream_id 处于 created。

解决：Phase 14.5 增加 active key 和重复启动保护，已有 active stream 时返回 HTTP 409。

反思：长任务服务必须考虑资源占用和生命周期互斥，不是所有任务都适合无限入队。

#### 4. 无效 camera_id 不能直接算 start 失败

Server 不一定能提前知道 `camera_id=99` 是否存在，因为真正打开 source 的是 Worker。因此 Server 返回 created 是合理的，随后 Worker 打开失败，再写入 reconnecting / failed。

反思：入口层和执行层的职责要分清。Server 负责校验 JSON 和入队，Worker 负责打开真实 source 并决定 running 或 failed。

#### 5. Worker failed_count 增加不是系统坏了

无效 `camera_id=99` 测试后：

```text
failed_count = 1
last_error = failed to open stream source: type=camera, uri=99, reconnect_count=3
```

这是预期失败，不是系统异常。判断系统是否健康应看：

```text
ready = true
worker status = idle
current_task_id = ""
XPENDING = 0
active key = nil
```

反思：稳定性测试中的失败任务要区分“预期失败”和“系统错误”。

### Phase 14 / 14.5 阶段结论

```text
Phase 14.5 已完成并通过验收。
```

当前系统已经支持单路实时流任务服务化，具备 start、status、snapshot、stop 四个基本接口；支持 `camera_id=0` 本地摄像头流稳定运行；支持最新帧 snapshot 下载；支持重复启动保护；支持打开失败后的 reconnecting 与 failed 状态；支持 Worker 心跳、active key 和 Redis Pending 验收。三轮 Phase 14.5 稳定性测试全部通过，最终 `XPENDING=0`、`stream_worker_1 pending=0`、active key 为 `nil`、Worker 回到 `idle`。


### Phase 14.5 最终补充：queued 语义、stale 清理与 stream metrics

在最开始的 Phase 14.0 复盘中，流任务初始状态曾经使用 `created`。后续为了和图片、OBB、视频异步任务的入队语义统一，最终版本将 `/api/v1/stream/start` 返回改为：

```text
status = queued
lifecycle_status = queued
```

因此当前最终状态机应理解为：

```text
queued -> starting -> running -> stopping -> stopped
queued -> starting -> reconnecting -> failed
running -> reconnecting -> running
running -> reconnecting -> failed
```

这次最终修正还补齐了三个容易被忽略、但对长任务服务非常重要的点。

#### 1. active lock TTL 续期

流任务是长生命周期任务，如果 active key 只在 start 时设置 TTL，而运行过程中不续期，长时间运行后可能出现 active key 过期、Server 误以为没有活跃流、从而允许重复 start 的风险。因此最终版本在以下状态更新点续期：

```text
markStreamStarting
markStreamRunning
markStreamReconnecting
updateStreamLatest
requestStopStreamTask
Worker heartbeat / running loop
```

对应的核心 Redis key 包括：

```text
yolo:streamtask:{stream_id}:status
yolo:streamtask:{stream_id}:meta
yolo:streamtask:{stream_id}:latest
yolo:streamtask:yolo_stream_live_detect:active
```

这说明实时流服务不能只看“开始时写入状态”，还要保证运行过程中的状态、锁和最新帧信息持续保活。

#### 2. stale stream cleanup

调试过程中曾出现旧 stream 已经不再被 Worker 持有，但 active key 仍然残留，导致新任务被 `STREAM_ALREADY_ACTIVE` 拦截。最终通过 stale cleanup 修复：Server 在处理新的 `/stream/start` 前，会检查旧 active stream 是否仍然可信。

判断依据包括：

```text
旧 stream status 是否是 running / starting / reconnecting / stopping
Worker heartbeat 是否存在
Worker current_task_id 是否仍然等于旧 stream_id
latest last_update_ms 是否长时间没有刷新
```

如果发现旧任务已经 stale，就将其标记为 failed/stale，并释放 active key。这个机制解决的是“Worker 异常退出或状态残留导致系统不能继续 start”的问题。

#### 3. stream terminal metrics

普通图片 / OBB / 视频短任务通常在 `markDone()` / `markFailed()` 后再 `XACK`，metrics 可以在短任务完成路径中更新。但 stream 不一样：stream start command 在进入 `running` 后就已经 `XACK`，任务真正结束发生在后续的 `stop` 或 `failed` 终态。

因此最终必须在：

```cpp
RedisTaskQueue::markStreamStopped(...)
RedisTaskQueue::markStreamFailed(...)
```

里面显式写入 metrics：

```text
stopped -> HINCRBY yolo:metrics:yolo_stream_live_detect:global done_count 1
failed  -> HINCRBY yolo:metrics:yolo_stream_live_detect:global failed_count 1
```

同时更新：

```text
yolo:metrics:yolo_stream_live_detect:worker:done
yolo:metrics:yolo_stream_live_detect:worker:failed
yolo:metrics:yolo_stream_live_detect:recent:done
last_task_id
last_finish_time_ms
```

最终验证结果：

```text
PASS Phase 14.5 stream metrics check
valid camera stream   -> stopped -> total_tasks_done = 1
invalid camera_id=99  -> failed  -> total_tasks_failed = 1
metrics_found = true
worker_distribution.stream_worker_1 = 1
worker_failed_distribution.stream_worker_1 = 1
```

这一步的反思是：长任务的“消息确认”和“任务完成”不是同一个时刻。实时流任务进入 running 后可以确认 start command，但业务上的 done/failed 只能在终态函数里统计。

#### 4. source matrix 验收口径

最终输入源验收应按三类区分：

| source_type | 当前验收结论 | 说明 |
|---|---|---|
| file | PASS | 本地视频文件可作为伪实时流读取，EOF 后正常 stopped |
| camera | PASS | 本机 `camera_id=0` 可 start / snapshot / stop |
| rtsp | 环境受限 | 当前没有真实网络摄像头，因此无法验证 RTSP 成功路径；占位 URL 的失败路径已验证为 reconnecting -> failed |

这不是 RTSP 功能失败，而是测试环境缺少真实 RTSP source。后续拿到真实网络摄像头或有效 RTSP URL 后，只需要补一次 source matrix 的 RTSP 成功路径测试。

#### 5. 本阶段最终封板结论

```text
Phase 14.0 Stream Lifecycle：PASS
Phase 14.5 Stream Stability / Reconnect / Metrics：PASS
```

最终验收项：

```text
file source：PASS
camera source：PASS
RTSP：无真实网络摄像头，异常路径 PASS
duplicate start 409：PASS
invalid camera reconnect -> failed：PASS
stream status lifecycle：PASS
snapshot：PASS
heartbeat：PASS
Redis pending：PASS
active lock release：PASS
stream metrics done/failed：PASS
```

### Phase 14.5 之后的实际推进

Phase 14.5 后已经进入并完成 Phase 15。Phase 15 没有直接做多 GPU 自动调度，而是先完成 Worker 能力描述、资源声明和统一观测，为后续调度打地基。

---

## Phase 15：Worker / 模型 / 任务类型能力注册与统一观测

### 阶段总定位

Phase 15 的核心不是继续新增模型或接口，而是解决“服务类型越来越多以后，Server 如何知道每个 Worker 能处理什么”的问题。Phase 14.5 结束时，项目已经有四类服务：Detect 图片、OBB 图片、Video 文件、Stream 实时流。它们的 Worker 名称、队列、任务类型、执行方式都不同，如果继续只用简单的 `model_type` 或 `consumer_name` 判断，很容易在后续多 GPU、多 Worker、多路流扩展时失控。

所以 Phase 15 的目标是：让每个 Worker 在 heartbeat 中清楚声明自己的能力，并让 `/ready`、`/workers`、`/metrics` 能按这些能力字段过滤和展示。

可以概括为：

```text
Phase 15：Worker Capability Registry
├── 不做自动多 GPU 调度
├── 不做多路 RTSP 并发
├── 不做新模型类型
├── 先统一 Worker 能力声明
├── 再统一 ready / workers / metrics 过滤
└── 为 Phase 16 / Phase 17 的生产化和部署增强做准备
```

### 为什么需要 Worker Capability Registry

前面阶段的 Worker 在线判断主要依赖 heartbeat：

```text
yolo:worker:{consumer_name}:heartbeat
```

这能判断 Worker 是否活着，但不能充分描述 Worker 的能力。例如：

```text
worker_1          -> Detect 图片短任务
obb_worker_1      -> OBB 图片短任务
video_worker_1    -> Video 文件长任务，但底层用 Detect Runner
stream_worker_1   -> Stream 实时流长任务，也用 Detect Runner
```

如果只看 `model_type=detect`，Video 和 Stream 会被误认为普通 Detect 图片 Worker；如果只看 `consumer_name`，后续扩容或多 GPU 部署时又会变得很脆弱。因此 Phase 15 将 Worker heartbeat 从“存活信息”升级为“能力注册信息”。

### Phase 15 核心字段

| 字段 | 含义 |
|---|---|
| `model_type` | 对外服务类型，用于 `/workers`、`/ready`、`/metrics` 过滤，例如 `detect`、`obb`、`video`、`stream` |
| `runner_model_type` | 内部实际推理模型，例如 Video / Stream 当前都使用 Detect Runner，所以是 `detect` |
| `worker_group` | Worker 资源分组，例如 `image_detect_gpu0`、`video_detect_gpu0` |
| `worker_kind` | Worker 类别，例如 `image`、`video`、`stream` |
| `task_kind` | 任务类型，例如 `image_async`、`video_file`、`live_stream` |
| `stream_type` | 任务生命周期类型，短任务为 `redis_stream`，实时流为 `long_running_stream` |
| `gpu_id` | Worker 绑定的 GPU 编号 |
| `max_concurrency` | 当前 Worker 声明的并发能力，现阶段主要是元信息，不做自动调度 |
| `engine_path` | 当前 Worker 使用的 TensorRT engine |
| `labels_path` | 当前 Worker 使用的 labels 文件 |

这一阶段最重要的认知是：`model_type` 和 `runner_model_type` 不是一回事。Video / Stream 是服务类型，但底层推理还是 Detect 模型，所以应该写成：

```text
model_type = video / stream
runner_model_type = detect
```

### 四类 Worker 能力矩阵

| 服务 | 端口 | Worker | model_type | runner_model_type | worker_group | worker_kind | task_kind | stream_type |
|---|---:|---|---|---|---|---|---|---|
| Detect 图片 | 8080 | `worker_1` | `detect` | `detect` | `image_detect_gpu0` | `image` | `image_async` | `redis_stream` |
| OBB 图片 | 8081 | `obb_worker_1` | `obb` | `obb` | `image_obb_gpu0` | `image` | `image_async` | `redis_stream` |
| Video 文件 | 8082 | `video_worker_1` | `video` | `detect` | `video_detect_gpu0` | `video` | `video_file` | `redis_stream` |
| Stream 实时流 | 8083 | `stream_worker_1` | `stream` | `detect` | `stream_detect_gpu0` | `stream` | `live_stream` | `long_running_stream` |

### 主要代码改动

| 文件 | 修改内容 |
|---|---|
| `include/server/app_config.h` | Worker 配置新增 `worker_group`、`worker_kind`、`task_kind`、`stream_type`、`max_concurrency` 等字段 |
| `src/server/app_config.cpp` | 从 YAML 读取 Phase 15 新字段，并提供默认值 |
| `include/server/redis_task_queue.h` | Worker heartbeat 结构扩展 capability 字段 |
| `src/server/redis_task_queue.cpp` | `updateWorkerHeartbeat()` 写入 `model_type`、`runner_model_type`、`worker_group` 等字段；metrics 增加 profile 能力维度 |
| `src/server/inference_worker.cpp` | Detect / OBB 图片 Worker 写入 image capability |
| `src/server/video_inference_worker.cpp` | Video Worker 写入 `model_type=video`、`runner_model_type=detect` |
| `src/server/stream_inference_worker.cpp` | Stream Worker 写入 `model_type=stream`、`task_kind=live_stream`、`stream_type=long_running_stream` |
| `src/server/http_controller.cpp` | `/ready`、`/workers`、`/metrics` 支持 capability filters；Stream start/status 对外 `task_kind` 统一为 `live_stream` |
| `config/*detect/obb/video/stream*.yaml` | 为四类服务补充 worker capability 配置 |
| `tools/phase15_worker_registry_test.py` | 新增 Phase 15 Worker Registry 自动检查脚本 |

### 关键接口过滤方式

Detect：

```powershell
curl.exe "http://127.0.0.1:8080/api/v1/workers?model=detect&task_kind=image_async"
curl.exe "http://127.0.0.1:8080/api/v1/ready?worker_group=image_detect_gpu0"
curl.exe "http://127.0.0.1:8080/api/v1/metrics?model=detect"
```

OBB：

```powershell
curl.exe "http://127.0.0.1:8081/api/v1/workers?model=obb&task_kind=image_async"
curl.exe "http://127.0.0.1:8081/api/v1/ready?worker_group=image_obb_gpu0"
curl.exe "http://127.0.0.1:8081/api/v1/metrics?model=obb"
```

Video：

```powershell
curl.exe "http://127.0.0.1:8082/api/v1/workers?model=video&task_kind=video_file"
curl.exe "http://127.0.0.1:8082/api/v1/ready?worker_group=video_detect_gpu0"
curl.exe "http://127.0.0.1:8082/api/v1/metrics?model=video"
```

Stream：

```powershell
curl.exe "http://127.0.0.1:8083/api/v1/workers?model=stream&task_kind=live_stream"
curl.exe "http://127.0.0.1:8083/api/v1/ready?worker_group=stream_detect_gpu0"
curl.exe "http://127.0.0.1:8083/api/v1/metrics?model=stream"
```

预期结果：

```text
matched_workers = 1
alive_workers = 1
ready = true
redis_pending = 0
phase = phase15_worker_capability_registry
```

### Phase 15 实测结果

#### Detect 8080

验证结果：

```text
/health phase = phase15_worker_capability_registry
/workers?model=detect&task_kind=image_async matched_workers=1
/ready?worker_group=image_detect_gpu0 ready=true
异步 detect 图片任务 status=done
num_detections=3
processed_count=1
failed_count=0
XPENDING yolo:stream:detect yolo11_group = 0
```

关键 heartbeat：

```text
model_type = detect
runner_model_type = detect
worker_group = image_detect_gpu0
worker_kind = image
task_kind = image_async
stream_type = redis_stream
```

#### OBB 8081

验证结果：

```text
/workers?model=obb&task_kind=image_async matched_workers=1
/ready?worker_group=image_obb_gpu0 ready=true
OBB 图片任务 status=done
result image 可下载
processed_count=1
failed_count=0
XPENDING yolo:stream:obb yolo11_obb_group = 0
```

关键 heartbeat：

```text
model_type = obb
runner_model_type = obb
worker_group = image_obb_gpu0
worker_kind = image
task_kind = image_async
stream_type = redis_stream
```

#### Video 8082

验证结果：

```text
/workers?model=video&task_kind=video_file matched_workers=1
/ready?worker_group=video_detect_gpu0 ready=true
视频任务 status=done
processed_frames=87
progress=1.0
result video 可下载
processed_count=1
failed_count=0
XPENDING yolo:stream:video:detect yolo11_video_detect_group = 0
```

关键 heartbeat：

```text
model_type = video
runner_model_type = detect
worker_group = video_detect_gpu0
worker_kind = video
task_kind = video_file
stream_type = redis_stream
```

#### Stream 8083

验证结果：

```text
/workers?model=stream&task_kind=live_stream matched_workers=1
/ready?worker_group=stream_detect_gpu0 ready=true
camera_id=0 stream/start 返回 queued
snapshot 可下载
stop 后 status=stopped
frame_count=300 左右
last_num_detections=1
metrics total_done=1
total_failed=0
processed_count=1
XPENDING yolo:stream:live:detect yolo11_stream_detect_group = 0
```

关键 heartbeat：

```text
model_type = stream
runner_model_type = detect
worker_group = stream_detect_gpu0
worker_kind = stream
task_kind = live_stream
stream_type = long_running_stream
```

### Phase 15 遇到的问题与解决方案

#### 1. 先 curl `/workers` 但 Server 没启动

现象：

```text
curl: (7) Failed to connect to 127.0.0.1 port 8080
```

原因：`/workers`、`/ready` 都是 HTTP 接口，必须先启动对应 Server；`/ready` 要 true，还必须启动对应 Worker。

解决：先启动：

```powershell
# Detect Server
.\outuildd-Debug\yolo11_server.exe .\config\server_detect.yaml

# Detect Worker
.\outuildd-Debug\yolo11_worker.exe .\config\worker_detect.yaml --consumer-name worker_1
```

反思：Server 负责 HTTP 接入，Worker 负责写 heartbeat 和执行推理；两者缺一不可。

#### 2. Video Worker 一开始 `model=video` 过滤不到

现象：

```text
/workers?model=video&task_kind=video_file
matched_workers = 0

/ready?worker_group=video_detect_gpu0
ready = true
matched_workers = 1
```

原因：Video Worker heartbeat 里最初写成了：

```text
model_type = detect
runner_model_type = detect
```

这样按 `worker_group` 能找到，但按 `model=video` 找不到。

解决：修改 `src/server/video_inference_worker.cpp`，让 Video Worker 对外声明：

```text
model_type = video
runner_model_type = detect
```

反思：Video 是服务类型，Detect 是内部推理模型。调度语义和模型执行语义必须分开。

#### 3. Stream API 里 `task_kind=stream` 和 Registry 的 `live_stream` 不一致

现象：Worker registry / metrics 中是：

```text
task_kind = live_stream
```

但 `/stream/start` 和 `/stream/status` 返回的是：

```text
task_kind = stream
```

解决：修改 `src/server/http_controller.cpp`，将 Stream 对外 API 返回统一为：

```text
task_kind = live_stream
```

保留 Redis 内部 start command 的历史语义，不强行破坏 Worker 消费逻辑。

反思：内部任务命令和外部调度语义可以不同，但对外 API、Registry 和 Metrics 要保持一致。

### Phase 15 阶段结论

```text
Phase 15 已完成并通过验收。
```

当前项目已经具备四类服务的统一能力注册与观测能力。Detect、OBB、Video、Stream 都可以通过 `/workers`、`/ready`、`/metrics` 按模型、任务类型和 Worker 分组进行过滤。四类服务均通过真实任务验证，最终 Redis `XPENDING=0`。这说明项目已经从“多个服务都能跑”推进到“多个服务可以被统一识别、统一观测、统一验收”的阶段。

Phase 15 的意义不是自动调度本身，而是为自动调度做准备。只有 Worker 能清楚声明“我是谁、我能处理什么、我绑定什么资源、当前是否空闲”，后续多 GPU、多路 RTSP、服务守护和生产部署才有稳定基础。

后续阶段实际推进说明：

```text
Phase 16：工程封板与上线前整理 —— 已完成
```

Phase 16 已完成服务启动 / 停止脚本标准化、统一检查脚本、全量回归入口、配置模板和运行报告归档。当前项目已经具备一键启动、统一检查、全量回归和一键停止的封板基线。

下一阶段建议进入：

```text
Phase 17：CLS 图片异步服务接入
```

继续后置：

```text
多 GPU 自动调度
多路 RTSP 并发
Docker / Linux 迁移
Prometheus / Grafana
数据库持久化
前端页面
```

---



## Phase 17 / 17.5：CLS 图片异步服务、POSE 图片异步服务与 ModelOutput 抽象

### 阶段总定位

Phase 17 和 Phase 17.5 是在 Phase 16 工程封板基线之后，继续补齐 YOLO11 五大基础任务接口的阶段。Phase 16 已经把 Detect / OBB / Video / Stream 四类服务整理成可启动、可检查、可回归、可停止的工程基线；Phase 17 开始把原始 YOLO11 分类与姿态估计 demo 接入同一套 HTTP + Redis Stream + Worker 服务框架。

这两个阶段可以概括为：

```text
Phase 17.0：CLS 图片异步服务接入 + ModelOutput 最小抽象
Phase 17.5：POSE 图片异步服务接入 + 全量回归验证
```

本阶段的关键不是简单新增两个接口，而是把模型输出从单一检测框结构升级为可扩展结构。分类任务输出 top1 / topk，姿态任务输出 bbox + 17 个 COCO keypoints + skeleton，二者都不适合继续强行塞进原来的 `std::vector<Detection>` 语义里。因此 Phase 17 先引入 `ModelOutput`，再接 CLS；Phase 17.5 在同一抽象基础上接入 POSE。

---

### Phase 17.0：CLS 图片异步服务

Phase 17.0 新增独立分类图片异步服务：

```text
CLS Server：yolo11_server.exe + config/server_cls.yaml，端口 8084
CLS Worker：yolo11_worker.exe + config/worker_cls.yaml
Redis Stream：yolo:stream:cls
Consumer Group：yolo11_cls_group
Consumer Name：cls_worker_1
Worker Group：image_cls_gpu0
Task Kind：image_async
Stream Type：redis_stream
```

新增接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| POST | `/api/v1/classify/image/async` | 上传图片并提交异步分类任务 |
| GET | `/api/v1/result/{task_id}` | 查询分类任务状态和 top1/topk 结果 |
| GET | `/api/v1/result/{task_id}/image` | 下载带分类文字标注的结果图 |
| GET | `/api/v1/ready?model=cls&task_kind=image_async&worker_group=image_cls_gpu0` | 检查 CLS Worker 是否就绪 |
| GET | `/api/v1/workers?model=cls` | 查看 CLS Worker heartbeat |
| GET | `/api/v1/metrics?model=cls` | 查看 CLS 任务统计 |

### CLS 主要代码改动

| 文件 | 内容 |
|---|---|
| `include/server/model_output.h` | 新增统一模型输出结构，支持 detections 与 classifications |
| `include/yolo11_cls_api.h` | 新增 CLS Runtime API 头文件 |
| `api/yolo11_cls_api.cpp` | 封装原始 `yolo11_cls.cpp` 推理逻辑 |
| `include/server/model_runner.h` | 新增 `ClsModelRunner` 声明 |
| `src/server/model_runner.cpp` | `createModelRunner("cls")` 返回 CLS Runner |
| `include/server/result_serializer.h` | 新增 CLS 序列化接口 |
| `src/server/result_serializer.cpp` | 输出 `classification_format`、`top1`、`topk` |
| `src/server/http_controller.cpp` | 注册 `/api/v1/classify/image/async` |
| `config/server_cls.yaml` | CLS Server 配置，端口 8084 |
| `config/worker_cls.yaml` | CLS Worker 配置 |
| `scripts/start_cls.ps1` / `stop_cls.ps1` | CLS 独立启动 / 停止脚本 |
| `tools/phase17_cls_registry_test.py` | CLS Worker Registry 测试 |
| `tools/phase17_cls_smoke_test.py` | CLS 端到端 smoke 测试 |
| `tools/run_phase17_regression.py` | CLS + Phase16 回归入口 |

### ModelOutput 最小抽象

Phase 17 之前，`IModelRunner` 主要返回 `std::vector<Detection>`。这对 Detect / OBB 勉强可用，但不适合分类。分类没有 bbox，它的核心输出是类别概率分布。于是新增：

```cpp
struct ClassificationItem {
    int class_id = -1;
    std::string class_name;
    float confidence = 0.0f;
};

struct ModelOutput {
    std::string model_type;
    std::vector<Detection> detections;
    std::vector<ClassificationItem> classifications;
};
```

这样 Worker 主循环只关心：

```text
读取图片
-> runner->infer(image)
-> runner->draw(image, output)
-> ResultSerializer 根据 model_type 序列化
-> 写回 Redis
-> XACK
```

而不同模型的输出差异被收敛到 Runner 和 Serializer 中。

### CLS 返回结果示例

```json
{
  "success": true,
  "status": "done",
  "model_type": "cls",
  "classification_format": "top1_and_topk",
  "num_classifications": 5,
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

说明：当前 `labels/imagenet.txt` 采用 `class_0` 到 `class_999` 占位标签，目的是先验证服务化链路。真实部署时应替换为 ImageNet 1000 类真实类别名。

### CLS 验收命令

启动：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_cls.ps1 -ExeDir .\out\build\x64-Debug
```

检查：

```powershell
curl.exe "http://127.0.0.1:8084/api/v1/health"
curl.exe "http://127.0.0.1:8084/api/v1/ready?model=cls&task_kind=image_async&worker_group=image_cls_gpu0"
curl.exe "http://127.0.0.1:8084/api/v1/workers?model=cls"
curl.exe "http://127.0.0.1:8084/api/v1/metrics?model=cls"
```

提交任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8084/api/v1/classify/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

自动测试：

```powershell
python tools\phase17_cls_registry_test.py --url http://127.0.0.1:8084
python tools\phase17_cls_smoke_test.py --url http://127.0.0.1:8084 --image .\images\bus.png
python tools\run_phase17_regression.py --skip-phase16
```

Redis 验收：

```powershell
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:cls yolo11_cls_group
```

阶段结果：

```text
CLS registry test：PASS
CLS smoke test：PASS
Phase 17 Regression Summary：success=True
metrics_found=true
total_tasks=3
total_tasks_done=3
total_tasks_failed=0
worker_distribution.cls_worker_1=3
XPENDING=0
```

---

### Phase 17.5：POSE 图片异步服务

Phase 17.5 在 CLS 已接入的基础上继续接入 YOLO11 Pose。POSE 与 CLS 的不同在于，它仍然有 bbox，但每个检测对象还携带 17 个 COCO keypoints 和骨架连接关系。因此它继续复用 `ModelOutput.detections`，但在 ResultSerializer 中增加 keypoint / skeleton 输出。

POSE 服务规划：

```text
POSE Server：yolo11_server.exe + config/server_pose.yaml，端口 8085
POSE Worker：yolo11_worker.exe + config/worker_pose.yaml
Redis Stream：yolo:stream:pose
Consumer Group：yolo11_pose_group
Consumer Name：pose_worker_1
Worker Group：image_pose_gpu0
Task Kind：image_async
Stream Type：redis_stream
```

新增接口：

| 方法 | 接口 | 说明 |
|---|---|---|
| POST | `/api/v1/pose/image/async` | 上传图片并提交异步姿态估计任务 |
| GET | `/api/v1/result/{task_id}` | 查询姿态估计结果 |
| GET | `/api/v1/result/{task_id}/image` | 下载绘制 bbox/keypoints/skeleton 的结果图 |
| GET | `/api/v1/ready?model=pose&task_kind=image_async&worker_group=image_pose_gpu0` | 检查 POSE Worker 是否就绪 |
| GET | `/api/v1/workers?model=pose` | 查看 POSE Worker heartbeat |
| GET | `/api/v1/metrics?model=pose` | 查看 POSE 任务统计 |

### POSE 主要代码改动

| 文件 | 内容 |
|---|---|
| `include/yolo11_pose_api.h` | 新增 POSE Runtime API 头文件 |
| `api/yolo11_pose_api.cpp` | 封装原始 `yolo11_pose.cpp` 推理逻辑 |
| `include/server/model_runner.h` | 新增 `PoseModelRunner` |
| `src/server/model_runner.cpp` | `createModelRunner("pose")` 返回 POSE Runner |
| `src/server/result_serializer.cpp` | 新增 pose JSON 输出：bbox、keypoints、skeleton |
| `src/server/http_controller.cpp` | 注册 `/api/v1/pose/image/async` |
| `labels/pose_coco.txt` | POSE 类别标签，当前为 `person` |
| `config/server_pose.yaml` | POSE Server 配置，端口 8085 |
| `config/worker_pose.yaml` | POSE Worker 配置 |
| `scripts/start_pose.ps1` / `stop_pose.ps1` | POSE 独立启动 / 停止脚本 |
| `tools/phase17_5_pose_registry_test.py` | POSE Worker Registry 测试 |
| `tools/phase17_5_pose_smoke_test.py` | POSE 端到端 smoke 测试 |
| `tools/run_phase17_5_regression.py` | POSE + CLS + Phase16 全量回归入口 |

### POSE 返回结果语义

POSE 结果的关键字段：

```text
model_type = pose
pose_coordinate_system = original_image_pixels
keypoint_format = coco17_xy_conf
skeleton_format = coco17_pairs
num_poses = 3
detections[].bbox
detections[].keypoints
detections[].skeleton
detections[].valid_keypoints
```

每个 keypoint 包含：

```text
id
name
x
y
confidence
visible
in_image
```

其中 `visible` 是根据置信度阈值判断的逻辑可见性，`in_image` 表示坐标是否落在原图范围内。坐标已经从模型输入坐标还原到原始图片像素坐标。

### POSE 验收命令

启动：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_pose.ps1 -ExeDir .\out\build\x64-Debug
```

检查：

```powershell
curl.exe "http://127.0.0.1:8085/api/v1/health"
curl.exe "http://127.0.0.1:8085/api/v1/ready?model=pose&task_kind=image_async&worker_group=image_pose_gpu0"
curl.exe "http://127.0.0.1:8085/api/v1/workers?model=pose"
curl.exe "http://127.0.0.1:8085/api/v1/metrics?model=pose"
```

提交任务：

```powershell
curl.exe -X POST "http://127.0.0.1:8085/api/v1/pose/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"
```

自动测试：

```powershell
python tools\phase17_5_pose_registry_test.py --url http://127.0.0.1:8085
python tools\phase17_5_pose_smoke_test.py --url http://127.0.0.1:8085 --image .\images\bus.png
python tools\run_phase17_5_regression.py --skip-phase17
```

Redis 验收：

```powershell
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:pose yolo11_pose_group
```

阶段结果：

```text
POSE registry test：PASS
POSE smoke test：PASS
Phase 17.5 Regression Summary：success=True
num_poses=3
每个 person 返回 17 个 COCO keypoints
metrics_found=true
total_tasks=3
total_tasks_done=3
total_tasks_failed=0
worker_distribution.pose_worker_1=3
XPENDING=0
```

---

### Phase 17.5 全量回归

Phase 17.5 最终不只验证 POSE，还验证新增 POSE 没有破坏 Phase 17 CLS 和 Phase 16 的旧服务。

启动旧服务、CLS、POSE：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_all.ps1 -ExeDir .\out\build\x64-Debug
powershell -ExecutionPolicy Bypass -File .\scripts\start_cls.ps1 -ExeDir .\out\build\x64-Debug
powershell -ExecutionPolicy Bypass -File .\scripts\start_pose.ps1 -ExeDir .\out\build\x64-Debug
```

完整回归：

```powershell
python tools\run_phase17_5_regression.py
```

最终结果：

```text
Phase 17.5 Regression Summary: success=True
[PASS] 01_phase17_5_pose_registry
[PASS] 02_phase17_5_pose_smoke
[PASS] 03_phase17_cls_regression
```

其中 Phase 17 CLS 回归内部继续调用 Phase 16 Full Regression，因此同时覆盖：

```text
Detect 图片异步
OBB 图片异步
Video 文件异步
Stream 实时流
CLS 图片异步
POSE 图片异步
Worker Registry
Metrics / Ready / Workers
Redis Pending
```

停止所有服务：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\stop_pose.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\stop_cls.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\stop_all.ps1
```

---

### Phase 17 / 17.5 遇到的问题与解决方案

#### 1. 分类结果显示 `class_608`，不是具体 ImageNet 类名

原因：当前 `labels/imagenet.txt` 使用 `class_0` 到 `class_999` 的占位标签。这样可以保证服务链路先跑通，但语义显示不完整。

解决：后续真实部署时，将 `labels/imagenet.txt` 替换为真实 ImageNet 1000 类标签文件。

反思：labels 文件和 engine 是一组部署资产。模型能推理不代表类别语义已经正确。

#### 2. `phase` 字段要区分服务阶段

CLS 起初可能仍显示旧的：

```text
phase = phase15_worker_capability_registry
```

这不影响功能，但不利于文档和接口观测。最终建议 CLS 显示：

```text
phase = phase17_cls_image_async_service
```

POSE 显示：

```text
phase = phase17_5_pose_image_async_service
```

反思：`phase` 字段虽然不是业务逻辑，但它是工程回溯的重要观测字段。

#### 3. `recent_done_count=0` 不等于任务失败

metrics 中的：

```text
recent_window_seconds = 60
recent_done_count = 0
recent_qps_60s = 0.0
```

只代表最近 60 秒没有新完成任务。如果任务完成时间已经超过 60 秒，这是正常结果。判断累计成功应看：

```text
total_tasks_done
total_tasks_failed
worker_distribution
last_task_id
last_finish_time_ms
```

反思：metrics 要理解统计窗口，不能只看一个字段。

#### 4. Worker `processed_count=0` 也可能正常

停止并重新启动 Worker 后，heartbeat 中的 `processed_count` 会从 0 开始，因为它属于当前进程生命周期计数。但 Redis metrics 中的 `total_done` 是累计统计，不会因为进程重启归零。

反思：进程级计数和 Redis 持久累计计数不是同一个口径。

#### 5. POSE GPU 后处理暂不启用

原始 `yolo11_pose.cpp` 中已经说明 pose GPU postprocess 当前不支持，因此 Phase 17.5 采用 CPU 后处理。即使配置中存在 `use_gpu_postprocess`，POSE 服务化第一版也应优先保证正确性和稳定性。

反思：服务化阶段不要为了追求性能强行启用未验证路径。正确性优先，性能后置。

#### 6. WSL localhost 代理提示不是 Redis 错误

执行 WSL Redis CLI 时可能出现：

```text
wsl: 检测到 localhost 代理配置，但未镜像到 WSL。NAT 模式下的 WSL 不支持 localhost 代理。
```

只要后续 Redis 命令返回 `PONG` 或 `XPENDING=0`，这个提示不影响验收。

反思：终端环境 warning 和服务错误要区分，不要看到 warning 就误判系统失败。

### Phase 17 / 17.5 阶段结论

```text
Phase 17.0 已完成 CLS 图片异步服务接入。
Phase 17.5 已完成 POSE 图片异步服务接入。
```

当前系统已经具备 Detect / OBB / CLS / POSE 四类图片异步服务，以及 Detect 视频文件异步服务和单路实时流服务。新增 CLS 和 POSE 后，旧的 Detect / OBB / Video / Stream 全量回归仍然通过，说明 `ModelOutput` 抽象、Runner 工厂、ResultSerializer 和 Worker 主循环的修改没有破坏旧功能。Phase 17.5 是项目从“四类工程服务”走向“YOLO 五大基础任务接口补齐”的关键节点。

下一阶段建议进入：

```text
Phase 18.0：SEG 图片异步服务
```

Phase 18 需要重点控制：

```text
mask / polygon 输出格式
JSON 体积
结果图 overlay
Redis Binary 与本地文件存储策略
mask 是否单独下载
SEG 与 Detect/OBB/CLS/POSE 的统一回归
```

---


## 关键问题记录与反思

### 1. 第三方库安装成功后，下一步不是继续装库，而是接入 CMake

已安装：

```text
nlohmann-json
yaml-cpp
crow
asio
```

但仅仅安装成功不代表项目能找到它们。必须在 CMake 中：

```cmake
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)
```

并且 CMake 配置时要使用 vcpkg toolchain。

反思：**依赖安装只是第一步，CMake target 能正确链接才是关键。**

### 2. server 源码不能混进原始 yolo11_det / yolo11_obb

最初的问题之一是 `src/server/*.cpp` 被编进了多个 target。这样原始命令行程序也会尝试编译 HTTP Server 代码，导致一堆看似无关的错误。

反思：**服务化扩展应该新增模块，而不是污染原 demo。**

### 3. PowerShell 运行 exe 必须加 `.\`

错误写法：

```powershell
build\Release\yolo11_server.exe
```

正确写法：

```powershell
.\out\build\x64-Debug\yolo11_server.exe
```

如果不知道 exe 在哪里：

```powershell
Get-ChildItem -Recurse -Filter yolo11_server.exe | Select-Object FullName
```

反思：**Windows 下 CMD 和 PowerShell 对可执行文件路径的处理方式不同。**

### 4. engine 路径错误是最常见运行错误

曾经配置写的是：

```yaml
engine_path: "./engines/yolo11n.engine"
```

但真实路径是：

```text
D:\tensorrtx\yolo11\engine\yolo11n.engine
```

导致：

```text
Cannot open engine file
```

反思：**服务配置阶段优先使用绝对路径，避免相对路径歧义。**

### 5. bbox 坐标不一致问题的根因

最初 HTTP JSON 直接读取了：

```cpp
detection.bbox[0]
detection.bbox[1]
detection.bbox[2]
detection.bbox[3]
```

并误以为它是原图坐标下的 `x, y, w, h`。

但实际画图时用的是：

```cpp
cv::Rect r = get_rect(img, res[j].bbox);
```

`get_rect()` 会把模型输出坐标从 letterbox / 模型输入坐标系还原到原图坐标系。

所以正确做法是：**JSON 序列化时也复用 `get_rect()`，保证 JSON 坐标和结果图画框一致。**

反思：**后端接口不能只看字段名，要确认字段处于哪个坐标系。**

### 6. 为什么要加 `class_name`

仅返回：

```json
"class_id": 0
```

对调用者不直观。

现在返回：

```json
"class_id": 0,
"class_name": "person"
```

前端和日志都更容易理解。

反思：**接口不是只给模型看，而是给人和外部系统看。**

### 7. 为什么 `raw_model_bbox` 只在 debug=true 时返回

`raw_model_bbox` 对调试有用，但对正式前端容易造成混淆，因为它不是原图坐标。

所以普通接口不返回：

```json
"raw_model_bbox": ...
```

只有调试请求：

```text
/api/v1/detect/image?debug=true
```

才返回。

反思：**正式接口要干净，调试信息要可开关。**

---


### 8. Redis Stream 不是简单的缓存，而是任务队列

最初对 Redis 的理解容易停留在 key-value 缓存，但这次使用的是 Redis Stream。

这意味着 Redis 不只是存图片或结果，而是在承担“消息队列”的角色：

```text
XADD 写入任务
XGROUP 创建消费组
XREADGROUP 消费任务
XACK 确认任务处理完成
```

反思：**Redis Stream 的价值在于任务可排队、可消费、可追踪；它是从同步服务走向异步服务的关键过渡。**

### 9. Windows Redis 和 WSL Redis 同时存在会造成严重混淆

本阶段最典型的问题是 Windows 本机已经有一个 Redis：

```text
127.0.0.1:6379 -> D:\redis\redis-server.exe
```

而真正想使用的是 WSL 中的 Redis：

```text
172.19.196.109:6379 -> /usr/bin/redis-server, Redis 8.2.1
```

如果 `server.yaml` 写成：

```yaml
host: "127.0.0.1"
port: 6379
```

程序会连到 Windows Redis，而不是 WSL Redis，导致出现：

```text
ERR unknown command 'XGROUP'
```

最终通过以下命令确认了两个 Redis 的区别：

```powershell
netstat -ano | findstr :6379
Get-Process -Id <PID>
```

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

反思：**部署时不能只说“Redis 在 6379”，必须明确是哪个操作系统、哪个 IP、哪个进程、哪个版本。**

### 10. hiredis 在 Windows 下需要 WinSock

hiredis 在 Windows 编译时可能出现 `timeval` 未定义等问题，原因是 Windows 网络类型来自 WinSock。

解决方式是在 `redis_task_queue.cpp` 中保证 WinSock 头文件先于 hiredis 引入：

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

同时 CMake 需要链接：

```cmake
if(WIN32)
    target_link_libraries(yolo11_server PRIVATE ws2_32)
endif()
```

反思：**跨平台库在 Windows 上经常不是“只 include 一个头文件”这么简单，系统网络库也必须正确链接。**

### 11. Redis 超时问题不一定是代码错，也可能是连错服务或服务刚重启

本阶段遇到过：

```text
failed to connect Redis: timed out
failed to connect Redis: connection refused
```

最后排查发现，原因可能包括：

- WSL Redis 没启动
- WSL IP 改变
- Windows Redis 占用了 localhost 6379
- C++ 程序连错 Redis
- Redis 正在重启

正确排查顺序应该是：

```bash
redis-cli ping
hostname -I
ss -lntp | grep 6379
```

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

反思：**网络问题不要一上来改代码，先确认服务、端口、IP、版本和进程。**

### 12. Phase 2 的核心不是“又加了一个接口”，而是服务形态改变了

同步接口：

```text
Client 等待 HTTP 请求完成
```

异步接口：

```text
Client 先拿 task_id，之后查询结果
```

这意味着系统从“单次调用”开始向“任务系统”转变。后面拆分 Worker、多 GPU、多进程、多机器，本质上都是围绕这个任务系统继续扩展。

反思：**异步队列是服务化部署的分水岭，它让模型推理从函数调用变成可调度任务。**

## 最小运行命令

### Detection 命令行和 API demo

```bat
cd /d D:\tensorrtx\yolo11

python gen_wts.py -w weights/yolo11n.pt -o yolo11n.wts -t detect
copy /Y yolo11n.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug

yolo11_det.exe -s yolo11n.wts yolo11n.engine n
yolo11_det.exe -d yolo11n.engine D:\tensorrtx\yolo11\images c

demo_image.exe yolo11n.engine D:\tensorrtx\yolo11\images\a.jpg det_result.jpg
```

### HTTP Server

```powershell
cd D:\tensorrtx\yolo11

cmake --build out\build\x64-Debug --config Debug --target yolo11_server
cmake --build out\build\x64-Debug --config Debug --target yolo11_worker
cmake --build out\build\x64-Debug --config Debug --target yolo11_video_worker
cmake --build out\build\x64-Debug --config Debug --target yolo11_stream_worker

.\out\build\x64-Debug\yolo11_server.exe .\config\server.yaml

# 另开终端启动 Worker
.\out\build\x64-Debug\yolo11_worker.exe .\config\server.yaml --consumer-name worker_1

curl.exe http://127.0.0.1:8080/api/v1/health

curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image" -H "Content-Type: image/png" --data-binary "@D:/tensorrtx/yolo11/images/bus.png"

curl.exe -X POST "http://127.0.0.1:8080/api/v1/detect/image/async" `
  -H "Content-Type: image/png" `
  --data-binary "@D:/tensorrtx/yolo11/images/bus.png"

curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>"

curl.exe "http://127.0.0.1:8080/api/v1/result/<task_id>/image" -o result.jpg
```

### Video Detect 异步服务

```powershell
cd D:\tensorrtx\yolo11

cmake --build out\build\x64-Debug --config Debug --target yolo11_server
cmake --build out\build\x64-Debug --config Debug --target yolo11_video_worker

# 窗口 1：Video Server
.\out\build\x64-Debug\yolo11_server.exe .\config\server_video.yaml

# 窗口 2：Video Worker
.\out\build\x64-Debug\yolo11_video_worker.exe .\config\worker_video.yaml --consumer-name video_worker_1

# 窗口 3：检查和测试
curl.exe "http://127.0.0.1:8082/api/v1/health"
curl.exe "http://127.0.0.1:8082/api/v1/ready"
curl.exe "http://127.0.0.1:8082/api/v1/workers"
curl.exe "http://127.0.0.1:8082/api/v1/metrics"

python tools\phase13_video_smoke_test.py `
  --url http://127.0.0.1:8082 `
  --video D:/tensorrtx/yolo11/videos/test.mp4 `
  --download D:/tensorrtx/yolo11/runtime/output/videos/detect/result_smoke.mp4
```


### Stream 实时流服务

```powershell
cd D:\tensorrtx\yolo11

cmake --build out\build\x64-Debug --config Debug --target yolo11_server
cmake --build out\build\x64-Debug --config Debug --target yolo11_stream_worker

# 窗口 1：Stream Server
.\out\build\x64-Debug\yolo11_server.exe .\config\server_stream.yaml

# 窗口 2：Stream Worker
.\out\build\x64-Debug\yolo11_stream_worker.exe .\config\worker_stream.yaml --consumer-name stream_worker_1

# 窗口 3：检查
curl.exe "http://127.0.0.1:8083/api/v1/health"
curl.exe "http://127.0.0.1:8083/api/v1/ready"
curl.exe "http://127.0.0.1:8083/api/v1/workers"

# 推荐用脚本测试
python tools\phase14_5_stream_stability_test.py `
  --url http://127.0.0.1:8083 `
  --source-type camera `
  --camera-id 0 `
  --repeat 3 `
  --run-seconds 5
```

### OBB

使用官方 DOTA OBB 模型前，先在 `include/config.h` 中设置 `kNumClass = 16`，然后重新编译项目。

```bat
cd /d D:\tensorrtx\yolo11

python gen_wts.py -w weights/yolo11n-obb.pt -o yolo11n-obb.wts -t obb
copy /Y yolo11n-obb.wts out\build\x64-Debug\

cd /d D:\tensorrtx\yolo11\out\build\x64-Debug

yolo11_obb.exe -s yolo11n-obb.wts yolo11n-obb.engine n
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c

demo_obb_image.exe yolo11n-obb.engine D:\tensorrtx\yolo11\images\a.jpg a_obb_result.jpg cpu
```

---

## 常见问题

### `Cannot open engine file`

检查 `config/server.yaml`：

```yaml
engine_path: "D:/tensorrtx/yolo11/engine/yolo11n.engine"
```

### PowerShell 无法识别 exe

PowerShell 运行相对路径 exe 时需要加：

```powershell
.\out\build\x64-Debug\yolo11_server.exe
```

如果不知道文件在哪里：

```powershell
Get-ChildItem -Recurse -Filter yolo11_server.exe | Select-Object FullName
```

### `Failed to load myplugins.dll`

确认 `myplugins.dll` 和当前运行的 `.exe` 位于同一目录。CMake 默认会自动复制，如果移动了可执行文件，则需要手动复制。

### `read xxx.engine error!`

说明当前可执行目录下没有该 engine 文件，或者 engine 文件名写错。可以用下面命令检查：

```bat
dir *.engine
```

### `Detected OBB objects: 0`

常见原因：

- `kNumClass` 仍然是 `80`，但 OBB 模型需要 `16` 类
- 使用了普通 Detection engine，却运行 OBB demo
- 测试图片不符合 OBB 模型领域
- 置信度阈值设置过高

调试时可以临时降低 `include/config.h` 中的置信度阈值：

```cpp
const static float kConfThresh = 0.25f;
```

然后重新编译并重新生成 engine。

### 使用 `g` 时出现 `vector subscript out of range`

OBB 验证阶段请先使用 CPU 后处理：

```bat
yolo11_obb.exe -d yolo11n-obb.engine D:\tensorrtx\yolo11\images c
```

### `gen_wts.py` 不接受 `-t obb`

请修改 `gen_wts.py`，确保类型选项包含 `obb`：

```python
choices=['detect', 'cls', 'seg', 'pose', 'obb']
```

并确保 YOLO head 分支包含 OBB：

```python
if m_type in ['detect', 'seg', 'pose', 'obb']:
```

### JSON bbox 和结果图不一致

检查 `src/server/http_controller.cpp` 中 Detection -> JSON 的逻辑，必须使用与画图一致的坐标还原方式，即复用 `get_rect()`。

---


### `ERR unknown command 'XGROUP'`

说明当前连接到的 Redis 不支持 Redis Stream 消费组，或者程序连到了错误的 Redis。

本项目中曾经出现过 Windows Redis 和 WSL Redis 同时存在的问题：

```text
127.0.0.1:6379        -> Windows Redis，D:\redis\redis-server.exe
172.19.196.109:6379   -> WSL Redis，Redis 8.2.1
```

排查命令：

```powershell
netstat -ano | findstr :6379
Get-Process -Id <PID>
```

```bash
redis-cli -h 172.19.196.109 -p 6379 INFO server
```

### `failed to connect Redis: timed out` 或 `connection refused`

先确认 WSL Redis 是否正常：

```bash
sudo service redis-server status
redis-cli ping
ss -lntp | grep 6379
hostname -I
```

再从 Windows 测试：

```powershell
Test-NetConnection 172.19.196.109 -Port 6379
```

如果 WSL IP 变了，需要同步修改 `config/server.yaml`。

### Windows 本机 Redis 干扰

如果 Windows 本机 Redis 不需要，可以禁用：

```powershell
Get-Service | Where-Object { $_.Name -match "redis" -or $_.DisplayName -match "redis" }
Stop-Service Redis
Set-Service Redis -StartupType Disabled
```

### hiredis 在 Windows 下编译问题

hiredis 需要安装：

```bat
.\vcpkg.exe install hiredis:x64-windows --vcpkg-root D:\vcpkg
```

CMake 中使用：

```cmake
find_package(hiredis CONFIG REQUIRED)
target_link_libraries(yolo11_server PRIVATE hiredis::hiredis ws2_32)
```

如果出现 `timeval` 未定义，检查 `redis_task_queue.cpp` 中 WinSock 头文件是否在 `hiredis.h` 之前。

### 视频任务 `processed_frames < total_frames`

如果看到：

```text
total_frames = 111
processed_frames = 87
status = done
progress = 1.0
```

这通常不是任务失败，而是 OpenCV 对 MP4 的 `CAP_PROP_FRAME_COUNT` 返回了容器估计帧数，实际 `VideoCapture.read()` 能解码出的有效帧数可能更少。只要 `status=done`、结果视频能下载、`XPENDING=0`，就说明任务正常在 EOF 结束。

### video 输出目录不统一

Phase 13.5 之后推荐统一使用：

```text
runtime/input/videos/detect
runtime/output/videos/detect
runtime/logs/video
```

如果仍然看到 `temp/video/output`，说明使用了旧配置：

```text
config/server_video_phase13.yaml
config/server_video_phase13_5.yaml
```

应该改用当前配置：

```text
config/server_video.yaml
config/worker_video.yaml
```



### `/api/v1/stream/start` 返回 `INVALID_JSON`

PowerShell 中直接传 JSON 字符串可能会丢失双引号。建议使用：

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

### Stream 任务一直是 `created`

说明任务已经创建，但 Worker 还没开始处理。排查：

```powershell
curl.exe "http://127.0.0.1:8083/api/v1/workers"
```

```bash
redis-cli -h 172.19.196.109 -p 6379 XPENDING yolo:stream:live:detect yolo11_stream_detect_group
redis-cli -h 172.19.196.109 -p 6379 GET yolo:streamtask:yolo_stream_live_detect:active
```

如果 Worker 正在跑旧 stream，先 stop 旧任务；如果 Worker 卡住，重启 `yolo11_stream_worker.exe`。

### 重复启动 Stream 返回 HTTP 409

这是 Phase 14.5 的预期保护：

```text
STREAM_ALREADY_ACTIVE
```

当前阶段是单 Worker 单活流设计，必须先 stop 当前 stream，再 start 新 stream。

### `camera_id=99` 进入 `reconnecting` 后 `failed`

这是预期结果。Worker 会尝试重新打开 source，达到 `reconnect_max_attempts` 后写入 failed，并记录 `last_error`。


### `/metrics?model=stream` 一直是 0

这个问题最终定位为：stream 生命周期已经能 `running -> stopped`，但是 `markStreamStopped()` / `markStreamFailed()` 里面没有写入 stream 专用 metrics，或者源码改了但 `yolo11_stream_worker.exe` 没有重新编译/重启。

正确检查方式：

```powershell
Select-String -Path .\src\server\redis_task_queue.cpp `
  -Pattern "markStreamStopped|markStreamFailed|HINCRBY %s done_count 1|HINCRBY %s failed_count 1" `
  -Context 2,2
```

Redis 侧检查：

```bash
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:yolo_stream_live_detect:global
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:yolo_stream_live_detect:worker:done
redis-cli -h 172.19.196.109 -p 6379 HGETALL yolo:metrics:yolo_stream_live_detect:worker:failed
```

修复后不要 `--clean-first`，优先增量编译，避免无关 CUDA 文件重新触发 `cudafe++` 崩溃：

```powershell
cmake --build out\build\x64-Debug --config Debug --target yolo11_stream_worker --parallel 1
cmake --build out\build\x64-Debug --config Debug --target yolo11_server --parallel 1
```

反思：实时流任务的 done/failed 不发生在 XACK 时，而发生在 `stopped/failed` 终态，所以 metrics 必须写在 stream 的终态函数里。


## Phase 16：工程封板与上线前整理

### 阶段定位

Phase 16 不是继续新增模型能力，也不是修改 Detect / OBB / Video / Stream 的 C++ 主业务链路，而是把 Phase 15 已经跑通的四类服务整理成一个可重复启动、可统一检查、可一键回归、可稳定停止的工程封板版本。

这一阶段的核心目标可以概括为：

```text
让项目从“我知道怎么手动启动”升级为“任何人按固定脚本都能启动、检查、回归和停止”。
```

Phase 16 的意义在于给后续 CLS / Pose / Seg 接入建立一条“保险绳”。后续每新增一个模型，都必须先跑 Phase 16 全量回归，确认 Detect / OBB / Video / Stream 旧功能没有被破坏。

### 本阶段完成内容

| 模块 | 新增 / 修改内容 | 作用 |
|---|---|---|
| 启动脚本 | `scripts/start_all.ps1` | 一键启动 Detect / OBB / Video / Stream 的 4 个 Server 和 4 个 Worker |
| 停止脚本 | `scripts/stop_all.ps1` | 根据 PID 文件停止服务，并支持按进程名 / 端口兜底清理 |
| 检查脚本 | `scripts/check_all.ps1` | 统一检查 HTTP health / ready / workers / metrics 与 Redis PING / XPENDING |
| 清理脚本 | `scripts/clean_runtime.ps1` | 清理 runtime / reports 等运行产物，辅助重新测试 |
| 全量回归 | `tools/run_full_regression.py` | 串联 Phase 12 / 13 / 14 / 15 的核心测试脚本 |
| 配置模板 | `config/*.example.yaml` | 提供可迁移配置模板，减少本机绝对路径依赖 |
| 报告归档 | `reports/phase16/<timestamp>/` | 保存 full regression 的 JSON / TXT 报告 |
| PID 归档 | `runtime/pids/phase16_services.json` | 记录一键启动的 8 个进程 PID，供 stop_all 使用 |
| 进程日志 | `runtime/logs/phase16_process/` | 记录各进程 stdout / stderr，方便启动失败排查 |

### Phase 16 标准操作流程

完整闭环只有四步：

```powershell
cd D:\tensorrtx\yolo11

powershell -ExecutionPolicy Bypass -File .\scripts\start_all.ps1 -ExeDir .\out\build\x64-Debug
powershell -ExecutionPolicy Bypass -File .\scripts\check_all.ps1 -UseWslRedisCli -WslDistro Ubuntu
python tools\run_full_regression.py
powershell -ExecutionPolicy Bypass -File .\scripts\stop_all.ps1
```

这四步分别对应：

```text
启动所有服务 -> 检查所有服务 -> 执行全量回归 -> 停止所有服务
```

### start_all.ps1 逻辑

`start_all.ps1` 会按照固定顺序启动 8 个进程：

```text
worker_detect       yolo11_worker.exe        config\worker_detect.yaml --consumer-name worker_1
worker_obb          yolo11_worker.exe        config\worker_obb.yaml --consumer-name obb_worker_1
worker_video        yolo11_video_worker.exe  config\worker_video.yaml --consumer-name video_worker_1
worker_stream       yolo11_stream_worker.exe config\worker_stream.yaml --consumer-name stream_worker_1
server_detect_8080  yolo11_server.exe        config\server_detect.yaml
server_obb_8081     yolo11_server.exe        config\server_obb.yaml
server_video_8082   yolo11_server.exe        config\server_video.yaml
server_stream_8083  yolo11_server.exe        config\server_stream.yaml
```

启动成功后，会写入：

```text
runtime/pids/phase16_services.json
runtime/logs/phase16_process/
```

这一步解决了之前“必须手动打开多个 PowerShell 窗口、靠记忆启动服务”的问题。

### check_all.ps1 逻辑

`check_all.ps1` 主要做两类检查。

第一类是 HTTP 检查：

```text
GET /api/v1/health
GET /api/v1/ready
GET /api/v1/workers
GET /api/v1/metrics
```

覆盖四个端口：

```text
8080 detect
8081 obb
8082 video
8083 stream
```

第二类是 Redis 检查：

```text
PING
XPENDING yolo:stream:detect yolo11_group
XPENDING yolo:stream:obb yolo11_obb_group
XPENDING yolo:stream:video:detect yolo11_video_detect_group
XPENDING yolo:stream:live:detect yolo11_stream_detect_group
```

最终验收结果：

```text
detect  health=True ready=True workers=1 metrics=True
obb     health=True ready=True workers=1 metrics=True
video   health=True ready=True workers=1 metrics=True
stream  health=True ready=True workers=1 metrics=True

PING: PONG
XPENDING detect = 0
XPENDING obb    = 0
XPENDING video  = 0
XPENDING stream = 0

PASS: all services are ready and Redis pending looks clean.
```

注意：当前 `/health` 中仍然显示：

```text
phase = phase15_worker_capability_registry
```

这是正常现象。Phase 16 没有修改 C++ 主链路，只是在工程脚本和回归层面进行封板整理，所以 C++ 服务内部 phase 字段仍保持 Phase 15。

### run_full_regression.py 逻辑

`tools/run_full_regression.py` 是 Phase 16 最重要的测试入口，它不是重新写测试，而是串联已有阶段脚本：

```text
01_phase12_detect_obb_smoke
02_phase13_video_smoke
03_phase13_5_video_cancel
04_phase14_stream_source_matrix
06_registry_detect
07_registry_obb
08_registry_video
09_registry_stream
```

最终通过结果：

```text
Phase 16 Full Regression Summary
success=True
```

报告输出：

```text
reports/phase16/20260709_002239/phase16_full_regression_summary.json
```

这说明 Detect / OBB 图片异步服务、Video 文件任务、Video cancel、Stream file source、四类 Worker Registry 均通过。

### stop_all.ps1 逻辑

`stop_all.ps1` 会优先读取：

```text
runtime/pids/phase16_services.json
```

然后按 PID 停止 `start_all.ps1` 启动的 8 个进程。如果 PID 文件不存在或进程已经退出，会给出 warning，但不会影响整体停止流程。

如果端口或进程残留，可以使用兜底参数：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\stop_all.ps1 -KillByName -KillByPort
```

手动兜底：

```powershell
Get-Process yolo11_server,yolo11_worker,yolo11_video_worker,yolo11_stream_worker -ErrorAction SilentlyContinue | Stop-Process -Force
```

### 本阶段技术栈与知识点

| 技术点 | 本阶段用途 | 学习价值 |
|---|---|---|
| PowerShell 脚本 | 启动、停止、检查服务 | 将手动操作固化成可复现流程 |
| WSL Redis CLI | 从 Windows 调用 Ubuntu 中的 redis-cli | 解决 Windows 没有 redis-cli 的问题 |
| Redis XPENDING | 检查四条队列是否有未确认消息 | 判断异步服务是否有任务残留 |
| HTTP health/ready/workers/metrics | 统一检查服务状态 | 区分服务存活、服务可用、Worker 在线和指标状态 |
| Python subprocess | 串联阶段测试脚本 | 形成全量回归入口 |
| JSON 报告 | 保存回归结果 | 方便阶段归档和后续对比 |
| PID 文件 | 记录脚本启动的进程 | 支持自动停止服务 |
| 配置模板 | `*.example.yaml` | 降低本机路径耦合，方便后续部署迁移 |

### 遇到的问题与解决方案

#### 1. PowerShell 找不到 redis-cli

现象：

```text
无法将“redis-cli”项识别为 cmdlet、函数、脚本文件或可运行程序的名称
```

原因：Windows PowerShell 环境没有安装 Redis CLI，而 Redis 实际运行在 WSL Ubuntu 中。

解决：使用 WSL 模式调用 Redis CLI：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_all.ps1 -UseWslRedisCli -WslDistro Ubuntu
```

手动验证：

```powershell
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 ping
```

期望：

```text
PONG
```

#### 2. 默认 WSL 进入 docker-desktop

现象：

```text
/bin/ash: redis-cli: not found
```

原因：`wsl -l -v` 显示默认发行版是 `docker-desktop`，而不是 `Ubuntu`。

解决：显式指定发行版：

```powershell
wsl -d Ubuntu -- redis-cli -h 172.19.196.109 -p 6379 ping
```

或者在检查脚本中使用：

```powershell
-WslDistro Ubuntu
```

#### 3. WSL localhost 代理 warning

现象：

```text
wsl: 检测到 localhost 代理配置，但未镜像到 WSL。NAT 模式下的 WSL 不支持 localhost 代理。
```

判断：这不是 Redis 错误。只要后面返回 `PONG`，Redis 就是通的。

#### 4. check_all.ps1 卡在 Redis checks

现象：脚本显示：

```text
=== Redis checks ===
```

然后长时间没有输出。

原因：早期脚本调用 WSL Redis CLI 时，Redis 命令参数没有正确传入，实际执行成了：

```text
redis-cli -h 172.19.196.109 -p 6379
```

缺少 `PING` 或 `XPENDING`，导致 `redis-cli` 进入等待输入状态。

解决：修复脚本参数传递，将容易冲突的参数变量改成明确的 `$RedisArgs`，并增加命令超时，避免无限卡住。

#### 5. stop_all.ps1 报 `$Pid` 只读

现象：

```text
无法覆盖变量 Pid，因为该变量为只读变量或常量。
```

原因：PowerShell 内置 `$PID` 是只读变量，脚本函数参数使用 `$Pid`，PowerShell 变量名大小写不敏感，因此发生冲突。

解决：将函数参数从 `$Pid` 改为 `$ProcessId`。

反思：PowerShell 脚本中不要使用 `$PID` / `$Host` / `$Error` 等容易与内置变量冲突的名称。

### Phase 16 最终测试结果

`check_all.ps1` 结果：

```text
detect  True  True  1  True  phase15_worker_capability_registry
obb     True  True  1  True  phase15_worker_capability_registry
video   True  True  1  True  phase15_worker_capability_registry
stream  True  True  1  True  phase15_worker_capability_registry

PING: PONG

detect pending = 0
obb    pending = 0
video  pending = 0
stream pending = 0

PASS: all services are ready and Redis pending looks clean.
```

`run_full_regression.py` 结果：

```text
[PASS] 01_phase12_detect_obb_smoke
[PASS] 02_phase13_video_smoke
[PASS] 03_phase13_5_video_cancel
[PASS] 04_phase14_stream_source_matrix
[PASS] 06_registry_detect
[PASS] 07_registry_obb
[PASS] 08_registry_video
[PASS] 09_registry_stream

Phase 16 Full Regression Summary
success=True
```

`stop_all.ps1` 结果：

```text
Stop request completed.
```

最终结论：

```text
Phase 16.0：工程封板与上线前整理 —— 已完成并通过验收。
```

### Phase 16 学习反思

1. 工程后期最重要的不是继续堆功能，而是保证已有功能可以被稳定启动、验证和停止。
2. 一键启动脚本的价值很高，它把“靠记忆打开多个窗口”变成了可复现流程。
3. `/health`、`/ready`、`/workers`、`/metrics` 四类接口要一起看，单看 health 不足以说明服务可用。
4. Redis `XPENDING=0` 仍然是异步系统封板验收的关键指标。
5. 全量回归脚本是后续接 CLS / Pose / Seg 的保险绳，新增模型后必须先跑旧功能回归。
6. PowerShell 脚本也会有工程坑，例如 WSL 默认发行版、参数传递、只读变量名冲突。
7. 配置模板和相对路径不是形式主义，而是为了让项目脱离“只能在自己电脑上跑”的状态。
8. Phase 16 之后，项目已经具备上线候选版本的基本形态；后续应进入五模型补齐，而不是继续扩张平台功能。

### Phase 16 之后的推进建议

下一阶段进入：

```text
Phase 17.0：CLS 图片异步服务接入
```

建议开发原则：

```text
先接 CLS 图片异步接口，不做视频分类、不做流分类；
先保证 top1 / topk JSON 正确，不追求复杂结果图；
接入后必须执行 Phase 16 全量回归；
旧的 Detect / OBB / Video / Stream 全部 PASS 后，才能标记 Phase 17 完成。
```

## Git Ignore

以下生成文件不建议上传到 GitHub：

```text
out/
build/
.vs/
runtime/
reports/
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

## 下一阶段路线

当前已经完成：

```text
Phase 1：同步 HTTP 图片检测服务
Phase 1.5：bbox 坐标修正、结果图访问、debug 模式
Phase 2：Redis task/status/result 存储
Phase 3：Redis Stream 异步任务队列
Phase 4：Redis Stream Consumer Group 多 Worker 推理池
Phase 5：压测统计、Pending 恢复、XTRIM 清理与稳定性验证
Phase 6：Redis 连接复用、自动重连与生产化稳定性优化
Phase 7：Server / Worker 独立进程拆分、Redis Binary 图片存储与 Worker 崩溃恢复
Phase 8：健康检查增强、Worker 心跳、Redis 图片 TTL / 内存控制、Worker 离线拒绝入队
Phase 8.5：spdlog 日志系统、labels_path 标签外置、ResultSerializer 收口、metrics 指标接口
Phase 10.0：OBB 图片异步服务最小闭环
Phase 10.5：多模型配置与 Runner 抽象整理
Phase 11.0：OBB 稳定性、异常输入与 Worker 恢复验证
Phase 12.0：Detect + OBB 双服务并行部署
Phase 13.0：Detect 视频文件异步检测最小闭环
Phase 13.5：视频稳定性、取消、异常输入、Worker 恢复与批量压测
Structure Cleanup：config 清理与 runtime 目录统一
Phase 14.0：RTSP / 摄像头流任务最小闭环
Phase 14.5：流任务稳定性增强、重复启动保护与重连失败处理
Phase 15：Worker Capability Registry、能力过滤、四类服务统一观测与真实任务验收
Phase 16：工程封板与上线前整理，一键启动/检查/回归/停止，全量回归 success=True
Phase 17：CLS 图片异步服务接入，ModelOutput 最小抽象，top1/topk 输出
Phase 17.5：POSE 图片异步服务接入，COCO17 keypoints/skeleton 输出，全量回归 success=True
```

下一步建议进入：

```text
Phase 18：SEG 图片异步服务接入
```

Phase 18 推荐目标：

```text
SEG 图片异步服务接入
├── 新增 Yolo11SegDetector API 包装
├── 新增 SegModelRunner，接入现有 IModelRunner / ModelOutput 体系
├── 新增 ResultSerializer::serializeSeg，输出 bbox + mask/polygon metadata
├── 新增 config/server_seg.yaml 与 config/worker_seg.yaml
├── 新增 POST /api/v1/segment/image/async
├── 新增 SEG registry / ready / metrics 验证脚本
├── 结果图优先返回 overlay，不建议第一版把完整 mask 矩阵直接塞进 JSON
└── 每次接入后必须跑 Phase 17.5 全量回归，确认 Detect / OBB / CLS / POSE / Video / Stream 均未被破坏
```

后续扩展方向：

- 多路 RTSP / 摄像头流并发管理
- WebSocket / SSE 实时推送
- 更完整的存储抽象与历史任务管理
- 多 GPU / 多模型 Worker 调度
- 批量推理 / micro-batching
- 对象存储或数据库持久化
- 结构化日志与服务守护脚本

---

## 致谢

Modified from `tensorrtx/yolo11`.
