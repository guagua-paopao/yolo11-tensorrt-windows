#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase 17 CLS worker registry wrapper."""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8084")
    ap.add_argument("--json-out", default="")
    args = ap.parse_args()

    cmd = [
        sys.executable,
        str(ROOT / "tools" / "phase15_worker_registry_test.py"),
        "--service", f"cls={args.url}",
        "--model", "cls",
        "--task-kind", "image_async",
        "--worker-group", "image_cls_gpu0",
    ]
    if args.json_out:
        cmd += ["--json-out", args.json_out]
    return subprocess.call(cmd, cwd=str(ROOT))


if __name__ == "__main__":
    raise SystemExit(main())
