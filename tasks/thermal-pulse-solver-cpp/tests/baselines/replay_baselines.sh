#!/usr/bin/env bash
set -u -o pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
task_dir="$(cd "$script_dir/../.." && pwd)"
image="${THERMAL_VERIFIER_IMAGE:-thermal-pulse-verifier}"

run_with_docker=0
if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  run_with_docker=1
fi

if [[ "$run_with_docker" -eq 1 ]]; then
  docker build -f "$task_dir/tests/Dockerfile" -t "$image" "$task_dir/tests" || exit 2
else
  echo "docker unavailable; using local verifier fallback" >&2
fi

for baseline in "$script_dir"/*; do
  [[ -d "$baseline" ]] || continue
  [[ -f "$baseline/solution.cpp" ]] || continue
  name="$(basename "$baseline")"
  echo "== baseline: $name =="
  if [[ "$run_with_docker" -eq 1 ]]; then
    if docker run --rm -v "$baseline:/app:ro" "$image"; then
      echo "baseline=$name reward=1.0"
    else
      echo "baseline=$name reward=0.0"
    fi
  else
    if APP_DIR="$baseline" TESTS_DIR="$task_dir/tests" python3 "$task_dir/tests/test_thermal.py"; then
      echo "baseline=$name reward=1.0"
    else
      echo "baseline=$name reward=0.0"
    fi
  fi
done
