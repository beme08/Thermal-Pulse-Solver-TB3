#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import os
import random
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


APP_DIR = Path(os.environ.get("APP_DIR", "/app"))
TESTS_DIR = Path(os.environ.get("TESTS_DIR", Path(__file__).resolve().parent))
REPORT_PATH = Path(os.environ.get("THERMAL_REPORT_PATH", "/tmp/thermal_pulse_report.json"))
SHARED_BUDGET_SEC = float(os.environ.get("THERMAL_SHARED_BUDGET_SEC", "180"))
REL_ERROR_THRESHOLD = 5.0e-3

ALL_INSTANCES = [
    {"instance": 0, "freq": 96.0, "reference_nt": 4096, "sharp": 36.0},
    {"instance": 1, "freq": 192.0, "reference_nt": 8192, "sharp": 36.0},
    {"instance": 2, "freq": 128.0, "reference_nt": 4096, "sharp": 640.0},
]


def selected_instances() -> list[dict[str, float]]:
    raw_ids = os.environ.get("THERMAL_INSTANCE_IDS")
    if raw_ids:
        ids = {int(part.strip()) for part in raw_ids.split(",") if part.strip()}
        return [instance for instance in ALL_INSTANCES if int(instance["instance"]) in ids]
    count = int(os.environ.get("THERMAL_INSTANCE_COUNT", "3"))
    if count < 1 or count > len(ALL_INSTANCES):
        raise ValueError(f"THERMAL_INSTANCE_COUNT must be 1..{len(ALL_INSTANCES)}")
    return ALL_INSTANCES[:count]


INSTANCES = selected_instances()


def compile_agent(work: Path) -> Path:
    src = APP_DIR / "solution.cpp"
    if not src.exists():
        raise AssertionError(f"missing submitted source: {src}")
    build_src = work / "solution.cpp"
    shutil.copy2(src, build_src)
    shutil.copy2(TESTS_DIR / "thermal_oracle.hpp", work / "thermal_oracle.hpp")
    binary = work / "solver"
    cmd = [
        "g++",
        "-O3",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-pedantic",
        str(build_src),
        str(TESTS_DIR / "thermal_oracle_impl.cpp"),
        "-o",
        str(binary),
    ]
    result = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        raise AssertionError(f"compile failed:\n{result.stdout}")
    return binary


def temporal(freq: float, t: float) -> float:
    return (1.0 + 0.05 * t) * (1.0 + 0.75 * math.sin(2.0 * math.pi * freq * t))


def spatial(x: float, y: float, sharp: float = 36.0) -> float:
    cx = 0.43
    cy = 0.58
    shape_b = 0.25
    return (
        math.sin(math.pi * x)
        * math.sin(math.pi * y)
        * (1.0 + shape_b * math.cos(2.0 * math.pi * x) * math.cos(2.0 * math.pi * y))
        * math.exp(-sharp * ((x - cx) ** 2 + (y - cy) ** 2))
    )


def exact_temp(freq: float, x: float, y: float, t: float, sharp: float = 36.0) -> float:
    return spatial(x, y, sharp) * temporal(freq, t)


def make_queries(instance: int, count: int = 768) -> list[dict[str, float]]:
    rng = random.Random(20260630 + 97 * instance)
    points: list[dict[str, float]] = []
    fixed_times = [0.137, 0.333, 0.591, 0.827]
    for t in fixed_times:
        for x, y in [(0.21, 0.35), (0.43, 0.58), (0.67, 0.72), (0.81, 0.24)]:
            points.append({"x": x, "y": y, "t": t})
    while len(points) < count:
        points.append(
            {
                "x": rng.uniform(0.025, 0.975),
                "y": rng.uniform(0.025, 0.975),
                "t": rng.uniform(0.0, 1.0),
            }
        )
    return points


