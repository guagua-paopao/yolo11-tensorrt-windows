#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase 12 concurrent detect + OBB benchmark."""

from __future__ import annotations

import argparse
import concurrent.futures as futures
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, List


def http_json(method: str, url: str, body: bytes | None = None, content_type: str | None = None, timeout: int = 30) -> Dict[str, Any]:
    headers = {}
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=body, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            data = json.loads(raw) if raw else {}
            data["http_status"] = resp.status
            return data
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", errors="replace")
        try:
            data = json.loads(raw)
        except Exception:
            data = {"raw": raw}
        data["http_status"] = e.code
        return data
    except Exception as e:
        return {"success": False, "error": str(e), "http_status": 0}


def submit(base_url: str, endpoint: str, image_bytes: bytes, image_path: Path, timeout: int) -> Dict[str, Any]:
    content_type = "image/png" if image_path.suffix.lower() == ".png" else "image/jpeg"
    return http_json("POST", f"{base_url}{endpoint}", image_bytes, content_type, timeout)


def wait_done(base_url: str, task_id: str, timeout: int) -> Dict[str, Any]:
    deadline = time.time() + timeout
    last: Dict[str, Any] = {}
    while time.time() < deadline:
        last = http_json("GET", f"{base_url}/api/v1/result/{urllib.parse.quote(task_id)}", timeout=20)
        if last.get("status") in ("done", "failed"):
            return last
        time.sleep(0.2)
    last["status"] = "timeout"
    return last


def submit_and_wait(model: str, base_url: str, endpoint: str, image_bytes: bytes, image_path: Path, timeout: int) -> Dict[str, Any]:
    t0 = time.perf_counter()
    submit_data = submit(base_url, endpoint, image_bytes, image_path, timeout)
    task_id = submit_data.get("task_id")
    if not task_id:
        return {"model": model, "status": "submit_failed", "submit": submit_data, "elapsed_ms": (time.perf_counter() - t0) * 1000.0}
    result = wait_done(base_url, task_id, timeout)
    return {
        "model": model,
        "task_id": task_id,
        "status": result.get("status"),
        "result_model_type": result.get("model_type"),
        "num_detections": result.get("num_detections"),
        "queue_wait_ms": result.get("queue_wait_ms"),
        "inference_ms": result.get("inference_ms"),
        "total_ms": result.get("total_ms"),
        "elapsed_ms": (time.perf_counter() - t0) * 1000.0,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Phase 12 concurrent detect + OBB benchmark")
    ap.add_argument("--detect-url", default="http://127.0.0.1:8080")
    ap.add_argument("--obb-url", default="http://127.0.0.1:8081")
    ap.add_argument("--detect-image", default="D:/tensorrtx/yolo11/images/bus.png")
    ap.add_argument("--obb-image", default="D:/tensorrtx/yolo11/images/a.jpg")
    ap.add_argument("--detect-tasks", type=int, default=50)
    ap.add_argument("--obb-tasks", type=int, default=50)
    ap.add_argument("--concurrency", type=int, default=10)
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--out-dir", default="reports/phase12")
    args = ap.parse_args()

    detect_img = Path(args.detect_image)
    obb_img = Path(args.obb_image)
    detect_bytes = detect_img.read_bytes()
    obb_bytes = obb_img.read_bytes()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    jobs = []
    for _ in range(args.detect_tasks):
        jobs.append(("detect", args.detect_url, "/api/v1/detect/image/async", detect_bytes, detect_img))
    for _ in range(args.obb_tasks):
        jobs.append(("obb", args.obb_url, "/api/v1/detect/obb/async", obb_bytes, obb_img))

    print(f"Submitting mixed jobs: detect={args.detect_tasks}, obb={args.obb_tasks}, concurrency={args.concurrency}")
    t0 = time.perf_counter()
    results: List[Dict[str, Any]] = []
    with futures.ThreadPoolExecutor(max_workers=args.concurrency) as ex:
        futs = [ex.submit(submit_and_wait, model, url, ep, data, path, args.timeout) for model, url, ep, data, path in jobs]
        for f in futures.as_completed(futs):
            results.append(f.result())
    elapsed = time.perf_counter() - t0

    summary: Dict[str, Any] = {
        "phase": "phase12_0_detect_obb_dual_parallel",
        "elapsed_seconds": elapsed,
        "total_tasks": len(results),
        "throughput_qps": len(results) / elapsed if elapsed > 0 else 0,
        "by_model": {},
        "status_counts": {},
    }
    for r in results:
        model = r.get("model", "unknown")
        status = r.get("status", "unknown")
        summary["status_counts"][status] = summary["status_counts"].get(status, 0) + 1
        bucket = summary["by_model"].setdefault(model, {"done": 0, "failed": 0, "timeout": 0, "other": 0})
        if status == "done":
            bucket["done"] += 1
        elif status == "failed":
            bucket["failed"] += 1
        elif status == "timeout":
            bucket["timeout"] += 1
        else:
            bucket["other"] += 1

    stamp = time.strftime("%Y%m%d_%H%M%S")
    report_json = out_dir / f"phase12_dual_benchmark_{stamp}.json"
    report_json.write_text(json.dumps({"summary": summary, "tasks": results}, ensure_ascii=False, indent=2), encoding="utf-8")

    print("\n========== Phase 12 Dual Benchmark Summary ==========")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"Report: {report_json}")

    ok = all(r.get("status") == "done" for r in results)
    if ok:
        print("[PASS] Phase 12 dual benchmark passed.")
        return 0
    print("[FAIL] Phase 12 dual benchmark failed.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
