import argparse
import json
import time
from typing import Any, Dict, Tuple

import requests

TERMINAL = {"stopped", "failed", "canceled"}


def pretty(obj: Any) -> str:
    return json.dumps(obj, ensure_ascii=False, indent=2)


def post_json(base: str, path: str, payload: Dict[str, Any] | None = None, timeout: int = 10) -> requests.Response:
    r = requests.post(f"{base}{path}", json=payload or {}, timeout=timeout)
    print(f"POST {base}{path} {r.status_code} {r.text[:2000]}")
    return r


def get_json(base: str, path: str, timeout: int = 10) -> Dict[str, Any]:
    r = requests.get(f"{base}{path}", timeout=timeout)
    print(f"GET {base}{path} {r.status_code} {r.text[:2000]}")
    r.raise_for_status()
    return r.json()


def metric_counts(m: Dict[str, Any]) -> Dict[str, int]:
    model = (m.get("models") or {}).get("stream") or {}
    done = int(m.get("total_tasks_done") or model.get("total_done") or 0)
    failed = int(m.get("total_tasks_failed") or model.get("total_failed") or 0)
    total = int(m.get("total_tasks") or model.get("total_tasks") or (done + failed))
    return {"done": done, "failed": failed, "total": total}


def safe_stop(base: str, stream_id: str) -> None:
    if not stream_id:
        return
    try:
        post_json(base, f"/api/v1/stream/{stream_id}/stop", timeout=10)
    except Exception as exc:
        print(f"safe_stop ignored error for {stream_id}: {exc}")


def wait_terminal(base: str, stream_id: str, wait_seconds: int) -> Dict[str, Any]:
    deadline = time.time() + wait_seconds
    last: Dict[str, Any] | None = None
    while time.time() < deadline:
        last = get_json(base, f"/api/v1/stream/{stream_id}/status")
        if str(last.get("status", "")).lower() in TERMINAL:
            return last
        time.sleep(1)
    raise RuntimeError(f"stream did not reach terminal status before timeout. last={pretty(last)}")


def wait_running_with_frames(base: str, stream_id: str, wait_seconds: int) -> Dict[str, Any]:
    deadline = time.time() + wait_seconds
    last: Dict[str, Any] | None = None
    while time.time() < deadline:
        last = get_json(base, f"/api/v1/stream/{stream_id}/status")
        status = str(last.get("status", "")).lower()
        frame_count = int(last.get("frame_count") or 0)

        # A stream can report running before the first frame/latest snapshot is written.
        # Do NOT fail immediately on running + frame_count=0; keep waiting.
        if status == "running" and frame_count > 0:
            return last

        if status in TERMINAL:
            raise RuntimeError(f"stream reached terminal status before producing frames: {pretty(last)}")

        time.sleep(1)

    raise RuntimeError(f"stream did not produce frames before timeout. last={pretty(last)}")


def run_valid_camera_round(base: str, camera_id: int, run_seconds: int, wait_seconds: int) -> Tuple[str, Dict[str, Any]]:
    stream_id = ""
    try:
        start = post_json(base, "/api/v1/stream/start", {"source_type": "camera", "camera_id": camera_id})
        start.raise_for_status()
        data = start.json()
        stream_id = data["stream_id"]
        status = data.get("status")
        if status not in {"queued", "created"}:
            raise RuntimeError(f"unexpected start status: {pretty(data)}")

        running = wait_running_with_frames(base, stream_id, wait_seconds)
        print("valid stream running with frames:", pretty(running))

        if run_seconds > 0:
            time.sleep(run_seconds)

        safe_stop(base, stream_id)
        stopped = wait_terminal(base, stream_id, wait_seconds)
        if stopped.get("status") != "stopped":
            raise RuntimeError(f"valid stream should stop cleanly, got: {pretty(stopped)}")
        return stream_id, stopped
    except Exception:
        safe_stop(base, stream_id)
        raise


def run_invalid_camera_round(base: str, camera_id: int, wait_seconds: int) -> Tuple[str, Dict[str, Any]]:
    stream_id = ""
    try:
        start = post_json(base, "/api/v1/stream/start", {"source_type": "camera", "camera_id": camera_id})
        start.raise_for_status()
        data = start.json()
        stream_id = data["stream_id"]
        terminal = wait_terminal(base, stream_id, wait_seconds)
        if terminal.get("status") != "failed":
            raise RuntimeError(f"invalid camera should fail, got: {pretty(terminal)}")
        print("invalid camera failed as expected:", pretty(terminal))
        return stream_id, terminal
    except Exception:
        safe_stop(base, stream_id)
        raise


def main() -> None:
    ap = argparse.ArgumentParser(description="Phase 14.5 stream metrics check")
    ap.add_argument("--url", default="http://127.0.0.1:8083")
    ap.add_argument("--camera-id", type=int, default=0)
    ap.add_argument("--invalid-camera-id", type=int, default=99)
    ap.add_argument("--run-seconds", type=int, default=3)
    ap.add_argument("--wait-seconds", type=int, default=30)
    args = ap.parse_args()

    base = args.url.rstrip("/")

    print("[0] health / ready / workers")
    health = get_json(base, "/api/v1/health")
    ready = get_json(base, "/api/v1/ready")
    workers = get_json(base, "/api/v1/workers")
    if not ready.get("ready"):
        raise RuntimeError(f"server is not ready: {pretty(ready)}")

    print("[1] baseline metrics")
    baseline_metrics = get_json(base, "/api/v1/metrics?model=stream")
    baseline = metric_counts(baseline_metrics)
    print("baseline:", baseline)

    print("[2] valid camera stream should increment done_count")
    valid_sid, stopped = run_valid_camera_round(base, args.camera_id, args.run_seconds, args.wait_seconds)

    after_done_metrics = get_json(base, "/api/v1/metrics?model=stream")
    after_done = metric_counts(after_done_metrics)
    print("after valid stream:", after_done)
    if after_done["done"] < baseline["done"] + 1:
        raise RuntimeError(f"done_count did not increment. baseline={baseline}, after={after_done}, metrics={pretty(after_done_metrics)}")

    print("[3] invalid camera stream should increment failed_count")
    invalid_sid, failed = run_invalid_camera_round(base, args.invalid_camera_id, args.wait_seconds)

    final_metrics = get_json(base, "/api/v1/metrics?model=stream")
    final_counts = metric_counts(final_metrics)
    print("final:", final_counts)
    if final_counts["failed"] < after_done["failed"] + 1:
        raise RuntimeError(f"failed_count did not increment. before={after_done}, final={final_counts}, metrics={pretty(final_metrics)}")

    final_workers = get_json(base, "/api/v1/workers")
    summary = {
        "valid_stream_id": valid_sid,
        "valid_terminal": stopped,
        "invalid_stream_id": invalid_sid,
        "invalid_terminal": failed,
        "baseline": baseline,
        "after_done": after_done,
        "final": final_counts,
        "health_phase": health.get("phase"),
        "workers": final_workers,
        "metrics": final_metrics,
    }
    print("\nPASS Phase 14.5 stream metrics check")
    print(pretty(summary))


if __name__ == "__main__":
    main()
