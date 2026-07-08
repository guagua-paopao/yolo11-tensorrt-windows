#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Phase 11 manual Worker recovery test.

This script submits a burst of OBB async tasks, gives you a window to kill the Worker,
then waits for you to restart it and verifies that tasks finally reach done/failed and XPENDING clears.
"""
from __future__ import annotations

import argparse
import json
import mimetypes
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


def http_json(method: str, url: str, body: Optional[bytes] = None,
              headers: Optional[Dict[str, str]] = None, timeout: float = 30.0) -> Tuple[int, Dict[str, Any]]:
    req = urllib.request.Request(url, data=body, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8", errors="replace"))
    except urllib.error.HTTPError as exc:
        try:
            return exc.code, json.loads(exc.read().decode("utf-8", errors="replace"))
        except Exception:
            return exc.code, {"success": False, "error": str(exc)}
    except Exception as exc:  # noqa: BLE001
        return 0, {"success": False, "error": str(exc), "error_type": exc.__class__.__name__}


def build_url(base: str, path: str) -> str:
    return base.rstrip("/") + "/" + path.lstrip("/")


def redis_xpending(redis_cli: str, host: str, port: int, stream: str, group: str) -> Dict[str, Any]:
    cmd = [redis_cli, "-h", host, "-p", str(port), "XPENDING", stream, group]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=10, check=False)
        text = (proc.stdout or "") + (proc.stderr or "")
        pending: Optional[int] = None
        for token in text.replace("\r", "\n").replace(")", " ").replace("(", " ").split():
            if token.isdigit():
                pending = int(token)
                break
        return {"available": proc.returncode == 0, "pending": pending, "raw": text.strip(), "cmd": " ".join(cmd)}
    except Exception as exc:  # noqa: BLE001
        return {"available": False, "pending": None, "raw": str(exc), "cmd": " ".join(cmd)}


def submit_one(base_url: str, endpoint: str, image_bytes: bytes, content_type: str) -> Dict[str, Any]:
    code, payload = http_json("POST", build_url(base_url, endpoint), body=image_bytes, headers={"Content-Type": content_type}, timeout=30.0)
    payload["_http_status"] = code
    return payload


def poll_counts(base_url: str, task_ids: List[str]) -> Dict[str, int]:
    counts: Dict[str, int] = {}
    for tid in task_ids:
        _, payload = http_json("GET", build_url(base_url, f"/api/v1/result/{urllib.parse.quote(tid)}"), timeout=30.0)
        status = str(payload.get("status", "unknown")).lower()
        counts[status] = counts.get(status, 0) + 1
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description="Phase 11 manual Worker recovery test")
    parser.add_argument("--url", default="http://127.0.0.1:8080")
    parser.add_argument("--endpoint", default="/api/v1/detect/obb/async")
    parser.add_argument("--image", required=True)
    parser.add_argument("--tasks", type=int, default=100)
    parser.add_argument("--concurrency", type=int, default=10)
    parser.add_argument("--kill-window", type=int, default=10, help="Seconds to wait while you kill the Worker")
    parser.add_argument("--timeout", type=int, default=240, help="Seconds to wait after Worker restart")
    parser.add_argument("--poll-interval", type=float, default=1.0)
    parser.add_argument("--model", default="obb")
    parser.add_argument("--redis-cli", default="redis-cli")
    parser.add_argument("--redis-host", default="172.19.196.109")
    parser.add_argument("--redis-port", type=int, default=6379)
    parser.add_argument("--redis-stream", default="yolo:stream:obb")
    parser.add_argument("--redis-group", default="yolo11_obb_group")
    parser.add_argument("--report-dir", default="reports/phase11")
    args = parser.parse_args()

    image_path = Path(args.image)
    if not image_path.exists():
        print(f"[ERROR] image not found: {image_path}")
        return 2
    image_bytes = image_path.read_bytes()
    content_type = mimetypes.guess_type(str(image_path))[0] or "application/octet-stream"

    print("[1/5] Checking ready before recovery test...")
    _, ready = http_json("GET", build_url(args.url, f"/api/v1/ready?model={urllib.parse.quote(args.model)}"), timeout=30.0)
    print(json.dumps(ready, ensure_ascii=False, indent=2))
    if ready.get("ready") is not True:
        print("[WARN] ready is not true. Continue only if you intentionally test no-worker behavior.")

    print(f"[2/5] Submitting {args.tasks} tasks with concurrency={args.concurrency}...")
    submit_payloads: List[Dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=max(1, args.concurrency)) as executor:
        futures = [executor.submit(submit_one, args.url, args.endpoint, image_bytes, content_type) for _ in range(args.tasks)]
        for fut in as_completed(futures):
            submit_payloads.append(fut.result())
    task_ids = [str(p.get("task_id")) for p in submit_payloads if p.get("success") is True and p.get("task_id")]
    print(f"Submitted ok: {len(task_ids)}/{args.tasks}")

    print("[3/5] Kill the OBB Worker NOW, for example Ctrl+C in the Worker window.")
    print(f"Waiting {args.kill_window}s before checking intermediate status...")
    time.sleep(args.kill_window)
    counts_during_kill = poll_counts(args.url, task_ids)
    pending_during_kill = redis_xpending(args.redis_cli, args.redis_host, args.redis_port, args.redis_stream, args.redis_group)
    print(f"Status during kill window: {counts_during_kill}")
    print(f"XPENDING during kill window: {pending_during_kill}")

    input("[4/5] Restart the Worker now, then press Enter here to continue polling...")

    deadline = time.time() + args.timeout
    final_counts: Dict[str, int] = {}
    while time.time() < deadline:
        final_counts = poll_counts(args.url, task_ids)
        done = final_counts.get("done", 0)
        failed = final_counts.get("failed", 0)
        timeout_like = final_counts.get("timeout", 0)
        print(f"Polling: {final_counts}")
        if done + failed + timeout_like >= len(task_ids):
            break
        time.sleep(args.poll_interval)

    pending_after = redis_xpending(args.redis_cli, args.redis_host, args.redis_port, args.redis_stream, args.redis_group)
    _, workers_after = http_json("GET", build_url(args.url, f"/api/v1/workers?model={urllib.parse.quote(args.model)}"), timeout=30.0)
    _, metrics_after = http_json("GET", build_url(args.url, f"/api/v1/metrics?model={urllib.parse.quote(args.model)}"), timeout=30.0)

    report = {
        "phase": "phase11_0_worker_recovery_test",
        "started_at": datetime.now().isoformat(timespec="seconds"),
        "args": vars(args),
        "submitted_ok": len(task_ids),
        "counts_during_kill": counts_during_kill,
        "pending_during_kill": pending_during_kill,
        "final_counts": final_counts,
        "pending_after": pending_after,
        "workers_after": workers_after,
        "metrics_after": metrics_after,
        "submit_payloads": submit_payloads,
    }
    out_dir = Path(args.report_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"phase11_worker_recovery_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    out_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"[5/5] Report: {out_path}")

    pass_status = (
        len(task_ids) == args.tasks
        and final_counts.get("failed", 0) == 0
        and final_counts.get("timeout", 0) == 0
        and final_counts.get("done", 0) == args.tasks
        and pending_after.get("pending") in (0, None)
    )
    print("[PASS] Worker recovery test passed." if pass_status else "[FAIL] Worker recovery test did not meet acceptance criteria.")
    return 0 if pass_status else 1


if __name__ == "__main__":
    raise SystemExit(main())
