#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase 17 regression runner: run CLS smoke/registry and optionally old Phase 16 regression."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, List

ROOT = Path(__file__).resolve().parents[1]


def now_stamp() -> str:
    return time.strftime("%Y%m%d_%H%M%S")


def run_step(name: str, cmd: List[str], out_dir: Path, timeout: int) -> Dict[str, Any]:
    print("\n" + "=" * 88)
    print(f"[RUN] {name}")
    print(" ".join(cmd))
    print("=" * 88)
    start = time.time()
    try:
        proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
        duration = time.time() - start
        stdout_path = out_dir / f"{name}.stdout.log"
        stderr_path = out_dir / f"{name}.stderr.log"
        stdout_path.write_text(proc.stdout, encoding="utf-8", errors="replace")
        stderr_path.write_text(proc.stderr, encoding="utf-8", errors="replace")
        print(proc.stdout[-4000:])
        if proc.stderr:
            print("[stderr tail]")
            print(proc.stderr[-2000:])
        ok = proc.returncode == 0
        print(f"[{'PASS' if ok else 'FAIL'}] {name}, returncode={proc.returncode}, duration={duration:.2f}s")
        return {"name": name, "success": ok, "returncode": proc.returncode, "duration_seconds": round(duration, 3), "stdout_log": str(stdout_path), "stderr_log": str(stderr_path), "command": cmd}
    except subprocess.TimeoutExpired as exc:
        duration = time.time() - start
        return {"name": name, "success": False, "timeout": True, "duration_seconds": round(duration, 3), "command": cmd}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cls-url", default="http://127.0.0.1:8084")
    ap.add_argument("--cls-image", default=str(ROOT / "images" / "bus.png"))
    ap.add_argument("--out-dir", default=str(ROOT / "reports" / "phase17"))
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--skip-phase16", action="store_true", help="Only run CLS tests")
    args = ap.parse_args()

    out_dir = Path(args.out_dir) / now_stamp()
    out_dir.mkdir(parents=True, exist_ok=True)

    results: List[Dict[str, Any]] = []
    results.append(run_step(
        "01_phase17_cls_registry",
        [sys.executable, "tools/phase17_cls_registry_test.py", "--url", args.cls_url, "--json-out", str(out_dir / "cls_registry.json")],
        out_dir,
        120,
    ))
    results.append(run_step(
        "02_phase17_cls_smoke",
        [sys.executable, "tools/phase17_cls_smoke_test.py", "--url", args.cls_url, "--image", args.cls_image, "--json-out", str(out_dir / "cls_smoke.json")],
        out_dir,
        args.timeout,
    ))

    if not args.skip_phase16:
        results.append(run_step(
            "03_phase16_full_regression",
            [sys.executable, "tools/run_full_regression.py", "--out-dir", str(out_dir / "phase16")],
            out_dir,
            args.timeout,
        ))

    ok = all(r.get("success") for r in results)
    summary = {"phase": "phase17_cls_regression", "success": ok, "out_dir": str(out_dir), "results": results}
    (out_dir / "phase17_regression_summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print("\n" + "=" * 88)
    print(f"Phase 17 Regression Summary: success={ok}")
    print(f"out_dir={out_dir}")
    for r in results:
        print(f"[{'PASS' if r.get('success') else 'FAIL'}] {r.get('name')} duration={r.get('duration_seconds', '-')}")
    print("=" * 88)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
