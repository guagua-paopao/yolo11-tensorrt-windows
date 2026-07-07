#!/usr/bin/env python3
"""
Phase 5 async benchmark for YOLO11 TensorRT C++ server.

No third-party dependency is required.

Example:
  python tools/benchmark_async.py ^
    --url http://127.0.0.1:8080 ^
    --image D:/tensorrtx/yolo11/images/bus.png ^
    --tasks 1000 ^
    --concurrency 10 ^
    --timeout 120
"""

from __future__ import annotations

import argparse
import concurrent.futures as futures
import json
import mimetypes
import statistics
import sys
import time
import urllib.error
import urllib.request
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional


@dataclass
class TaskRecord:
    index: int
    task_id: str = ""
    submit_ok: bool = False
    submit_error: str = ""
    submit_ms: float = 0.0
    submit_time: float = 0.0
    done_time: float = 0.0
    final_status: str = ""
    final_error: str = ""
    queue_wait_ms: Optional[float] = None
    inference_ms: Optional[float] = None
    total_ms: Optional[float] = None
    worker_id: str = ""
    consumer_name: str = ""
    num_detections: Optional[int] = None


def percentile(values: List[float], pct: float) -> Optional[float]:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * pct / 100.0
    lo = int(pos)
    hi = min(lo + 1, len(ordered) - 1)
    frac = pos - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def http_json(method: str, url: str, body: Optional[bytes] = None, content_type: str = "application/json", timeout: float = 10.0) -> Dict[str, Any]:
    headers = {}
    if body is not None:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url=url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", errors="replace")
        try:
            data = json.loads(raw)
        except json.JSONDecodeError:
            data = {"success": False, "error": raw}
        data["http_status"] = e.code
        return data


def submit_one(index: int, base_url: str, endpoint: str, image_bytes: bytes, content_type: str, timeout: float) -> TaskRecord:
    rec = TaskRecord(index=index)
    rec.submit_time = time.time()
    t0 = time.perf_counter()
    try:
        data = http_json(
            "POST",
            base_url.rstrip("/") + endpoint,
            body=image_bytes,
            content_type=content_type,
            timeout=timeout,
        )
        rec.submit_ms = (time.perf_counter() - t0) * 1000.0
        if data.get("success") and data.get("task_id"):
            rec.submit_ok = True
            rec.task_id = str(data["task_id"])
        else:
            rec.submit_error = str(data.get("error") or data)
    except Exception as e:  # noqa: BLE001
        rec.submit_ms = (time.perf_counter() - t0) * 1000.0
        rec.submit_error = repr(e)
    return rec


def query_result(base_url: str, task_id: str, timeout: float) -> Dict[str, Any]:
    return http_json("GET", base_url.rstrip("/") + f"/api/v1/result/{task_id}", timeout=timeout)


def poll_until_finished(records: List[TaskRecord], base_url: str, timeout_s: float, poll_interval_s: float, request_timeout_s: float) -> None:
    pending = {r.task_id: r for r in records if r.submit_ok and r.task_id}
    deadline = time.time() + timeout_s

    while pending and time.time() < deadline:
        for task_id in list(pending.keys()):
            rec = pending[task_id]
            try:
                data = query_result(base_url, task_id, request_timeout_s)
            except Exception as e:  # noqa: BLE001
                rec.final_error = repr(e)
                continue

            status = str(data.get("status", ""))
            if status in {"done", "failed"}:
                rec.done_time = time.time()
                rec.final_status = status
                rec.final_error = str(data.get("error", ""))
                rec.queue_wait_ms = to_float_or_none(data.get("queue_wait_ms"))
                rec.inference_ms = to_float_or_none(data.get("inference_ms"))
                rec.total_ms = to_float_or_none(data.get("total_ms"))
                rec.worker_id = str(data.get("worker_id", ""))
                rec.consumer_name = str(data.get("consumer_name", ""))
                if "num_detections" in data:
                    try:
                        rec.num_detections = int(data["num_detections"])
                    except Exception:  # noqa: BLE001
                        pass
                pending.pop(task_id, None)
        if pending:
            time.sleep(poll_interval_s)

    for rec in pending.values():
        rec.final_status = "timeout"
        rec.final_error = f"not finished in {timeout_s}s"


def to_float_or_none(value: Any) -> Optional[float]:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except Exception:  # noqa: BLE001
        return None


