#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


CASES = [
    {"case": "reference", "nx": 320, "ny": 320, "nt": 4096},
    {"case": "coarse_dt", "nx": 320, "ny": 320, "nt": 128},
    {"case": "explicit", "nx": 320, "ny": 320, "nt": 4096},
    {"case": "brute_overresolve", "nx": 480, "ny": 480, "nt": 65536},
]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[4]


def compile_gate(root: Path) -> Path:
    build_dir = root / "tasks/thermal-pulse-solver-cpp/tests/sweeps/.build"
    build_dir.mkdir(parents=True, exist_ok=True)
    binary = build_dir / "thermal_gate"
    source = root / "tasks/thermal-pulse-solver-cpp/tests/sweeps/thermal_gate.cpp"
    compiler = os.environ.get("CXX", "g++")
    cmd = [
        compiler,
        "-O3",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-pedantic",
        str(source),
        "-o",
        str(binary),
    ]
    subprocess.run(cmd, cwd=root, check=True)
    return binary


def run_case(binary: Path, case: dict, budget: float, tol: float) -> dict:
    cmd = [
        str(binary),
        "--case",
        case["case"],
        "--nx",
        str(case["nx"]),
        "--ny",
        str(case["ny"]),
        "--nt",
        str(case["nt"]),
        "--tol",
        str(tol),
    ]
    cmd.extend(case.get("extra_args", []))
    started = time.monotonic()
    try:
        completed = subprocess.run(
            cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=budget,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return {
            **case,
            "wall_clock": budget,
            "relative_error": None,
            "solver_status": "timeout",
            "result": "timeout",
            "command": " ".join(cmd),
        }

    wall = time.monotonic() - started
    if completed.returncode != 0:
        return {
            **case,
            "wall_clock": wall,
            "relative_error": None,
            "solver_status": "error",
            "result": "fail",
            "stderr": completed.stderr.strip(),
            "command": " ".join(cmd),
        }

    try:
        payload = json.loads(completed.stdout.strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError) as exc:
        return {
            **case,
            "wall_clock": wall,
            "relative_error": None,
            "solver_status": "bad-output",
            "result": "fail",
            "stderr": f"{exc}: {completed.stdout!r}",
            "command": " ".join(cmd),
        }

    rel = payload.get("relative_error")
    status = payload.get("status", "unknown")
    if status == "ok" and rel is not None and rel <= tol:
        result = "pass"
    elif status == "timeout":
        result = "timeout"
    else:
        result = "fail"

    return {
        **case,
        "wall_clock": wall,
        "relative_error": rel,
        "solver_seconds": payload.get("seconds"),
        "solver_status": status,
        "result": result,
        "max_abs": payload.get("max_abs"),
        "command": " ".join(cmd),
    }


def print_table(rows):
    headers = ["case", "Nx", "Ny", "Nt", "wall-clock", "rel-error", "status", "result"]
    print("| " + " | ".join(headers) + " |")
    print("| " + " | ".join(["---"] * len(headers)) + " |")
    for row in rows:
        rel = row["relative_error"]
        rel_text = "n/a" if rel is None else f"{rel:.6g}"
        print(
            "| {case} | {nx} | {ny} | {nt} | {wall:.3f}s | {rel} | {status} | {result} |".format(
                case=row["case"],
                nx=row["nx"],
                ny=row["ny"],
                nt=row["nt"],
                wall=row["wall_clock"],
                rel=rel_text,
                status=row["solver_status"],
                result=row["result"],
            )
        )


def gate_verdict(rows):
    by_case = {row["case"]: row for row in rows}
    reference = by_case["reference"]
    coarse = by_case["coarse_dt"]
    explicit = by_case["explicit"]
    brute = by_case["brute_overresolve"]

    reference_ok = reference["result"] == "pass"
    coarse_wrong = coarse["result"] == "fail" and coarse["solver_status"] == "ok"
    explicit_loses = explicit["result"] in {"fail", "timeout"}
    brute_loses = brute["result"] == "timeout" or (
        brute["result"] == "fail" and brute["wall_clock"] >= reference["wall_clock"]
    )

    if reference_ok and coarse_wrong and explicit_loses and brute_loses:
        return "PASS: reference fits, coarse_dt is silently wrong, explicit loses, brute over-resolve does not win."
    return "FAIL: the over-resolve gate is not established; retune before building the final task."


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--budget", type=float, default=180.0)
    parser.add_argument("--tol", type=float, default=5.0e-3)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    root = repo_root()
    binary = compile_gate(root)
    rows = [run_case(binary, case, args.budget, args.tol) for case in CASES]

    print_table(rows)
    print()
    print(gate_verdict(rows))

    if args.json_out:
        args.json_out.write_text(json.dumps(rows, indent=2) + "\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
