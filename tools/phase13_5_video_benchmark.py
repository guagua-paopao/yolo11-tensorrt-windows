import argparse
import concurrent.futures
import json
import os
import statistics
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
    except Exception as e:
        return 0, {"success": False, "error": str(e)}


def submit_one(url, video_bytes):
    code, body = request_json(
        "POST",
        f"{url}/api/v1/detect/video/async",
        data=video_bytes,
        headers={"Content-Type": "video/mp4"},
        timeout=120,
    )
    return code, body


def percentile(values, p):
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = int(round((len(ordered) - 1) * p))
    return float(ordered[max(0, min(idx, len(ordered) - 1))])


def main():
    parser = argparse.ArgumentParser(description="Phase 13.5 video async benchmark")
    parser.add_argument("--url", default="http://127.0.0.1:8082")
    parser.add_argument("--video", required=True)
    parser.add_argument("--tasks", type=int, default=5)
    parser.add_argument("--concurrency", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--poll-interval", type=float, default=0.5)
    parser.add_argument("--output-json", default="phase13_5_video_benchmark_result.json")
    args = parser.parse_args()

    if not os.path.exists(args.video):
        print(f"video not found: {args.video}")
        return 2
    with open(args.video, "rb") as f:
        video_bytes = f.read()

    code, ready = request_json("GET", f"{args.url}/api/v1/ready", timeout=30)
    print("ready", code, json.dumps(ready, ensure_ascii=False)[:1000])
    if code != 200 or not ready.get("ready"):
        print("server not ready")
        return 3

    t0 = time.time()
    task_ids = []
    submit_errors = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.concurrency)) as pool:
        futures = [pool.submit(submit_one, args.url, video_bytes) for _ in range(args.tasks)]
        for fut in concurrent.futures.as_completed(futures):
            code, body = fut.result()
            if code == 202 and body.get("task_id"):
                task_ids.append(body["task_id"])
            else:
                submit_errors.append({"code": code, "body": body})
            print(f"submitted={len(task_ids)} submit_errors={len(submit_errors)}")

    deadline = time.time() + args.timeout
    final = {}
    while time.time() < deadline and len(final) < len(task_ids):
        for task_id in list(task_ids):
            if task_id in final:
                continue
            code, body = request_json("GET", f"{args.url}/api/v1/video/result/{task_id}", timeout=30)
            status = body.get("status")
            if status in ("done", "failed", "canceled"):
                final[task_id] = body
        print(f"poll final={len(final)}/{len(task_ids)}")
        if len(final) >= len(task_ids):
            break
        time.sleep(args.poll_interval)

    t1 = time.time()
    done = [v for v in final.values() if v.get("status") == "done"]
    failed = [v for v in final.values() if v.get("status") == "failed"]
    canceled = [v for v in final.values() if v.get("status") == "canceled"]
    timeout_count = len(task_ids) - len(final)
    total_ms = [float(v.get("total_ms", 0) or 0) for v in done]
    process_ms = [float(v.get("process_ms", 0) or 0) for v in done]
    queue_ms = [float(v.get("queue_wait_ms", 0) or 0) for v in done]
    summary = {
        "tasks_requested": args.tasks,
        "tasks_submitted": len(task_ids),
        "submit_errors": len(submit_errors),
        "done": len(done),
        "failed": len(failed),
        "canceled": len(canceled),
        "timeout": timeout_count,
        "wall_seconds": t1 - t0,
        "qps_done": len(done) / (t1 - t0) if t1 > t0 else 0.0,
        "latency_ms": {
            "avg_total": statistics.mean(total_ms) if total_ms else 0.0,
            "p50_total": percentile(total_ms, 0.50),
            "p95_total": percentile(total_ms, 0.95),
            "avg_process": statistics.mean(process_ms) if process_ms else 0.0,
            "avg_queue": statistics.mean(queue_ms) if queue_ms else 0.0,
        },
        "submit_error_samples": submit_errors[:3],
        "final": final,
    }
    with open(args.output_json, "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)
    print(json.dumps({k: v for k, v in summary.items() if k != "final"}, ensure_ascii=False, indent=2))
    print(f"saved: {args.output_json}")
    return 0 if len(done) == len(task_ids) and not submit_errors else 1


if __name__ == "__main__":
    sys.exit(main())
