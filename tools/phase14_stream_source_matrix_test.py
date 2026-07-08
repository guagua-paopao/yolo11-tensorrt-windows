import argparse
import json
import time
from pathlib import Path

import requests

TERMINAL = {"stopped", "failed", "canceled"}


def post_json(url, path, payload=None, timeout=10):
    r = requests.post(f"{url}{path}", json=payload or {}, timeout=timeout)
    print(path, r.status_code, r.text[:1000])
    return r


def get_json(url, path, timeout=10):
    r = requests.get(f"{url}{path}", timeout=timeout)
    print(path, r.status_code, r.text[:1000])
    r.raise_for_status()
    return r.json()


def wait_frames(url, stream_id, wait_seconds):
    deadline = time.time() + wait_seconds
    last = None
    while time.time() < deadline:
        last = get_json(url, f"/api/v1/stream/{stream_id}/status")
        status = last.get("status")
        if status in TERMINAL and int(last.get("frame_count", 0)) <= 0:
            raise RuntimeError(f"stream reached terminal status before producing frames: {last}")
        if int(last.get("frame_count", 0)) > 0:
            return last
        time.sleep(1)
    raise RuntimeError(f"stream did not produce frames before timeout. last={last}")


def wait_terminal(url, stream_id, wait_seconds):
    deadline = time.time() + wait_seconds
    last = None
    while time.time() < deadline:
        last = get_json(url, f"/api/v1/stream/{stream_id}/status")
        if last.get("status") in TERMINAL:
            return last
        time.sleep(1)
    raise RuntimeError(f"stream did not stop before timeout. last={last}")


def run_case(url, name, payload, snapshot_dir, wait_seconds, run_seconds):
    print(f"\n===== CASE {name}: start =====")
    start = post_json(url, "/api/v1/stream/start", payload)
    start.raise_for_status()
    data = start.json()
    stream_id = data["stream_id"]
    assert data.get("status") == "queued", f"/stream/start status should be queued, got {data.get('status')}"

    first = wait_frames(url, stream_id, wait_seconds)
    print("first frame status:", json.dumps(first, ensure_ascii=False, indent=2))
    if run_seconds > 0:
        time.sleep(run_seconds)

    print("download snapshot")
    snap = requests.get(f"{url}/api/v1/stream/{stream_id}/snapshot", timeout=10)
    print("snapshot", snap.status_code, snap.headers.get("Content-Type"), len(snap.content))
    snap.raise_for_status()
    out = snapshot_dir / f"{name}_{stream_id}.jpg"
    out.write_bytes(snap.content)

    print("stop stream")
    stop = post_json(url, f"/api/v1/stream/{stream_id}/stop")
    if stop.status_code not in (200, 409):
        stop.raise_for_status()
    terminal = wait_terminal(url, stream_id, wait_seconds)
    if terminal.get("status") != "stopped":
        raise RuntimeError(f"expected stopped for {name}, got {terminal}")

    return {
        "case": name,
        "stream_id": stream_id,
        "status": terminal.get("status"),
        "frame_count": terminal.get("frame_count"),
        "snapshot": str(out),
    }


def main():
    ap = argparse.ArgumentParser(description="Phase 14 source matrix test: file / camera / rtsp stream lifecycle")
    ap.add_argument("--url", default="http://127.0.0.1:8083")
    ap.add_argument("--include", default="file,camera", help="Comma-separated cases: file,camera,rtsp")
    ap.add_argument("--file-path", default="")
    ap.add_argument("--camera-id", type=int, default=0)
    ap.add_argument("--rtsp-url", default="")
    ap.add_argument("--wait-seconds", type=int, default=20)
    ap.add_argument("--run-seconds", type=int, default=5)
    ap.add_argument("--snapshot-dir", default="runtime/output/streams/source_matrix")
    args = ap.parse_args()

    url = args.url.rstrip("/")
    snapshot_dir = Path(args.snapshot_dir)
    snapshot_dir.mkdir(parents=True, exist_ok=True)

    print("health / ready / workers before source matrix")
    health = get_json(url, "/api/v1/health")
    ready = get_json(url, "/api/v1/ready")
    workers = get_json(url, "/api/v1/workers")
    if not ready.get("ready"):
        raise RuntimeError(f"server is not ready: {ready}")

    cases = []
    requested = {x.strip().lower() for x in args.include.split(",") if x.strip()}
    if "file" in requested:
        if not args.file_path:
            raise ValueError("--file-path is required when include contains file")
        cases.append(("file", {"source_type": "file", "file_path": args.file_path}))
    if "camera" in requested:
        cases.append(("camera", {"source_type": "camera", "camera_id": args.camera_id}))
    if "rtsp" in requested:
        if not args.rtsp_url:
            raise ValueError("--rtsp-url is required when include contains rtsp")
        cases.append(("rtsp", {"source_type": "rtsp", "rtsp_url": args.rtsp_url}))

    results = []
    for name, payload in cases:
        results.append(run_case(url, name, payload, snapshot_dir, args.wait_seconds, args.run_seconds))

    metrics = get_json(url, "/api/v1/metrics")
    summary = {"results": results, "health_phase": health.get("phase"), "workers": workers, "metrics": metrics}
    print("\nPASS Phase 14 source matrix")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
