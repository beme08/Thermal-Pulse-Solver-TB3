#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
BUDGET="${1:-180}"
if [[ $# -gt 0 ]]; then
  shift
fi
IMAGE="thermal-pulse-solver-cpp-gate"

docker build -f "$ROOT/tasks/thermal-pulse-solver-cpp/tests/sweeps/Dockerfile" -t "$IMAGE" "$ROOT"
docker run --rm "$IMAGE" python3 /app/tasks/thermal-pulse-solver-cpp/tests/sweeps/run_multi_instance_sweep.py --budget "$BUDGET" "$@"
