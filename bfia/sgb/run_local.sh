#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
LOG_LEVEL="${LOG_LEVEL:-debug}"

alice_pid=""
bob_pid=""

cleanup() {
  if [[ -n "${alice_pid}" ]] && kill -0 "${alice_pid}" 2>/dev/null; then
    kill "${alice_pid}" 2>/dev/null || true
  fi
  if [[ -n "${bob_pid}" ]] && kill -0 "${bob_pid}" 2>/dev/null; then
    kill "${bob_pid}" 2>/dev/null || true
  fi
}

trap cleanup INT TERM EXIT

"${PYTHON_BIN}" "${SCRIPT_DIR}/alice.py" --log-level "${LOG_LEVEL}" "$@" &
alice_pid=$!

"${PYTHON_BIN}" "${SCRIPT_DIR}/bob.py" --log-level "${LOG_LEVEL}" "$@" &
bob_pid=$!

alice_status=0
bob_status=0
wait "${alice_pid}" || alice_status=$?
wait "${bob_pid}" || bob_status=$?

if [[ "${alice_status}" -ne 0 || "${bob_status}" -ne 0 ]]; then
  echo "BFIA SGB failed: alice=${alice_status}, bob=${bob_status}" >&2
  exit 1
fi

echo "BFIA SGB completed: alice=${alice_status}, bob=${bob_status}"
