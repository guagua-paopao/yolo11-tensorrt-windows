import argparse
import json
import os
import sys
import time
import urllib.request
import urllib.error


def request_json(method, url, data=None, headers=None, timeout=30):
    req = urllib.request.Request(url, data=data, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            return resp.status, json.loads(body)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(body)
        except Exception:
            parsed = {"raw": body}
        return e.code, parsed


def main():
    parser = argparse.ArgumentParser(description="Phase 13 video async smoke test")
    parser.add_argument("--url", default="http://127.0.0.1:8082")
    parser.add_argument("--video", required=True)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--download", default="phase13_result.mp4")
    args = parser.parse_args()

    if not os.path.exists(args.video):
        print(f"video not found: {args.video}")
        return 2

    code, health = request_json("GET", f"{args.url}/api/v1/health")
    print("health", code, json.dumps(health, ensure_ascii=False)[:1000])

    code, ready = request_json("GET", f"{args.url}/api/v1/ready")
    print("ready", code, json.dumps(ready, ensure_ascii=False)[:1000])
    if code != 200:
        print("server is not ready; start yolo11_video_worker first")
        return 3

    with open(args.video, "rb") as f:
        video_bytes = f.read()

    code, submit = request_json(
        "POST",
        f"{args.url}/api/v1/detect/video/async",
        data=video_bytes,
        headers={"Content-Type": "video/mp4"},
        timeout=120,
    )
    print("submit", code, json.dumps(submit, ensure_ascii=False)[:1000])
    if code != 202:
        return 4

    task_id = submit["task_id"]
    deadline = time.time() + args.timeout
    last = None
    while time.time() < deadline:
        code, result = request_json("GET", f"{args.url}/api/v1/video/result/{task_id}", timeout=30)
        last = result
        status = result.get("status")
        video = result.get("video", {})
        print(f"poll code={code} status={status} progress={video.get('progress')} frames={video.get('processed_frames')}/{video.get('total_frames')}")
        if status in ("done", "failed", "canceled"):
            break
        time.sleep(1)

    if not last or last.get("status") != "done":
        print("not done:", json.dumps(last, ensure_ascii=False, indent=2))
        return 5

    file_url = f"{args.url}/api/v1/video/result/{task_id}/file"
    with urllib.request.urlopen(file_url, timeout=120) as resp:
        data = resp.read()
    with open(args.download, "wb") as f:
        f.write(data)
    print(f"downloaded result video: {args.download}, bytes={len(data)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
