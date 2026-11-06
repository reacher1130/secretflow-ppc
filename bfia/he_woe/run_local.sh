#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
cd "${SCRIPT_DIR}"

"${PYTHON_BIN}" alice.py "$@" &
ALICE_PID=$!
"${PYTHON_BIN}" bob.py "$@" &
BOB_PID=$!

trap 'kill "${ALICE_PID}" "${BOB_PID}" 2>/dev/null || true' EXIT
wait "${ALICE_PID}"
wait "${BOB_PID}"