def validate_output(path: Path, expected_len: int) -> list[float]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if set(payload) != {"temperatures"}:
        raise AssertionError(f"output must contain only temperatures, got keys {sorted(payload)}")
    values = payload["temperatures"]
    if not isinstance(values, list) or len(values) != expected_len:
        raise AssertionError("temperature array length mismatch")
    out = []
    for value in values:
        if not isinstance(value, (int, float)) or not math.isfinite(float(value)):
            raise AssertionError(f"non-finite temperature: {value!r}")
        out.append(float(value))
    return out


def relative_error(pred: list[float], truth: list[float]) -> float:
    err2 = sum((a - b) ** 2 for a, b in zip(pred, truth))
    ref2 = max(sum(v * v for v in truth), 1.0e-300)
    return math.sqrt(err2 / ref2)


def run_instance(binary: Path, instance: dict[str, float], work: Path, deadline: float) -> dict[str, object]:
    idx = int(instance["instance"])
    freq = float(instance["freq"])
    run_dir = work / f"instance_{idx}"
    run_dir.mkdir()
    queries = make_queries(idx)
    sharp = float(instance.get("sharp", 36.0))
    truth = [exact_temp(freq, p["x"], p["y"], p["t"], sharp) for p in queries]
    inp = run_dir / "queries.json"
    out = run_dir / "predictions.json"
    inp.write_text(json.dumps({"points": queries}, separators=(",", ":")) + "\n", encoding="utf-8")
    env = {
        "PATH": "/usr/local/bin:/usr/bin:/bin",
        "THERMAL_INSTANCE": str(idx),
        "HOME": "/tmp",
    }
    if os.environ.get("THERMAL_SOLVER_MODE"):
        env["THERMAL_SOLVER_MODE"] = os.environ["THERMAL_SOLVER_MODE"]
    remaining = max(0.0, deadline - time.monotonic())
    if remaining <= 0.0:
        raise AssertionError("shared verifier budget exhausted before instance run")
    started = time.monotonic()
    try:
        proc = subprocess.run(
            [str(binary), str(inp), str(out)],
            cwd=run_dir,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=remaining,
        )
    except subprocess.TimeoutExpired as exc:
        return {
            "instance": idx,
            "freq": freq,
            "runtime_sec": time.monotonic() - started,
            "timed_out": True,
            "returncode": None,
            "stdout": exc.stdout or "",
            "stderr": exc.stderr or "",
            "rel_error": None,
        }
    runtime = time.monotonic() - started
    if proc.returncode != 0:
        raise AssertionError(
            f"solver failed for instance {idx} rc={proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    pred = validate_output(out, len(queries))
    rel = relative_error(pred, truth)
    if rel > REL_ERROR_THRESHOLD:
        raise AssertionError(f"instance {idx} relative error {rel:.6g} exceeds {REL_ERROR_THRESHOLD:.6g}")
    return {
        "instance": idx,
        "freq": freq,
        "runtime_sec": runtime,
        "timed_out": False,
        "returncode": proc.returncode,
        "rel_error": rel,
        "query_count": len(queries),
    }


def main() -> int:
    started = time.monotonic()
    deadline = started + SHARED_BUDGET_SEC
    report: dict[str, object] = {
        "task": "thermal-pulse-solver-cpp",
        "shared_budget_sec": SHARED_BUDGET_SEC,
        "threshold": REL_ERROR_THRESHOLD,
        "instances": [],
    }
    with tempfile.TemporaryDirectory(prefix="thermal-pulse-verifier-") as tmp_s:
        work = Path(tmp_s)
        binary = compile_agent(work)
        for instance in INSTANCES:
            result = run_instance(binary, instance, work, deadline)
            report["instances"].append(result)
            if result.get("timed_out"):
                raise AssertionError(f"instance {result['instance']} timed out")
        report["total_runtime_sec"] = time.monotonic() - started
    REPORT_PATH.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"verifier failure: {exc}", file=sys.stderr)
        raise
