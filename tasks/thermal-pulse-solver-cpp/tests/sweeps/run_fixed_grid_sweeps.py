#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path

from run_overresolve_gate import compile_gate, repo_root, run_case


FIXED_GRID_NT = [4096, 65536, 98304, 131072]
SPATIAL_NX = [192, 256, 320, 384, 448]


def parse_int_list(value):
    return [int(part) for part in value.split(",") if part.strip()]


def print_table(title, rows):
    print(title)
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
    print()


def fixed_grid_cases(nx, nt_values, freq, sharp):
    extra_args = ["--freq", str(freq), "--sharp", str(sharp)]
    return [
        {
            "case": f"brute_nt_{nt}",
            "nx": nx,
            "ny": nx,
            "nt": nt,
            "extra_args": extra_args,
        }
        for nt in nt_values
    ]


def spatial_cases(nx_values, nt, freq, sharp):
    extra_args = ["--freq", str(freq), "--sharp", str(sharp)]
    return [
        {
            "case": f"spatial_{nx}",
            "nx": nx,
            "ny": nx,
            "nt": nt,
            "extra_args": extra_args,
        }
        for nx in nx_values
    ]


def find_crossover(rows, budget):
    for row in rows:
        if row["result"] == "timeout" or row["wall_clock"] >= budget:
            return row
    return None


def first_pass(rows):
    for row in rows:
        if row["result"] == "pass":
            return row
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--budget", type=float, default=180.0)
    parser.add_argument("--tol", type=float, default=5.0e-3)
    parser.add_argument("--nx", type=int, default=320)
    parser.add_argument("--spatial-nt", type=int, default=4096)
    parser.add_argument("--freq", type=float, default=96.0)
    parser.add_argument("--sharp", type=float, default=36.0)
    parser.add_argument("--nt-values", default=",".join(str(nt) for nt in FIXED_GRID_NT))
    parser.add_argument("--spatial-nx-values", default=",".join(str(nx) for nx in SPATIAL_NX))
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    root = repo_root()
    binary = compile_gate(root)
    nt_values = parse_int_list(args.nt_values)
    spatial_nx_values = parse_int_list(args.spatial_nx_values)

    nt_rows = [
        run_case(binary, case, args.budget, args.tol)
        for case in fixed_grid_cases(args.nx, nt_values, args.freq, args.sharp)
    ]
    spatial_rows = [
        run_case(binary, case, args.budget, args.tol)
        for case in spatial_cases(spatial_nx_values, args.spatial_nt, args.freq, args.sharp)
    ]

    print_table(f"Fixed-grid brute Nt sweep at Nx=Ny={args.nx}", nt_rows)
    first = first_pass(nt_rows)
    if first:
        print(
            "First passing Nt: {nt} ({seconds:.3f}s, rel-error {rel:.6g}).".format(
                nt=first["nt"],
                seconds=first["wall_clock"],
                rel=first["relative_error"],
            )
        )
    else:
        print("First passing Nt: none in this sweep.")
    finite_nt_errors = [
        row["relative_error"]
        for row in nt_rows
        if row["relative_error"] is not None
    ]
    if finite_nt_errors:
        print("Fixed-grid error floor: {:.6g}".format(min(finite_nt_errors)))
    crossover = find_crossover(nt_rows, args.budget)
    if crossover:
        print(
            "Fixed-grid crossover: {case} reached the {budget:.0f}s budget boundary.".format(
                case=crossover["case"],
                budget=args.budget,
            )
        )
    else:
        print("Fixed-grid crossover: not reached by this sweep.")
    if any(row["nt"] >= 65536 and row["result"] == "pass" for row in nt_rows):
        print("Leak signal: blind high-Nt ADI at the reference grid can pass under budget.")
    print()

    print_table(f"Spatial error sweep at Nt={args.spatial_nt}", spatial_rows)
    finite_errors = [
        row["relative_error"]
        for row in spatial_rows
        if row["relative_error"] is not None
    ]
    if finite_errors:
        print(
            "Spatial error range: min={:.6g}, max={:.6g}".format(
                min(finite_errors),
                max(finite_errors),
            )
        )

    if args.json_out:
        args.json_out.write_text(
            json.dumps({"fixed_grid_nt": nt_rows, "spatial": spatial_rows}, indent=2) + "\n"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
