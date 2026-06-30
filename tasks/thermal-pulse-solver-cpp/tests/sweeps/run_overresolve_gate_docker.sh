#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
BUDGET="${1:-180}"
IMAGE="thermal-pulse-solver-cpp-gate"

docker build -f "$ROOT/tasks/thermal-pulse-solver-cpp/tests/sweeps/Dockerfile" -t "$IMAGE" "$ROOT"
docker run --rm "$IMAGE" python3 /app/tasks/thermal-pulse-solver-cpp/tests/sweeps/run_overresolve_gate.py --budget "$BUDGET"
