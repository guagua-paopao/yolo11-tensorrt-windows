#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Phase 18 regression runner: SEG registry/smoke/invalid-input and optional Phase 17.5 regression."""
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
    except subprocess.TimeoutExpired:
        duration = time.time() - start
        return {"name": name, "success": False, "timeout": True, "duration_seconds": round(duration, 3), "command": cmd}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seg-url", default="http://127.0.0.1:8086")
    ap.add_argument("--seg-image", default=str(ROOT / "images" / "bus.png"))
    ap.add_argument("--pose-url", default="http://127.0.0.1:8085")
    ap.add_argument("--pose-image", default=str(ROOT / "images" / "bus.png"))
    ap.add_argument("--cls-url", default="http://127.0.0.1:8084")
    ap.add_argument("--cls-image", default=str(ROOT / "images" / "bus.png"))
    ap.add_argument("--out-dir", default=str(ROOT / "reports" / "phase18"))
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--skip-phase17-5", action="store_true", help="Only run SEG tests")
    ap.add_argument("--skip-phase16", action="store_true", help="When running Phase17.5 regression, skip old Phase16 full regression")
    args = ap.parse_args()

    out_dir = Path(args.out_dir) / now_stamp()
    out_dir.mkdir(parents=True, exist_ok=True)

    results: List[Dict[str, Any]] = []
    results.append(run_step(
        "01_phase18_seg_registry",
        [sys.executable, "tools/phase18_seg_registry_test.py", "--url", args.seg_url, "--json-out", str(out_dir / "seg_registry.json")],
        out_dir,
        120,
    ))
    results.append(run_step(
        "02_phase18_seg_smoke",
        [sys.executable, "tools/phase18_seg_smoke_test.py", "--url", args.seg_url, "--image", args.seg_image, "--json-out", str(out_dir / "seg_smoke.json")],
        out_dir,
        args.timeout,
    ))
    results.append(run_step(
        "03_phase18_seg_invalid_input",
        [sys.executable, "tools/phase18_seg_invalid_input_test.py", "--url", args.seg_url],
        out_dir,
        120,
    ))

    if not args.skip_phase17_5:
        cmd = [sys.executable, "tools/run_phase17_5_regression.py", "--pose-url", args.pose_url, "--pose-image", args.pose_image, "--cls-url", args.cls_url, "--cls-image", args.cls_image, "--out-dir", str(out_dir / "phase17_5")]
        if args.skip_phase16:
            cmd.append("--skip-phase16")
        results.append(run_step("04_phase17_5_regression", cmd, out_dir, args.timeout))

    ok = all(r.get("success") for r in results)
    summary = {"phase": "phase18_seg_regression", "success": ok, "out_dir": str(out_dir), "results": results}
    (out_dir / "phase18_regression_summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print("\n" + "=" * 88)
    print(f"Phase 18 Regression Summary: success={ok}")
    print(f"out_dir={out_dir}")
    for r in results:
        print(f"[{'PASS' if r.get('success') else 'FAIL'}] {r.get('name')} duration={r.get('duration_seconds', '-')}")
    print("=" * 88)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
