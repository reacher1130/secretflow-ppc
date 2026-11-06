#!/usr/bin/env bash

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-all}"
status=0

case "$MODE" in
  python)
    "$SCRIPT_DIR/run_python_tests.sh" || status=$?
    ;;
  cpp)
    "$SCRIPT_DIR/run_cpp_tests.sh" || status=$?
    ;;
  all)
    "$SCRIPT_DIR/run_python_tests.sh" || status=$?
    "$SCRIPT_DIR/run_cpp_tests.sh" || status=$?
    ;;
  *)
    echo "用法: $0 [python|cpp|all]" >&2
    exit 2
    ;;
esac

exit "$status"
