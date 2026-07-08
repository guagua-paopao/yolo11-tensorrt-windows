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
    parser = argparse.ArgumentParser(description="Phase 13.5 video worker recovery helper")
    parser.add_argument("--url", default="http://127.0.0.1:8082")
    parser.add_argument("--video", required=True)
    parser.add_argument("--tasks", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()

    if not os.path.exists(args.video):
        print(f"video not found: {args.video}")
        return 2
    with open(args.video, "rb") as f:
        video_bytes = f.read()

    task_ids = []
    for i in range(args.tasks):
        code, body = request_json("POST", f"{args.url}/api/v1/detect/video/async", data=video_bytes, headers={"Content-Type": "video/mp4"}, timeout=120)
        print("submit", i + 1, code, json.dumps(body, ensure_ascii=False)[:800])
        if code == 202:
            task_ids.append(body["task_id"])

    if not task_ids:
        print("no tasks submitted")
        return 3

    print("\nNow stop/kill yolo11_video_worker.exe in another PowerShell, wait a few seconds, then restart it.")
    print("Example:")
    print("  Get-Process yolo11_video_worker | Stop-Process -Force")
    print("  .\\out\\build\\phase13-video-debug\\yolo11_video_worker.exe .\\config\\worker_video_phase13_5.yaml --consumer-name video_worker_1")
    input("Press Enter after you have restarted the worker...")

    deadline = time.time() + args.timeout
    final = {}
    while time.time() < deadline and len(final) < len(task_ids):
        for task_id in task_ids:
            if task_id in final:
                continue
            code, body = request_json("GET", f"{args.url}/api/v1/video/result/{task_id}", timeout=30)
            status = body.get("status")
            video = body.get("video", {})
            print(f"{task_id}: code={code} status={status} progress={video.get('progress')} frames={video.get('processed_frames')}/{video.get('total_frames')}")
            if status in ("done", "failed", "canceled"):
                final[task_id] = body
        if len(final) >= len(task_ids):
            break
        time.sleep(1)

    done = sum(1 for v in final.values() if v.get("status") == "done")
    failed = sum(1 for v in final.values() if v.get("status") == "failed")
    canceled = sum(1 for v in final.values() if v.get("status") == "canceled")
    timeout = len(task_ids) - len(final)
    print(json.dumps({"submitted": len(task_ids), "done": done, "failed": failed, "canceled": canceled, "timeout": timeout}, ensure_ascii=False, indent=2))
    return 0 if done == len(task_ids) else 1


if __name__ == "__main__":
    sys.exit(main())
