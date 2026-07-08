#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Phase 11.0 OBB Stability Suite

Standard-library only test tool for the YOLO11 TensorRT async service.
It validates the Phase 10.5 OBB async path under a controlled batch load:
HTTP submit -> Redis Stream -> OBB worker -> result JSON -> result image -> metrics/workers/ready.
"""
from __future__ import annotations

import argparse
import csv
import json
import mimetypes
import os
import statistics
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


def now_ms() -> int:
    return int(time.time() * 1000)


def percentile(values: List[float], p: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    if len(values) == 1:
        return values[0]
    k = (len(values) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(values) - 1)
    if f == c:
        return values[f]
    return values[f] + (values[c] - values[f]) * (k - f)


def load_json_bytes(data: bytes) -> Dict[str, Any]:
    if not data:
        return {}
    try:
        return json.loads(data.decode("utf-8", errors="replace"))
    except json.JSONDecodeError:
        return {"_raw": data.decode("utf-8", errors="replace")}


def http_json(method: str, url: str, body: Optional[bytes] = None,
              headers: Optional[Dict[str, str]] = None,
              timeout: float = 30.0) -> Tuple[int, Dict[str, Any], float]:
    started = time.perf_counter()
    req = urllib.request.Request(url=url, data=body, method=method.upper(), headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            payload = resp.read()
            return resp.status, load_json_bytes(payload), (time.perf_counter() - started) * 1000.0
    except urllib.error.HTTPError as exc:
        payload = exc.read()
        return exc.code, load_json_bytes(payload), (time.perf_counter() - started) * 1000.0
    except Exception as exc:  # noqa: BLE001 - intentionally reported as structured output
        return 0, {"success": False, "error": str(exc), "error_type": exc.__class__.__name__}, (time.perf_counter() - started) * 1000.0


def http_download(url: str, output_path: Path, timeout: float = 30.0) -> Tuple[bool, int, str]:
    req = urllib.request.Request(url=url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            payload = resp.read()
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_bytes(payload)
            return True, len(payload), ""
    except Exception as exc:  # noqa: BLE001
        return False, 0, str(exc)


def build_url(base_url: str, path: str) -> str:
    return base_url.rstrip("/") + "/" + path.lstrip("/")


def submit_task(base_url: str, endpoint: str, image_bytes: bytes, content_type: str,
                request_timeout: float) -> Dict[str, Any]:
    url = build_url(base_url, endpoint)
    status_code, payload, latency_ms = http_json(
        "POST",
        url,
        body=image_bytes,
        headers={"Content-Type": content_type},
        timeout=request_timeout,
    )
    payload["_http_status"] = status_code
    payload["_submit_latency_ms"] = latency_ms
    return payload


def get_result(base_url: str, task_id: str, request_timeout: float) -> Dict[str, Any]:
    url = build_url(base_url, f"/api/v1/result/{urllib.parse.quote(task_id)}")
    status_code, payload, latency_ms = http_json("GET", url, timeout=request_timeout)
    payload["_http_status"] = status_code
    payload["_poll_latency_ms"] = latency_ms
    return payload


def query_endpoint(base_url: str, path: str, request_timeout: float) -> Dict[str, Any]:
    status_code, payload, latency_ms = http_json("GET", build_url(base_url, path), timeout=request_timeout)
    payload["_http_status"] = status_code
    payload["_latency_ms"] = latency_ms
    return payload


def redis_xpending(redis_cli: str, host: str, port: int, stream: str, group: str) -> Dict[str, Any]:
    cmd = [redis_cli, "-h", host, "-p", str(port), "XPENDING", stream, group]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=10, check=False)
        text = (proc.stdout or "") + (proc.stderr or "")
        pending: Optional[int] = None
        # redis-cli default formats first line like: 1) (integer) 0
        for token in text.replace("\r", "\n").replace(")", " ").replace("(", " ").split():
            if token.isdigit():
                pending = int(token)
                break
        return {
            "available": proc.returncode == 0,
            "returncode": proc.returncode,
            "pending": pending,
            "raw": text.strip(),
            "cmd": " ".join(cmd),
        }
    except FileNotFoundError:
        return {"available": False, "pending": None, "raw": f"redis-cli not found: {redis_cli}", "cmd": " ".join(cmd)}
    except Exception as exc:  # noqa: BLE001
        return {"available": False, "pending": None, "raw": str(exc), "cmd": " ".join(cmd)}


def wait_all(base_url: str, task_ids: List[str], timeout_s: float, poll_interval_s: float,
             request_timeout: float) -> Dict[str, Dict[str, Any]]:
    deadline = time.time() + timeout_s
    pending = set(task_ids)
    results: Dict[str, Dict[str, Any]] = {}

    while pending and time.time() < deadline:
        for task_id in list(pending):
            payload = get_result(base_url, task_id, request_timeout=request_timeout)
            status = str(payload.get("status", "")).lower()
            if status in {"done", "failed"}:
                payload["_final_status"] = status
                results[task_id] = payload
                pending.remove(task_id)
            elif payload.get("success") is False and payload.get("error_code") not in {"TASK_NOT_FOUND", None}:
                payload["_final_status"] = "failed"
                results[task_id] = payload
                pending.remove(task_id)
        if pending:
            time.sleep(poll_interval_s)

    for task_id in pending:
        results[task_id] = {
            "task_id": task_id,
            "status": "timeout",
            "_final_status": "timeout",
            "success": False,
            "error": f"Timeout after {timeout_s:.1f}s",
        }
    return results


def extract_number(payload: Dict[str, Any], key: str) -> Optional[float]:
    value = payload.get(key)
    if isinstance(value, (int, float)):
        return float(value)
    return None


def summarize(results: Dict[str, Dict[str, Any]], elapsed_s: float) -> Dict[str, Any]:
    status_counts: Dict[str, int] = {}
    worker_distribution: Dict[str, int] = {}
    total_ms: List[float] = []
    queue_wait_ms: List[float] = []
    inference_ms: List[float] = []

    for payload in results.values():
        status = str(payload.get("_final_status") or payload.get("status") or "unknown").lower()
        status_counts[status] = status_counts.get(status, 0) + 1
        worker = payload.get("consumer_name") or payload.get("worker_name") or payload.get("worker")
        if isinstance(worker, str) and worker:
            worker_distribution[worker] = worker_distribution.get(worker, 0) + 1
        for key, bucket in (("total_ms", total_ms), ("queue_wait_ms", queue_wait_ms), ("inference_ms", inference_ms)):
            number = extract_number(payload, key)
            if number is not None:
                bucket.append(number)

    def stat(values: List[float]) -> Dict[str, float]:
        return {
            "avg": statistics.mean(values) if values else 0.0,
            "p50": percentile(values, 50),
            "p95": percentile(values, 95),
            "p99": percentile(values, 99),
            "min": min(values) if values else 0.0,
            "max": max(values) if values else 0.0,
        }

    done = status_counts.get("done", 0)
    return {
        "elapsed_seconds": elapsed_s,
        "throughput_qps": done / elapsed_s if elapsed_s > 0 else 0.0,
        "status_counts": status_counts,
        "worker_distribution": worker_distribution,
        "total_ms": stat(total_ms),
        "queue_wait_ms": stat(queue_wait_ms),
        "inference_ms": stat(inference_ms),
    }


def write_reports(report_dir: Path, prefix: str, submit_payloads: List[Dict[str, Any]],
                  results: Dict[str, Dict[str, Any]], report: Dict[str, Any]) -> Dict[str, str]:
    report_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    json_path = report_dir / f"{prefix}_{stamp}.json"
    csv_path = report_dir / f"{prefix}_{stamp}_tasks.csv"
    txt_path = report_dir / f"{prefix}_{stamp}_summary.txt"

    json_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    fields = [
        "task_id", "submit_success", "submit_http_status", "submit_latency_ms",
        "final_status", "result_success", "worker", "queue_wait_ms", "inference_ms",
        "total_ms", "num_detections", "error", "result_image_url",
    ]
    submit_by_id = {p.get("task_id"): p for p in submit_payloads if p.get("task_id")}
    with csv_path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for task_id, payload in results.items():
            submit_payload = submit_by_id.get(task_id, {})
            writer.writerow({
                "task_id": task_id,
                "submit_success": submit_payload.get("success"),
                "submit_http_status": submit_payload.get("_http_status"),
                "submit_latency_ms": submit_payload.get("_submit_latency_ms"),
                "final_status": payload.get("_final_status") or payload.get("status"),
                "result_success": payload.get("success"),
                "worker": payload.get("consumer_name") or payload.get("worker_name") or payload.get("worker"),
                "queue_wait_ms": payload.get("queue_wait_ms"),
                "inference_ms": payload.get("inference_ms"),
                "total_ms": payload.get("total_ms"),
                "num_detections": payload.get("num_detections"),
                "error": payload.get("error") or payload.get("last_error"),
                "result_image_url": payload.get("result_image_url"),
            })

    summary = report.get("summary", {})
    lines = [
        "Phase 11.0 OBB Stability Suite Summary",
        "=======================================",
        f"elapsed_seconds: {summary.get('elapsed_seconds')}",
        f"throughput_qps:  {summary.get('throughput_qps')}",
        f"status_counts:   {summary.get('status_counts')}",
        f"worker_dist:     {summary.get('worker_distribution')}",
        f"redis_xpending:  {report.get('redis_xpending_after')}",
        f"json_report:     {json_path}",
        f"csv_tasks:       {csv_path}",
    ]
    txt_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    return {"json": str(json_path), "csv": str(csv_path), "txt": str(txt_path)}


def main() -> int:
    parser = argparse.ArgumentParser(description="Phase 11 OBB async stability suite")
    parser.add_argument("--url", default="http://127.0.0.1:8080")
    parser.add_argument("--endpoint", default="/api/v1/detect/obb/async")
    parser.add_argument("--image", required=True)
    parser.add_argument("--tasks", type=int, default=100)
    parser.add_argument("--concurrency", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=180.0, help="Global polling timeout in seconds")
    parser.add_argument("--poll-interval", type=float, default=0.25)
    parser.add_argument("--request-timeout", type=float, default=30.0)
    parser.add_argument("--download-results", type=int, default=3)
    parser.add_argument("--report-dir", default="reports/phase11")
    parser.add_argument("--model", default="obb")
    parser.add_argument("--redis-cli", default="redis-cli")
    parser.add_argument("--redis-host", default="172.19.196.109")
    parser.add_argument("--redis-port", type=int, default=6379)
    parser.add_argument("--redis-stream", default="yolo:stream:obb")
    parser.add_argument("--redis-group", default="yolo11_obb_group")
    parser.add_argument("--skip-redis-check", action="store_true")
    args = parser.parse_args()

    image_path = Path(args.image)
    if not image_path.exists():
        print(f"[ERROR] image not found: {image_path}", file=sys.stderr)
        return 2
    image_bytes = image_path.read_bytes()
    content_type = mimetypes.guess_type(str(image_path))[0] or "application/octet-stream"

    print(f"Server:      {args.url}")
    print(f"Endpoint:    {args.endpoint}")
    print(f"Image:       {image_path} ({len(image_bytes)} bytes, {content_type})")
    print(f"Tasks:       {args.tasks}")
    print(f"Concurrency: {args.concurrency}")

    ready_before = query_endpoint(args.url, f"/api/v1/ready?model={urllib.parse.quote(args.model)}", args.request_timeout)
    workers_before = query_endpoint(args.url, f"/api/v1/workers?model={urllib.parse.quote(args.model)}", args.request_timeout)
    metrics_before = query_endpoint(args.url, f"/api/v1/metrics?model={urllib.parse.quote(args.model)}", args.request_timeout)
    redis_before = None if args.skip_redis_check else redis_xpending(args.redis_cli, args.redis_host, args.redis_port, args.redis_stream, args.redis_group)

    if ready_before.get("ready") is not True:
        print("[WARN] ready?model is not true before test. Continue anyway.")

    start = time.perf_counter()
    submit_payloads: List[Dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=max(1, args.concurrency)) as executor:
        futures = [
            executor.submit(submit_task, args.url, args.endpoint, image_bytes, content_type, args.request_timeout)
            for _ in range(args.tasks)
        ]
        for future in as_completed(futures):
            submit_payloads.append(future.result())

    submit_seconds = time.perf_counter() - start
    task_ids = [str(p.get("task_id")) for p in submit_payloads if p.get("success") is True and p.get("task_id")]
    print(f"Submitted ok: {len(task_ids)}/{args.tasks} in {submit_seconds:.3f}s")

    results = wait_all(args.url, task_ids, timeout_s=args.timeout, poll_interval_s=args.poll_interval, request_timeout=args.request_timeout)
    elapsed_seconds = time.perf_counter() - start
    summary = summarize(results, elapsed_seconds)

    metrics_after = query_endpoint(args.url, f"/api/v1/metrics?model={urllib.parse.quote(args.model)}", args.request_timeout)
    workers_after = query_endpoint(args.url, f"/api/v1/workers?model={urllib.parse.quote(args.model)}", args.request_timeout)
    ready_after = query_endpoint(args.url, f"/api/v1/ready?model={urllib.parse.quote(args.model)}", args.request_timeout)
    redis_after = None if args.skip_redis_check else redis_xpending(args.redis_cli, args.redis_host, args.redis_port, args.redis_stream, args.redis_group)

    report_dir = Path(args.report_dir)
    image_dir = report_dir / "images"
    downloaded: List[Dict[str, Any]] = []
    done_task_ids = [tid for tid, payload in results.items() if (payload.get("_final_status") or payload.get("status")) == "done"]
    for task_id in done_task_ids[: max(0, args.download_results)]:
        output_path = image_dir / f"{task_id}.jpg"
        ok, size, error = http_download(build_url(args.url, f"/api/v1/result/{urllib.parse.quote(task_id)}/image"), output_path, args.request_timeout)
        downloaded.append({"task_id": task_id, "ok": ok, "size": size, "error": error, "path": str(output_path)})

    report = {
        "phase": "phase11_0_obb_stability_recovery",
        "started_at": datetime.now().isoformat(timespec="seconds"),
        "args": vars(args),
        "submit_seconds": submit_seconds,
        "submitted_ok": len(task_ids),
        "summary": summary,
        "ready_before": ready_before,
        "workers_before": workers_before,
        "metrics_before": metrics_before,
        "redis_xpending_before": redis_before,
        "ready_after": ready_after,
        "workers_after": workers_after,
        "metrics_after": metrics_after,
        "redis_xpending_after": redis_after,
        "downloaded_images": downloaded,
        "submit_payloads": submit_payloads,
        "results": results,
    }
    paths = write_reports(report_dir, "phase11_obb_stability", submit_payloads, results, report)

    print("\n========== Phase 11 Stability Summary ==========")
    print(f"Elapsed seconds:     {summary['elapsed_seconds']:.3f}")
    print(f"Done/Failed/Timeout: {summary['status_counts'].get('done',0)}/{summary['status_counts'].get('failed',0)}/{summary['status_counts'].get('timeout',0)}")
    print(f"Status counts:       {summary['status_counts']}")
    print(f"Throughput QPS:      {summary['throughput_qps']:.3f}")
    print(f"Worker distribution: {summary['worker_distribution']}")
    print(f"Redis XPENDING:      {redis_after}")
    print(f"Reports:             {paths}")

    pass_status = (
        len(task_ids) == args.tasks
        and summary["status_counts"].get("done", 0) == args.tasks
        and summary["status_counts"].get("failed", 0) == 0
        and summary["status_counts"].get("timeout", 0) == 0
    )
    if redis_after and redis_after.get("pending") not in (0, None):
        pass_status = False

    if pass_status:
        print("[PASS] Phase 11 stability suite passed.")
        return 0
    print("[FAIL] Phase 11 stability suite did not meet acceptance criteria.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
