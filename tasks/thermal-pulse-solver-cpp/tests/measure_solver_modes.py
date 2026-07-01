#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import os
import subprocess
import tempfile
import time
from pathlib import Path

import test_thermal


MODES = [
    ("reference", "reference", "adaptive"),
    ("brute_overresolve", "brute_overresolve", "65536"),
    ("coarse_dt", "coarse_dt", "128"),
    ("explicit", "explicit", "4096"),
]

GRID_LABEL = os.environ.get("THERMAL_GRID_LABEL", "solver-selected")


def mode_filter() -> list[tuple[str, str, str]]:
    raw = os.environ.get("THERMAL_MODE_FILTER")
    if not raw:
        return MODES
    wanted = {part.strip() for part in raw.split(",") if part.strip()}
    return [mode for mode in MODES if mode[0] in wanted or mode[1] in wanted]


def run_instance_lenient(binary: Path, instance: dict[str, float], work: Path, deadline: float, mode: str) -> dict[str, object]:
    idx = int(instance["instance"])
    freq = float(instance["freq"])
    sharp = float(instance.get("sharp", 36.0))
    run_dir = work / f"{mode}_instance_{idx}"
    run_dir.mkdir()
    queries = test_thermal.make_queries(idx)
    truth = [test_thermal.exact_temp(freq, p["x"], p["y"], p["t"], sharp) for p in queries]
    inp = run_dir / "queries.json"
    out = run_dir / "predictions.json"
    inp.write_text(json.dumps({"points": queries}, separators=(",", ":")) + "\n", encoding="utf-8")
    env = {
        "PATH": "/usr/local/bin:/usr/bin:/bin",
        "THERMAL_INSTANCE": str(idx),
        "THERMAL_SOLVER_MODE": mode,
        "HOME": "/tmp",
    }
    remaining = max(0.0, deadline - time.monotonic())
    started = time.monotonic()
    if remaining <= 0.0:
        return {"instance": idx, "freq": freq, "runtime_sec": 0.0, "rel_error": None, "status": "timeout"}
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
    except subprocess.TimeoutExpired:
        return {
            "instance": idx,
            "freq": freq,
            "runtime_sec": time.monotonic() - started,
            "rel_error": None,
            "status": "timeout",
        }
    runtime = time.monotonic() - started
    if proc.returncode != 0:
        return {
            "instance": idx,
            "freq": freq,
            "runtime_sec": runtime,
            "rel_error": None,
            "status": "error",
            "stderr": proc.stderr[-500:],
        }
    try:
        pred = test_thermal.validate_output(out, len(queries))
        rel = test_thermal.relative_error(pred, truth)
    except Exception as exc:
        return {
            "instance": idx,
            "freq": freq,
            "runtime_sec": runtime,
            "rel_error": None,
            "status": "bad-output",
            "stderr": str(exc),
        }
    status = "pass" if math.isfinite(rel) and rel <= test_thermal.REL_ERROR_THRESHOLD else "fail"
    return {
        "instance": idx,
        "freq": freq,
        "runtime_sec": runtime,
        "rel_error": rel,
        "status": status,
    }


def main() -> int:
    rows = []
    with tempfile.TemporaryDirectory(prefix="thermal-mode-measure-") as tmp_s:
        tmp = Path(tmp_s)
        binary = test_thermal.compile_agent(tmp)
        for label, mode, nt_label in mode_filter():
            started = time.monotonic()
            deadline = started + test_thermal.SHARED_BUDGET_SEC
            instances = []
            for instance in test_thermal.INSTANCES:
                row = run_instance_lenient(binary, instance, tmp, deadline, mode)
                instances.append(row)
                if row["status"] == "timeout":
                    break
            total = min(time.monotonic() - started, test_thermal.SHARED_BUDGET_SEC)
            reward = 1.0 if len(instances) == len(test_thermal.INSTANCES) and all(r["status"] == "pass" for r in instances) else 0.0
            rows.append(
                {
                    "solver": label,
                    "instances": len(test_thermal.INSTANCES),
                    "grid": GRID_LABEL,
                    "nt": nt_label,
                    "total_wall_clock": total,
                    "per_instance": instances,
                    "status": "pass" if reward == 1.0 else "fail",
                    "reward": reward,
                }
            )
    print("| solver | instances | grid | Nt | total wall-clock | per-instance error | status | reward |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for row in rows:
        errors = []
        for inst in row["per_instance"]:
            err = inst["rel_error"]
            if err is None:
                errors.append(f"i{inst['instance']}:{inst['status']}")
            else:
                errors.append(f"i{inst['instance']}:{err:.6g}")
        print(
            "| {solver} | {instances} | {grid} | {nt} | {total:.3f}s | {errors} | {status} | {reward:.1f} |".format(
                solver=row["solver"],
                instances=row["instances"],
                grid=row["grid"],
                nt=row["nt"],
                total=row["total_wall_clock"],
                errors=", ".join(errors),
                status=row["status"],
                reward=row["reward"],
            )
        )
    print()
    print(json.dumps(rows, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
