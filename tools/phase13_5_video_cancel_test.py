import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request


def request_json(method, url, data=None, headers=None, timeout=30):
    req = urllib.request.Request(url, data=data, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            return resp.status, json.loads(body)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        try:
            return e.code, json.loads(body)
        except Exception:
            return e.code, {"raw": body}


def main():
    parser = argparse.ArgumentParser(description="Phase 13.5 video cancel test")
    parser.add_argument("--url", default="http://127.0.0.1:8082")
    parser.add_argument("--video", required=True)
    parser.add_argument("--cancel-delay", type=float, default=0.2, help="Seconds after submit before sending cancel")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--allow-fast-done", action="store_true", help="Treat done as PASS when the video finishes before cancel takes effect")
    args = parser.parse_args()

    if not os.path.exists(args.video):
        print(f"video not found: {args.video}")
        return 2

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
        return 3
    task_id = submit["task_id"]

    time.sleep(max(0.0, args.cancel_delay))
    code, cancel = request_json("POST", f"{args.url}/api/v1/video/result/{task_id}/cancel", timeout=30)
    print("cancel", code, json.dumps(cancel, ensure_ascii=False)[:1000])

    deadline = time.time() + args.timeout
    last = None
    while time.time() < deadline:
        code, result = request_json("GET", f"{args.url}/api/v1/video/result/{task_id}", timeout=30)
        last = result
        status = result.get("status")
        video = result.get("video", {})
        print(f"poll code={code} status={status} cancel_requested={result.get('cancel_requested')} progress={video.get('progress')} frames={video.get('processed_frames')}/{video.get('total_frames')}")
        if status in ("canceled", "done", "failed"):
            break
        time.sleep(0.5)

    if not last:
        print("FAIL: no final response")
        return 4
    final_status = last.get("status")
    if final_status == "canceled":
        print("PASS: task canceled")
        return 0
    if final_status == "done" and args.allow_fast_done:
        print("PASS: task completed before cancellation took effect")
        return 0
    print("FAIL: expected canceled", json.dumps(last, ensure_ascii=False, indent=2)[:2000])
    return 5


if __name__ == "__main__":
    sys.exit(main())
