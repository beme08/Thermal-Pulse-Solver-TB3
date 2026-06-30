#!/usr/bin/env bash
set -euo pipefail

export PYTHONDONTWRITEBYTECODE=1
TESTS_DIR="${TESTS_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
LOG_DIR="${LOG_DIR:-/logs/verifier}"
REPORT_PATH="${THERMAL_REPORT_PATH:-/tmp/thermal_pulse_report.json}"
export TESTS_DIR
export THERMAL_REPORT_PATH="$REPORT_PATH"
export THERMAL_SHARED_BUDGET_SEC="${THERMAL_SHARED_BUDGET_SEC:-180}"

if ! mkdir -p "$LOG_DIR" 2>/dev/null; then
    LOG_DIR="/tmp/thermal_pulse_logs"
    mkdir -p "$LOG_DIR"
fi
REWARD_FILE="${LOG_DIR}/reward.txt"
trap 'status=$?; [ -f "$REWARD_FILE" ] || echo "0.0" > "$REWARD_FILE"; exit "$status"' EXIT

set +e
python3 "$TESTS_DIR/test_thermal.py" 2>&1 | tee "$LOG_DIR/thermal_verifier.log"
VERIFY_RC=${PIPESTATUS[0]}
set -e

if [ -f "$REPORT_PATH" ]; then
    cp "$REPORT_PATH" "$LOG_DIR/thermal_report.json"
fi

if [ "$VERIFY_RC" -eq 0 ]; then
    echo "1.0" > "$REWARD_FILE"
    echo ">> verifier reward: 1.0"
    exit 0
fi

echo "0.0" > "$REWARD_FILE"
echo ">> verifier reward: 0.0 (thermal=${VERIFY_RC})"
exit 1
