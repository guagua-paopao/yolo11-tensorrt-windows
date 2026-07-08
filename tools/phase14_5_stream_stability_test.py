import argparse
import json
import time
from pathlib import Path

import requests


def post_json(url, path, payload, timeout=10):
    r = requests.post(f"{url}{path}", json=payload, timeout=timeout)
    print(path, r.status_code, r.text[:1000])
    return r


def get_json(url, path, timeout=10):
    r = requests.get(f"{url}{path}", timeout=timeout)
    print(path, r.status_code, r.text[:1000])
    r.raise_for_status()
    return r.json()


def wait_running(url, stream_id, timeout_s):
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        last = get_json(url, f"/api/v1/stream/{stream_id}/status")
        if last.get("status") == "running" and int(last.get("frame_count", 0)) > 0:
            return last
        if last.get("status") == "failed":
            raise RuntimeError(f"stream failed before running: {last}")
        time.sleep(1)
    raise RuntimeError(f"stream did not reach running with frames before timeout. last={last}")


def wait_terminal(url, stream_id, timeout_s):
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        last = get_json(url, f"/api/v1/stream/{stream_id}/status")
        if last.get("status") in ("stopped", "failed"):
            return last
        time.sleep(1)
    raise RuntimeError(f"stream did not reach terminal status before timeout. last={last}")


def stop_stream(url, stream_id):
    r = requests.post(f"{url}/api/v1/stream/{stream_id}/stop", timeout=10)
    print("stop", stream_id, r.status_code, r.text[:1000])
    r.raise_for_status()


def main():
    ap = argparse.ArgumentParser(description="Phase 14.5 stream stability test: repeated start/stop, duplicate guard, snapshot, invalid camera")
    ap.add_argument("--url", default="http://127.0.0.1:8083")
    ap.add_argument("--source-type", default="camera", choices=["camera", "file", "rtsp"])
    ap.add_argument("--camera-id", type=int, default=0)
    ap.add_argument("--file-path", default="")
    ap.add_argument("--rtsp-url", default="")
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--run-seconds", type=int, default=5)
    ap.add_argument("--wait-seconds", type=int, default=20)
    ap.add_argument("--snapshot-dir", default="phase14_5_snapshots")
    ap.add_argument("--bad-camera-id", type=int, default=99)
    args = ap.parse_args()

    url = args.url.rstrip("/")
    Path(args.snapshot_dir).mkdir(parents=True, exist_ok=True)

    health = get_json(url, "/api/v1/health")
    print("health phase=", health.get("phase"))
    ready = get_json(url, "/api/v1/ready")
    if not ready.get("ready"):
        raise RuntimeError(f"server not ready: {ready}")

    payload = {"source_type": args.source_type}
    if args.source_type == "camera":
        payload["camera_id"] = args.camera_id
    elif args.source_type == "file":
        payload["file_path"] = args.file_path
    else:
        payload["rtsp_url"] = args.rtsp_url

    results = []
    for i in range(args.repeat):
        print(f"\n=== round {i + 1}/{args.repeat}: start ===")
        start = post_json(url, "/api/v1/stream/start", payload)
        start.raise_for_status()
        data = start.json()
        stream_id = data["stream_id"]

        print("duplicate start should be rejected with 409")
        dup = post_json(url, "/api/v1/stream/start", payload)
        if dup.status_code != 409:
            raise RuntimeError(f"duplicate start was not rejected: status={dup.status_code}, body={dup.text}")

        running = wait_running(url, stream_id, args.wait_seconds)
        time.sleep(args.run_seconds)

        snap = requests.get(f"{url}/api/v1/stream/{stream_id}/snapshot", timeout=10)
        print("snapshot", snap.status_code, snap.headers.get("Content-Type"), len(snap.content))
        snap.raise_for_status()
        snap_path = Path(args.snapshot_dir) / f"{stream_id}.jpg"
        snap_path.write_bytes(snap.content)

        stop_stream(url, stream_id)
        terminal = wait_terminal(url, stream_id, args.wait_seconds)
        if terminal.get("status") != "stopped":
            raise RuntimeError(f"expected stopped, got {terminal}")
        results.append({"stream_id": stream_id, "frame_count": terminal.get("frame_count"), "snapshot": str(snap_path)})

    print("\n=== invalid camera test ===")
    bad_payload = {"source_type": "camera", "camera_id": args.bad_camera_id}
    bad_start = post_json(url, "/api/v1/stream/start", bad_payload)
    if bad_start.status_code == 503:
        print("bad camera rejected because worker unavailable; acceptable for offline-protection path")
    else:
        bad_start.raise_for_status()
        bad_id = bad_start.json()["stream_id"]
        bad_terminal = wait_terminal(url, bad_id, args.wait_seconds)
        if bad_terminal.get("status") != "failed":
            stop_stream(url, bad_id)
            raise RuntimeError(f"bad camera should fail, got {bad_terminal}")
        print("bad camera failed as expected", json.dumps(bad_terminal, indent=2, ensure_ascii=False))

    workers = get_json(url, "/api/v1/workers")
    print("\nPASS Phase 14.5 stability test")
    print(json.dumps({"rounds": results, "workers": workers}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
