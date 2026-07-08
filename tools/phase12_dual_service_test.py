#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Phase 12.0 Detect + OBB dual-service smoke test.

This script assumes two HTTP server processes:
  - Detect server: http://127.0.0.1:8080
  - OBB server:    http://127.0.0.1:8081
and two worker processes:
  - worker_1 consuming yolo:stream:detect / yolo11_group
  - obb_worker_1 consuming yolo:stream:obb / yolo11_obb_group
"""

from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, Tuple


def http_json(method: str, url: str, body: bytes | None = None, content_type: str | None = None, timeout: int = 30) -> Tuple[int, Dict[str, Any]]:
    headers = {}
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=body, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            return resp.status, json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", errors="replace")
        try:
            data = json.loads(raw)
        except Exception:
            data = {"raw": raw}
        return e.code, data


def http_bytes(url: str, timeout: int = 30) -> Tuple[int, bytes]:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def wait_done(base_url: str, task_id: str, timeout_s: int) -> Dict[str, Any]:
    deadline = time.time() + timeout_s
    last = {}
    while time.time() < deadline:
        code, data = http_json("GET", f"{base_url}/api/v1/result/{urllib.parse.quote(task_id)}", timeout=15)
        last = data
        status = data.get("status")
        if code == 200 and status in ("done", "failed"):
            return data
        time.sleep(0.2)
    last["status"] = last.get("status", "timeout")
    return last


def submit_image(base_url: str, endpoint: str, image_path: Path, timeout: int) -> Dict[str, Any]:
    image_bytes = image_path.read_bytes()
    content_type = "image/png" if image_path.suffix.lower() == ".png" else "image/jpeg"
    code, data = http_json("POST", f"{base_url}{endpoint}", image_bytes, content_type, timeout)
    data["http_status"] = code
    return data


def main() -> int:
    ap = argparse.ArgumentParser(description="Phase 12 Detect + OBB dual-service smoke test")
    ap.add_argument("--detect-url", default="http://127.0.0.1:8080")
    ap.add_argument("--obb-url", default="http://127.0.0.1:8081")
    ap.add_argument("--detect-image", default="D:/tensorrtx/yolo11/images/bus.png")
    ap.add_argument("--obb-image", default="D:/tensorrtx/yolo11/images/a.jpg")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--out-dir", default="reports/phase12")
    args = ap.parse_args()

    detect_image = Path(args.detect_image)
    obb_image = Path(args.obb_image)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    print("[1/6] Checking detect server...")
    detect_health = http_json("GET", f"{args.detect_url}/api/v1/health", timeout=20)[1]
    detect_ready = http_json("GET", f"{args.detect_url}/api/v1/ready?model=detect", timeout=20)[1]
    print(json.dumps({"health": detect_health.get("success"), "ready": detect_ready.get("ready"), "model": detect_health.get("model_type")}, indent=2))

    print("[2/6] Checking OBB server...")
    obb_health = http_json("GET", f"{args.obb_url}/api/v1/health", timeout=20)[1]
    obb_ready = http_json("GET", f"{args.obb_url}/api/v1/ready?model=obb", timeout=20)[1]
    print(json.dumps({"health": obb_health.get("success"), "ready": obb_ready.get("ready"), "model": obb_health.get("model_type")}, indent=2))

    print("[3/6] Submitting detect task...")
    detect_submit = submit_image(args.detect_url, "/api/v1/detect/image/async", detect_image, args.timeout)
    print(json.dumps(detect_submit, ensure_ascii=False, indent=2))
    detect_task_id = detect_submit.get("task_id")

    print("[4/6] Submitting OBB task...")
    obb_submit = submit_image(args.obb_url, "/api/v1/detect/obb/async", obb_image, args.timeout)
    print(json.dumps(obb_submit, ensure_ascii=False, indent=2))
    obb_task_id = obb_submit.get("task_id")

    if not detect_task_id or not obb_task_id:
        print("[FAIL] task_id missing")
        return 2

    print("[5/6] Polling results...")
    detect_result = wait_done(args.detect_url, detect_task_id, args.timeout)
    obb_result = wait_done(args.obb_url, obb_task_id, args.timeout)
    print(json.dumps({
        "detect": {"task_id": detect_task_id, "status": detect_result.get("status"), "model_type": detect_result.get("model_type"), "num_detections": detect_result.get("num_detections")},
        "obb": {"task_id": obb_task_id, "status": obb_result.get("status"), "model_type": obb_result.get("model_type"), "num_detections": obb_result.get("num_detections")},
    }, indent=2))

    print("[6/6] Downloading result images...")
    d_code, d_img = http_bytes(f"{args.detect_url}/api/v1/result/{urllib.parse.quote(detect_task_id)}/image", timeout=30)
    o_code, o_img = http_bytes(f"{args.obb_url}/api/v1/result/{urllib.parse.quote(obb_task_id)}/image", timeout=30)
    detect_img_path = out_dir / f"phase12_detect_{detect_task_id}.jpg"
    obb_img_path = out_dir / f"phase12_obb_{obb_task_id}.jpg"
    if d_code == 200 and d_img:
        detect_img_path.write_bytes(d_img)
    if o_code == 200 and o_img:
        obb_img_path.write_bytes(o_img)

    report = {
        "phase": "phase12_0_detect_obb_dual_parallel",
        "detect_url": args.detect_url,
        "obb_url": args.obb_url,
        "detect_health": detect_health,
        "detect_ready": detect_ready,
        "obb_health": obb_health,
        "obb_ready": obb_ready,
        "detect_submit": detect_submit,
        "obb_submit": obb_submit,
        "detect_result": detect_result,
        "obb_result": obb_result,
        "detect_image_http_status": d_code,
        "obb_image_http_status": o_code,
        "detect_result_image": str(detect_img_path) if d_code == 200 else None,
        "obb_result_image": str(obb_img_path) if o_code == 200 else None,
    }
    stamp = time.strftime("%Y%m%d_%H%M%S")
    report_path = out_dir / f"phase12_dual_service_{stamp}.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    ok = (
        detect_health.get("success") is True and detect_ready.get("ready") is True and
        obb_health.get("success") is True and obb_ready.get("ready") is True and
        detect_result.get("status") == "done" and detect_result.get("model_type") == "detect" and
        obb_result.get("status") == "done" and obb_result.get("model_type") == "obb" and
        d_code == 200 and o_code == 200
    )
    print(f"Report: {report_path}")
    if ok:
        print("[PASS] Phase 12 dual-service smoke test passed.")
        return 0
    print("[FAIL] Phase 12 dual-service smoke test failed.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