def fmt(value: Optional[float]) -> str:
    if value is None:
        return "n/a"
    return f"{value:.3f}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8080", help="Server base URL")
    parser.add_argument("--image", required=True, help="Image path")
    parser.add_argument("--endpoint", default="/api/v1/detect/image/async", help="Async submit endpoint, for example /api/v1/detect/obb/async")
    parser.add_argument("--tasks", type=int, default=100, help="Number of async tasks")
    parser.add_argument("--concurrency", type=int, default=5, help="Concurrent submissions")
    parser.add_argument("--timeout", type=float, default=120.0, help="Total polling timeout in seconds")
    parser.add_argument("--poll-interval", type=float, default=0.2, help="Polling interval in seconds")
    parser.add_argument("--request-timeout", type=float, default=10.0, help="HTTP request timeout in seconds")
    args = parser.parse_args()

    image_path = Path(args.image)
    if not image_path.exists():
        print(f"Image not found: {image_path}", file=sys.stderr)
        return 2

    image_bytes = image_path.read_bytes()
    content_type = mimetypes.guess_type(str(image_path))[0] or "application/octet-stream"

    print(f"Server:      {args.url}")
    print(f"Image:       {image_path} ({len(image_bytes)} bytes, {content_type})")
    print(f"Endpoint:    {args.endpoint}")
    print(f"Tasks:       {args.tasks}")
    print(f"Concurrency: {args.concurrency}")

    start = time.perf_counter()
    records: List[TaskRecord] = []

    with futures.ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        futs = [
            executor.submit(submit_one, i, args.url, args.endpoint, image_bytes, content_type, args.request_timeout)
            for i in range(args.tasks)
        ]
        for fut in futures.as_completed(futs):
            rec = fut.result()
            records.append(rec)
            if not rec.submit_ok:
                print(f"submit failed index={rec.index}: {rec.submit_error}")

    submit_end = time.perf_counter()
    records.sort(key=lambda r: r.index)
    ok_records = [r for r in records if r.submit_ok]
    print(f"Submitted ok: {len(ok_records)}/{len(records)} in {submit_end - start:.3f}s")

    poll_until_finished(ok_records, args.url, args.timeout, args.poll_interval, args.request_timeout)
    end = time.perf_counter()

    status_counter = Counter(r.final_status for r in ok_records)
    worker_counter = Counter(r.consumer_name or r.worker_id or "unknown" for r in ok_records if r.final_status == "done")

    done_records = [r for r in ok_records if r.final_status == "done"]
    failed_records = [r for r in ok_records if r.final_status == "failed"]
    timeout_records = [r for r in ok_records if r.final_status == "timeout"]

    total_ms_values = [r.total_ms for r in done_records if r.total_ms is not None]
    infer_ms_values = [r.inference_ms for r in done_records if r.inference_ms is not None]
    queue_ms_values = [r.queue_wait_ms for r in done_records if r.queue_wait_ms is not None]
    client_latency_ms = [(r.done_time - r.submit_time) * 1000.0 for r in done_records if r.done_time > 0 and r.submit_time > 0]

    elapsed = end - start
    qps = len(done_records) / elapsed if elapsed > 0 else 0.0

    print("\n========== Benchmark Summary ==========")
    print(f"Elapsed seconds:     {elapsed:.3f}")
    print(f"Submit seconds:      {submit_end - start:.3f}")
    print(f"Done/Failed/Timeout: {len(done_records)}/{len(failed_records)}/{len(timeout_records)}")
    print(f"Final status counts: {dict(status_counter)}")
    print(f"Throughput QPS:      {qps:.3f}")
    print(f"Worker distribution: {dict(worker_counter)}")

    print("\n--- total_ms from server ---")
    print(f"avg={fmt(statistics.mean(total_ms_values) if total_ms_values else None)}  p50={fmt(percentile(total_ms_values, 50))}  p95={fmt(percentile(total_ms_values, 95))}  p99={fmt(percentile(total_ms_values, 99))}")

    print("\n--- queue_wait_ms from server ---")
    print(f"avg={fmt(statistics.mean(queue_ms_values) if queue_ms_values else None)}  p50={fmt(percentile(queue_ms_values, 50))}  p95={fmt(percentile(queue_ms_values, 95))}  p99={fmt(percentile(queue_ms_values, 99))}")

    print("\n--- inference_ms from server ---")
    print(f"avg={fmt(statistics.mean(infer_ms_values) if infer_ms_values else None)}  p50={fmt(percentile(infer_ms_values, 50))}  p95={fmt(percentile(infer_ms_values, 95))}  p99={fmt(percentile(infer_ms_values, 99))}")

    print("\n--- client observed latency ---")
    print(f"avg={fmt(statistics.mean(client_latency_ms) if client_latency_ms else None)}  p50={fmt(percentile(client_latency_ms, 50))}  p95={fmt(percentile(client_latency_ms, 95))}  p99={fmt(percentile(client_latency_ms, 99))}")

    if failed_records:
        print("\nFailed examples:")
        for rec in failed_records[:5]:
            print(f"  task_id={rec.task_id} error={rec.final_error}")

    if timeout_records:
        print("\nTimeout examples:")
        for rec in timeout_records[:5]:
            print(f"  task_id={rec.task_id}")

    return 0 if len(done_records) == args.tasks else 1


if __name__ == "__main__":
    raise SystemExit(main())
