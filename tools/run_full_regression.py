#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Phase 16 full regression orchestrator.

It assumes the four HTTP services and four workers have already been started:
  - Detect 8080 + worker_1
  - OBB    8081 + obb_worker_1
  - Video  8082 + video_worker_1
  - Stream 8083 + stream_worker_1

Recommended usage from project root:
  python tools\run_full_regression.py

With camera-dependent stream metrics test:
  python tools\run_full_regression.py --include-camera-tests --camera-id 0 --invalid-camera-id 99
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional


ROOT = Path(__file__).resolve().parents[1]


class StepResult(Dict[str, Any]):
    pass


def now_stamp() -> str:
    return time.strftime("%Y%m%d_%H%M%S")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", errors="replace")


def run_step(name: str, cmd: List[str], out_dir: Path, timeout: int) -> StepResult:
    print("\n" + "=" * 88)
    print(f"[RUN] {name}")
    print(" ".join(cmd))
    print("=" * 88)
    start = time.time()
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
        duration = time.time() - start
        stdout_path = out_dir / f"{name}.stdout.log"
        stderr_path = out_dir / f"{name}.stderr.log"
        write_text(stdout_path, proc.stdout)
        write_text(stderr_path, proc.stderr)
        print(proc.stdout[-4000:])
        if proc.stderr:
            print("[stderr tail]")
            print(proc.stderr[-2000:])
        ok = proc.returncode == 0
        print(f"[{'PASS' if ok else 'FAIL'}] {name}, returncode={proc.returncode}, duration={duration:.2f}s")
        return StepResult(
            name=name,
            command=cmd,
            returncode=proc.returncode,
            success=ok,
            duration_seconds=round(duration, 3),
            stdout_log=str(stdout_path),
            stderr_log=str(stderr_path),
        )
    except subprocess.TimeoutExpired as exc:
        duration = time.time() - start
        stdout_path = out_dir / f"{name}.stdout.log"
        stderr_path = out_dir / f"{name}.stderr.log"
        write_text(stdout_path, exc.stdout or "")
        write_text(stderr_path, exc.stderr or "")
        print(f"[TIMEOUT] {name}, duration={duration:.2f}s")
        return StepResult(
            name=name,
            command=cmd,
            returncode=None,
            success=False,
            timeout=True,
            duration_seconds=round(duration, 3),
            stdout_log=str(stdout_path),
            stderr_log=str(stderr_path),
        )


def ensure_smoke_video(video_path: Optional[str], out_dir: Path, image_path: Path) -> Optional[Path]:
    if video_path:
        p = Path(video_path)
        if p.exists():
            return p
        raise FileNotFoundError(f"video file not found: {p}")

    candidates = [
        ROOT / "videos" / "smoke.mp4",
        ROOT / "videos" / "test.mp4",
        ROOT / "runtime" / "input" / "videos" / "detect" / "smoke.mp4",
    ]
    for p in candidates:
        if p.exists():
            return p

    generated = out_dir / "phase16_generated_smoke.mp4"
    try:
        import cv2  # type: ignore
    except Exception:
        print("[WARN] cv2 is not available in Python. Video tests will be skipped unless --video is provided.")
        return None

    img = cv2.imread(str(image_path))
    if img is None:
        print(f"[WARN] cannot read {image_path}. Video tests will be skipped.")
        return None

    h, w = img.shape[:2]
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(str(generated), fourcc, 25.0, (w, h))
    if not writer.isOpened():
        print("[WARN] cannot create smoke video. Video tests will be skipped.")
        return None
    for _ in range(80):
        writer.write(img)
    writer.release()
    print(f"[INFO] generated smoke video: {generated}")
    return generated


