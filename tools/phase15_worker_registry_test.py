#!/usr/bin/env python3
r"""
Phase 15 worker capability / ready isolation / metrics grouping smoke test.

Run after starting the server/worker pair you want to check.  It does not submit
heavy inference tasks; it verifies that the Phase 15 capability fields are
visible in /health, /ready, /workers and /metrics.

Examples:
  python tools\phase15_worker_registry_test.py --service detect=http://127.0.0.1:8080
  python tools\phase15_worker_registry_test.py --service stream=http://127.0.0.1:8083 --model stream --task-kind live_stream
"""
import argparse
import json
import sys
from typing import Any, Dict, List, Tuple

import requests


def get_json(url: str, timeout: float) -> Tuple[int, Dict[str, Any]]:
    r = requests.get(url, timeout=timeout)
    try:
        return r.status_code, r.json()
    except Exception:
        return r.status_code, {"raw": r.text}


def require(condition: bool, message: str, errors: List[str]) -> None:
    if not condition:
        errors.append(message)


def check_service(name: str, base_url: str, model: str, task_kind: str, worker_group: str, timeout: float) -> Dict[str, Any]:
    base_url = base_url.rstrip("/")
    errors: List[str] = []
    result: Dict[str, Any] = {"name": name, "base_url": base_url, "checks": {}}

    code, health = get_json(f"{base_url}/api/v1/health", timeout)
    result["checks"]["health"] = {"status_code": code, "body": health}
    require(code == 200 and health.get("success") is True, f"{name}: /health failed", errors)
    require("worker_group" in health, f"{name}: /health missing worker_group", errors)
    require("task_kind" in health, f"{name}: /health missing task_kind", errors)

    params = []
    if model:
        params.append(("model", model))
    if task_kind:
        params.append(("task_kind", task_kind))
    if worker_group:
        params.append(("worker_group", worker_group))
    suffix = ""
    if params:
        suffix = "?" + "&".join(f"{k}={v}" for k, v in params)

    code, ready = get_json(f"{base_url}/api/v1/ready{suffix}", timeout)
    result["checks"]["ready"] = {"status_code": code, "body": ready}
    require("capability_filters" in ready, f"{name}: /ready missing capability_filters", errors)
    require("workers" in ready, f"{name}: /ready missing workers", errors)

    code, workers = get_json(f"{base_url}/api/v1/workers{suffix}", timeout)
    result["checks"]["workers"] = {"status_code": code, "body": workers}
    require(code == 200 and workers.get("success") is True, f"{name}: /workers failed", errors)
    worker_list = workers.get("workers", [])
    require(isinstance(worker_list, list), f"{name}: /workers workers is not a list", errors)
    for idx, worker in enumerate(worker_list):
        for field in ["model_type", "runner_model_type", "worker_group", "worker_kind", "task_kind", "stream_type", "gpu_id", "max_concurrency"]:
            require(field in worker, f"{name}: worker[{idx}] missing {field}", errors)
        if model:
            require(str(worker.get("model_type", "")).lower() == model.lower(), f"{name}: worker[{idx}] model_type does not match {model}", errors)
        if task_kind:
            require(str(worker.get("task_kind", "")).lower() == task_kind.lower(), f"{name}: worker[{idx}] task_kind does not match {task_kind}", errors)
        if worker_group:
            require(str(worker.get("worker_group", "")).lower() == worker_group.lower(), f"{name}: worker[{idx}] worker_group does not match {worker_group}", errors)

    code, metrics = get_json(f"{base_url}/api/v1/metrics{suffix}", timeout)
    result["checks"]["metrics"] = {"status_code": code, "body": metrics}
    require(code == 200 and metrics.get("success") is True, f"{name}: /metrics failed", errors)
    require("capability_filters" in metrics, f"{name}: /metrics missing capability_filters", errors)
    require("worker_group" in metrics, f"{name}: /metrics missing worker_group", errors)

    result["ok"] = len(errors) == 0
    result["errors"] = errors
    return result


def parse_service(value: str) -> Tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("service must be name=url, e.g. detect=http://127.0.0.1:8080")
    name, url = value.split("=", 1)
    if not name or not url:
        raise argparse.ArgumentTypeError("service name and url must be non-empty")
    return name, url


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--service", action="append", type=parse_service, required=True,
                        help="Service in name=url form. Can be repeated.")
    parser.add_argument("--model", default="", help="Optional model/profile filter: detect/obb/video/stream")
    parser.add_argument("--task-kind", default="", help="Optional task_kind filter: image_async/video_file/live_stream")
    parser.add_argument("--worker-group", default="", help="Optional worker_group filter, e.g. image_detect_gpu0")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--json-out", default="")
    args = parser.parse_args()

    all_results = []
    ok = True
    for name, url in args.service:
        res = check_service(name, url, args.model, args.task_kind, args.worker_group, args.timeout)
        all_results.append(res)
        ok = ok and res["ok"]
        print(f"[{name}] {'PASS' if res['ok'] else 'FAIL'} {url}")
        for err in res["errors"]:
            print(f"  - {err}")

    summary = {"success": ok, "results": all_results}
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(summary, f, ensure_ascii=False, indent=2)
        print(f"Saved report: {args.json_out}")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
