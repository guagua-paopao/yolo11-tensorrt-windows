import argparse
import json
import time
from pathlib import Path

import requests


def main():
    parser = argparse.ArgumentParser(description="Phase 14.0 stream lifecycle smoke test")
    parser.add_argument("--url", default="http://127.0.0.1:8083")
    parser.add_argument("--source-type", default="camera", choices=["camera", "file", "rtsp"])
    parser.add_argument("--camera-id", type=int, default=0)
    parser.add_argument("--file-path", default="")
    parser.add_argument("--rtsp-url", default="")
    parser.add_argument("--wait-seconds", type=int, default=10)
    parser.add_argument("--snapshot-out", default="phase14_stream_snapshot.jpg")
    args = parser.parse_args()

    payload = {"source_type": args.source_type}
    if args.source_type == "camera":
        payload["camera_id"] = args.camera_id
    elif args.source_type == "file":
        payload["file_path"] = args.file_path
    else:
        payload["rtsp_url"] = args.rtsp_url

    print("[1] start stream", payload)
    r = requests.post(f"{args.url}/api/v1/stream/start", json=payload, timeout=10)
    print(r.status_code, r.text[:1000])
    r.raise_for_status()
    data = r.json()
    stream_id = data["stream_id"]

    last_status = None
    deadline = time.time() + args.wait_seconds
    print("[2] poll status")
    while time.time() < deadline:
        s = requests.get(f"{args.url}/api/v1/stream/{stream_id}/status", timeout=10)
        print(s.status_code, s.text[:1000])
        s.raise_for_status()
        last_status = s.json()
        if last_status.get("frame_count", 0) > 0:
            break
        time.sleep(1)

    if not last_status or last_status.get("frame_count", 0) <= 0:
        raise RuntimeError("stream did not produce frames before timeout")

    print("[3] download snapshot")
    img = requests.get(f"{args.url}/api/v1/stream/{stream_id}/snapshot", timeout=10)
    print(img.status_code, img.headers.get("Content-Type"), len(img.content))
    img.raise_for_status()
    Path(args.snapshot_out).write_bytes(img.content)
    print("snapshot saved to", args.snapshot_out)

    print("[4] stop stream")
    stop = requests.post(f"{args.url}/api/v1/stream/{stream_id}/stop", timeout=10)
    print(stop.status_code, stop.text[:1000])
    stop.raise_for_status()

    print("[5] wait stopped")
    for _ in range(20):
        s = requests.get(f"{args.url}/api/v1/stream/{stream_id}/status", timeout=10)
        data = s.json()
        print(data.get("status"), "frames=", data.get("frame_count"))
        if data.get("status") in ("stopped", "failed"):
            print(json.dumps(data, indent=2, ensure_ascii=False))
            return
        time.sleep(1)
    raise RuntimeError("stream did not stop before timeout")


if __name__ == "__main__":
    main()
