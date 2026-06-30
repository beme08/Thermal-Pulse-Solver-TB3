#!/usr/bin/env python3
import argparse
import json
import time
from pathlib import Path

from run_overresolve_gate import compile_gate, repo_root, run_case


DEFAULT_INSTANCES = "96:4096,192:8192"


def parse_instances(value):
    instances = []
    for raw_part in value.split(","):
        part = raw_part.strip()
        if not part:
            continue
        freq_text, nt_text = part.split(":", 1)
        instances.append({"freq": float(freq_text), "reference_nt": int(nt_text)})
    return instances


def timed_sequence(binary, instances, nx, budget, tol, sharp, brute_nt=None):
    rows = []
    started = time.monotonic()
    timed_out = False
    for index, instance in enumerate(instances, start=1):
        elapsed = time.monotonic() - started
        remaining = max(0.0, budget - elapsed)
        if remaining <= 0.0:
            timed_out = True
            break
        nt = brute_nt if brute_nt is not None else instance["reference_nt"]
        case = {
            "case": "instance_{}_f{}_nt{}".format(index, int(instance["freq"]), nt),
            "nx": nx,
            "ny": nx,
            "nt": nt,
            "extra_args": ["--freq", str(instance["freq"]), "--sharp", str(sharp)],
        }
        row = run_case(binary, case, remaining, tol)
        rows.append(row)
        if row["result"] == "timeout":
            timed_out = True
            break
    total = min(time.monotonic() - started, budget)
    passed_all = len(rows) == len(instances) and all(row["result"] == "pass" for row in rows)
    return {
        "rows": rows,
        "total_seconds": total,
        "timed_out": timed_out,
        "result": "pass" if passed_all else "timeout" if timed_out else "fail",
    }


def print_sequence(title, sequence):
    print(title)
    print("| instance | Nx | Ny | Nt | wall-clock | rel-error | status | result |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for row in sequence["rows"]:
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
    print(
        "Total: {seconds:.3f}s, result={result}, timed_out={timed_out}".format(
            seconds=sequence["total_seconds"],
            result=sequence["result"],
            timed_out=str(sequence["timed_out"]).lower(),
        )
    )
    print()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--budget", type=float, default=180.0)
    parser.add_argument("--tol", type=float, default=5.0e-3)
    parser.add_argument("--nx", type=int, default=320)
    parser.add_argument("--sharp", type=float, default=36.0)
    parser.add_argument("--instances", default=DEFAULT_INSTANCES)
    parser.add_argument("--brute-nt", type=int, default=65536)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    root = repo_root()
    binary = compile_gate(root)
    instances = parse_instances(args.instances)

    print(
        "Budget model: shared {}s wall-clock across all listed instances in one verifier run.".format(
            int(args.budget)
        )
    )
    print("This measurement is not a gate closer if the final harness grants a fresh budget per instance.")
    print()

    reference = timed_sequence(binary, instances, args.nx, args.budget, args.tol, args.sharp)
    brute = timed_sequence(
        binary,
        instances,
        args.nx,
        args.budget,
        args.tol,
        args.sharp,
        brute_nt=args.brute_nt,
    )

    print_sequence("Reference multi-instance sequence", reference)
    print_sequence(f"Blind brute multi-instance sequence at Nt={args.brute_nt}", brute)

    if reference["result"] == "pass" and brute["result"] == "timeout":
        print("Shared-budget multi-instance separation: PASS.")
    else:
        print("Shared-budget multi-instance separation: FAIL.")

    if args.json_out:
        args.json_out.write_text(
            json.dumps({"reference": reference, "brute": brute}, indent=2) + "\n"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
