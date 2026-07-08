#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase 11 invalid input test for the async image API."""
from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, Optional, Tuple


def load_json(data: bytes) -> Dict[str, Any]:
    try:
        return json.loads(data.decode("utf-8", errors="replace"))
    except Exception:
        return {"_raw": data.decode("utf-8", errors="replace")}


def http_json(method: str, url: str, body: Optional[bytes] = None,
              headers: Optional[Dict[str, str]] = None, timeout: float = 30.0) -> Tuple[int, Dict[str, Any]]:
    req = urllib.request.Request(url, data=body, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, load_json(resp.read())
    except urllib.error.HTTPError as exc:
        return exc.code, load_json(exc.read())
    except Exception as exc:  # noqa: BLE001
        return 0, {"success": False, "error": str(exc), "error_type": exc.__class__.__name__}


def build_url(base: str, path: str) -> str:
    return base.rstrip("/") + "/" + path.lstrip("/")


def main() -> int:
    parser = argparse.ArgumentParser(description="Phase 11 invalid input test")
    parser.add_argument("--url", default="http://127.0.0.1:8080")
    parser.add_argument("--endpoint", default="/api/v1/detect/obb/async")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--poll-interval", type=float, default=0.25)
    parser.add_argument("--report-dir", default="reports/phase11")
    args = parser.parse_args()

    bad_payload = b"this is not a valid image file. phase11 invalid input test.\n"
    status_code, submit = http_json(
        "POST",
        build_url(args.url, args.endpoint),
        body=bad_payload,
        headers={"Content-Type": "image/jpeg"},
        timeout=30.0,
    )

    report: Dict[str, Any] = {
        "phase": "phase11_0_invalid_input_test",
        "started_at": datetime.now().isoformat(timespec="seconds"),
        "submit_http_status": status_code,
        "submit_response": submit,
    }

    # Accept both early HTTP rejection and queued task that eventually fails in Worker.
    if submit.get("success") is False and not submit.get("task_id"):
        report["verdict"] = "PASS_HTTP_REJECTED"
    else:
        task_id = submit.get("task_id")
        if not task_id:
            report["verdict"] = "FAIL_NO_TASK_ID"
        else:
            deadline = time.time() + args.timeout
            final: Dict[str, Any] = {}
            while time.time() < deadline:
                _, final = http_json("GET", build_url(args.url, f"/api/v1/result/{urllib.parse.quote(str(task_id))}"), timeout=30.0)
                if str(final.get("status", "")).lower() in {"done", "failed"}:
                    break
                time.sleep(args.poll_interval)
            report["final_result"] = final
            if str(final.get("status", "")).lower() == "failed":
                report["verdict"] = "PASS_WORKER_FAILED_CLEARLY"
            else:
                report["verdict"] = "FAIL_INVALID_INPUT_NOT_FAILED"

    out_dir = Path(args.report_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"phase11_invalid_input_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    out_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    print(json.dumps(report, ensure_ascii=False, indent=2))
    print(f"Report: {out_path}")
    return 0 if str(report.get("verdict", "")).startswith("PASS") else 1


if __name__ == "__main__":
    raise SystemExit(main())
