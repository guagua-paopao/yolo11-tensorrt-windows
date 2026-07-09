#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase 18 SEG invalid input test."""
from __future__ import annotations

import argparse
import json
from typing import Any, Dict

import requests


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8086")
    args = ap.parse_args()

    base = args.url.rstrip("/")
    r = requests.post(
        f"{base}/api/v1/segment/image/async",
        data=b"this is not an image",
        headers={"Content-Type": "application/octet-stream"},
        timeout=10,
    )
    try:
        body: Dict[str, Any] = r.json()
    except Exception:
        body = {"raw": r.text}
    body["_status_code"] = r.status_code
    ok = r.status_code == 400 and body.get("error_code") == "IMAGE_DECODE_FAILED"
    print(json.dumps({"success": ok, "response": body}, ensure_ascii=False, indent=2))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