def main() -> int:
    ap = argparse.ArgumentParser(description="Phase 16 full regression runner")
    ap.add_argument("--detect-url", default="http://127.0.0.1:8080")
    ap.add_argument("--obb-url", default="http://127.0.0.1:8081")
    ap.add_argument("--video-url", default="http://127.0.0.1:8082")
    ap.add_argument("--stream-url", default="http://127.0.0.1:8083")
    ap.add_argument("--detect-image", default=str(ROOT / "images" / "bus.png"))
    ap.add_argument("--obb-image", default=str(ROOT / "images" / "a.jpg"))
    ap.add_argument("--video", default="", help="Optional mp4/avi video for video and stream file tests")
    ap.add_argument("--out-dir", default=str(ROOT / "reports" / "phase16"))
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--skip-dual-image", action="store_true")
    ap.add_argument("--skip-video", action="store_true")
    ap.add_argument("--skip-stream", action="store_true")
    ap.add_argument("--skip-registry", action="store_true")
    ap.add_argument("--include-camera-tests", action="store_true", help="Run camera-dependent stream metrics test")
    ap.add_argument("--stream-include", default="file", help="file, camera, rtsp, or comma-separated. Default file only.")
    ap.add_argument("--camera-id", type=int, default=0)
    ap.add_argument("--invalid-camera-id", type=int, default=99)
    ap.add_argument("--rtsp-url", default="")
    args = ap.parse_args()

    out_dir = Path(args.out_dir) / now_stamp()
    out_dir.mkdir(parents=True, exist_ok=True)

    detect_image = Path(args.detect_image)
    obb_image = Path(args.obb_image)
    if not detect_image.exists():
        raise FileNotFoundError(f"detect image not found: {detect_image}")
    if not obb_image.exists():
        raise FileNotFoundError(f"obb image not found: {obb_image}")

    video_path = ensure_smoke_video(args.video or None, out_dir, detect_image)

    results: List[StepResult] = []

    if not args.skip_dual_image:
        results.append(run_step(
            "01_phase12_detect_obb_smoke",
            [sys.executable, "tools/phase12_dual_service_test.py",
             "--detect-url", args.detect_url,
             "--obb-url", args.obb_url,
             "--detect-image", str(detect_image),
             "--obb-image", str(obb_image),
             "--out-dir", str(out_dir / "phase12"),
             "--timeout", "180"],
            out_dir,
            args.timeout,
        ))

    if not args.skip_video:
        if video_path is None:
            results.append(StepResult(name="02_phase13_video_smoke", success=True, skipped=True, reason="no video and cannot generate smoke video"))
            results.append(StepResult(name="03_phase13_5_video_cancel", success=True, skipped=True, reason="no video and cannot generate smoke video"))
        else:
            results.append(run_step(
                "02_phase13_video_smoke",
                [sys.executable, "tools/phase13_video_smoke_test.py",
                 "--url", args.video_url,
                 "--video", str(video_path),
                 "--timeout", "600",
                 "--download", str(out_dir / "phase13_result.mp4")],
                out_dir,
                args.timeout,
            ))
            results.append(run_step(
                "03_phase13_5_video_cancel",
                [sys.executable, "tools/phase13_5_video_cancel_test.py",
                 "--url", args.video_url,
                 "--video", str(video_path),
                 "--timeout", "180",
                 "--allow-fast-done"],
                out_dir,
                args.timeout,
            ))

    if not args.skip_stream:
        if "file" in {x.strip().lower() for x in args.stream_include.split(",") if x.strip()} and video_path is None:
            results.append(StepResult(name="04_phase14_stream_source_matrix", success=True, skipped=True, reason="file stream requested but no video available"))
        else:
            cmd = [sys.executable, "tools/phase14_stream_source_matrix_test.py",
                   "--url", args.stream_url,
                   "--include", args.stream_include,
                   "--camera-id", str(args.camera_id),
                   "--wait-seconds", "30",
                   "--run-seconds", "3",
                   "--snapshot-dir", str(out_dir / "stream_snapshots")]
            if video_path is not None:
                cmd += ["--file-path", str(video_path)]
            if args.rtsp_url:
                cmd += ["--rtsp-url", args.rtsp_url]
            results.append(run_step("04_phase14_stream_source_matrix", cmd, out_dir, args.timeout))

        if args.include_camera_tests:
            results.append(run_step(
                "05_phase14_5_stream_metrics_check",
                [sys.executable, "tools/phase14_5_stream_metrics_check.py",
                 "--url", args.stream_url,
                 "--camera-id", str(args.camera_id),
                 "--invalid-camera-id", str(args.invalid_camera_id),
                 "--run-seconds", "3",
                 "--wait-seconds", "40"],
                out_dir,
                args.timeout,
            ))

    if not args.skip_registry:
        registry_cases = [
            ("06_registry_detect", args.detect_url, "detect", "image_async", "image_detect_gpu0"),
            ("07_registry_obb", args.obb_url, "obb", "image_async", "image_obb_gpu0"),
            ("08_registry_video", args.video_url, "video", "video_file", "video_detect_gpu0"),
            ("09_registry_stream", args.stream_url, "stream", "live_stream", "stream_detect_gpu0"),
        ]
        for name, url, model, task_kind, worker_group in registry_cases:
            results.append(run_step(
                name,
                [sys.executable, "tools/phase15_worker_registry_test.py",
                 "--service", f"{model}={url}",
                 "--model", model,
                 "--task-kind", task_kind,
                 "--worker-group", worker_group,
                 "--json-out", str(out_dir / f"{name}.json")],
                out_dir,
                120,
            ))

    ok = all(bool(r.get("success")) for r in results)
    summary = {
        "phase": "phase16_full_regression",
        "success": ok,
        "timestamp": now_stamp(),
        "root": str(ROOT),
        "out_dir": str(out_dir),
        "results": results,
    }
    summary_path = out_dir / "phase16_full_regression_summary.json"
    text_path = out_dir / "phase16_full_regression_summary.txt"
    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

    lines = [
        "Phase 16 Full Regression Summary",
        f"success={ok}",
        f"out_dir={out_dir}",
        "",
    ]
    for r in results:
        status = "SKIP" if r.get("skipped") else ("PASS" if r.get("success") else "FAIL")
        lines.append(f"[{status}] {r.get('name')} duration={r.get('duration_seconds', '-')}")
        if r.get("reason"):
            lines.append(f"       reason={r.get('reason')}")
    text_path.write_text("\n".join(lines), encoding="utf-8")

    print("\n" + "=" * 88)
    print(text_path.read_text(encoding="utf-8"))
    print("=" * 88)
    print(f"JSON report: {summary_path}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
