#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase 18 SEG image async smoke test."""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from typing import Any, Dict

import requests


def get_json(url: str, timeout: float) -> Dict[str, Any]:
    r = requests.get(url, timeout=timeout)
    try:
        body = r.json()
    except Exception:
        body = {"raw": r.text}
    body["_status_code"] = r.status_code
    return body


def post_image(url: str, image_path: Path, timeout: float) -> Dict[str, Any]:
    data = image_path.read_bytes()
    r = requests.post(
        url,
        data=data,
        headers={"Content-Type": "image/jpeg" if image_path.suffix.lower() in {".jpg", ".jpeg"} else "image/png"},
        timeout=timeout,
    )
    try:
        body = r.json()
    except Exception:
        body = {"raw": r.text}
    body["_status_code"] = r.status_code
    return body


def has_seg_result(result: Dict[str, Any]) -> bool:
    if result.get("segmentation_coordinate_system") != "original_image_pixels":
        return False
    if result.get("segmentation_format") != "bbox_polygon_mask_metadata":
        return False
    segmentations = result.get("segmentations")
    if not isinstance(segmentations, list):
        return False
    # Zero detections is still valid for an image with no segmentable object.
    for item in segmentations:
        if not isinstance(item, dict):
            return False
        mask = item.get("mask")
        if not isinstance(mask, dict):
            return False
        if mask.get("coordinate_system") != "original_image_pixels":
            return False
        if "area_pixels" not in mask:
            return False
        if "polygon" not in item:
            return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8086")
    ap.add_argument("--image", default=str(Path(__file__).resolve().parents[1] / "images" / "bus.png"))
    ap.add_argument("--timeout", type=float, default=180.0)
    ap.add_argument("--poll-interval", type=float, default=0.5)
    ap.add_argument("--json-out", default="")
    args = ap.parse_args()

    base = args.url.rstrip("/")
    image_path = Path(args.image)
    if not image_path.exists():
        raise FileNotFoundError(f"image not found: {image_path}")

    report: Dict[str, Any] = {"url": base, "image": str(image_path), "steps": {}}

    report["steps"]["health"] = get_json(f"{base}/api/v1/health", 5)
    if report["steps"]["health"].get("_status_code") != 200:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 1

    report["steps"]["ready"] = get_json(f"{base}/api/v1/ready?model=seg&task_kind=image_async&worker_group=image_seg_gpu0", 5)
    if report["steps"]["ready"].get("ready") is not True:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 1

    submit = post_image(f"{base}/api/v1/segment/image/async", image_path, 30)
    report["steps"]["submit"] = submit
    if submit.get("_status_code") != 202 or submit.get("success") is not True:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 1

    task_id = submit.get("task_id")
    if not task_id:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 1

    deadline = time.time() + args.timeout
    last: Dict[str, Any] = {}
    while time.time() < deadline:
        last = get_json(f"{base}/api/v1/result/{task_id}", 10)
        status = last.get("status")
        if status in {"done", "failed"}:
            break
        time.sleep(args.poll_interval)
    report["steps"]["result"] = last

    image_check = get_json(f"{base}/api/v1/metrics?model=seg", 5)
    report["steps"]["metrics"] = image_check

    ok = last.get("status") == "done" and last.get("model_type") == "seg" and has_seg_result(last)
    report["success"] = ok
    if args.json_out:
        out = Path(args.json_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"Saved report: {out}")

    print(json.dumps(report, ensure_ascii=False, indent=2))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
