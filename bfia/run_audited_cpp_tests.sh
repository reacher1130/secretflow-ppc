#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export BFIA_RUN_ID="${BFIA_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
bash "$SCRIPT_DIR/run_cpp_tests.sh"
run="${BFIA_RESULT_ROOT:-$SCRIPT_DIR/results}/cpp/$BFIA_RUN_ID"
IFS=', ' read -r -a algorithms <<<"${BFIA_CPP_ALGOS:-ecdh-psi,ss-lr,ss-xgb,ss-pearson,ss-vif,ss-compare}"
args=("$SCRIPT_DIR/validate_numerics.py" "$run" --public-fixtures --algorithms "${algorithms[@]}" --lr-rate "${BFIA_LR_RATE:-0.0001}" --lr-epochs "${BFIA_LR_EPOCHS:-1}" --xgb-rounds "${BFIA_XGB_ROUNDS:-1}")
if python3 -c 'import numpy' >/dev/null 2>&1; then
  python3 "${args[@]}"
else
  docker run --rm -v "$SCRIPT_DIR:$SCRIPT_DIR" -v "$run:$run" -w "$SCRIPT_DIR" --entrypoint python "${BFIA_VALIDATOR_IMAGE:-ppci/bfia-python:amd64-20260908}" "${args[@]}"
fi
