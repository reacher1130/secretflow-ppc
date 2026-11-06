#!/usr/bin/env bash

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RESULT_ROOT="${BFIA_RESULT_ROOT:-$SCRIPT_DIR/results}"
RUN_ID="${BFIA_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
RUN_DIR="$RESULT_ROOT/python/$RUN_ID"
TIMEOUT_SEC="${BFIA_TIMEOUT_SEC:-180}"
LOG_LEVEL="${BFIA_LOG_LEVEL:-info}"
PYTHON_ALICE_IP="${BFIA_PYTHON_ALICE_IP:-127.0.0.1}"
if [[ -n "${BFIA_PYTHON_BOB_IP:-}" ]]; then
  PYTHON_BOB_IP="$BFIA_PYTHON_BOB_IP"
else
  default_interface="$(route -n get default 2>/dev/null | awk '/interface:/{print $2; exit}' || true)"
  PYTHON_BOB_IP=""
  if command -v ip >/dev/null 2>&1; then
    PYTHON_BOB_IP="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i=1;i<=NF;i++) if ($i == "src") {print $(i+1); exit}}' || true)"
  fi
  if [[ -z "$PYTHON_BOB_IP" && -n "$default_interface" ]] && command -v ifconfig >/dev/null 2>&1; then
    PYTHON_BOB_IP="$(ifconfig "$default_interface" 2>/dev/null | awk '$1 == "inet" && $2 != "127.0.0.1" {print $2; exit}' || true)"
  fi
  if [[ -z "$PYTHON_BOB_IP" ]] && command -v ifconfig >/dev/null 2>&1; then
    PYTHON_BOB_IP="$(ifconfig 2>/dev/null | awk '$1 == "inet" && $2 != "127.0.0.1" {print $2; exit}' || true)"
  fi
  if [[ -z "$PYTHON_BOB_IP" ]]; then
    PYTHON_BOB_IP="$PYTHON_ALICE_IP"
  fi
fi
if [[ "$PYTHON_ALICE_IP" == "$PYTHON_BOB_IP" ]]; then
  NETWORK_SCOPE="same IPv4 address with distinct ports; same-host dual process"
else
  NETWORK_SCOPE="two distinct IPv4 addresses; same-host wildcard listeners"
fi

mkdir -p "$RUN_DIR"
ln -sfn "$RUN_DIR" "$RESULT_ROOT/python/latest"

if [[ -n "${PYTHON_BIN:-}" ]]; then
  PYTHON_BIN="$PYTHON_BIN"
elif command -v pyenv >/dev/null 2>&1 && \
     PYENV_VERSION=secretflow pyenv which python >/dev/null 2>&1; then
  PYTHON_BIN="$(PYENV_VERSION=secretflow pyenv which python)"
else
  PYTHON_BIN="$(command -v python3 || true)"
fi

if [[ -z "$PYTHON_BIN" || ! -x "$PYTHON_BIN" ]]; then
  echo "PYTHON_STATUS=FAIL reason=python-not-found"
  exit 2
fi

PYTHONPATH_VALUE="$REPO_ROOT:$REPO_ROOT/interconnection"
if [[ -n "${PYTHONPATH:-}" ]]; then
  PYTHONPATH_VALUE="$PYTHONPATH_VALUE:$PYTHONPATH"
fi

if ! PYTHONPATH="$PYTHONPATH_VALUE" "$PYTHON_BIN" -c \
  'import secretflow, spu, heu, interconnection, sklearn' >/dev/null 2>"$RUN_DIR/environment.log"; then
  echo "PYTHON_STATUS=FAIL reason=dependency-import-failed"
  echo "python=$PYTHON_BIN" | tee "$RUN_DIR/summary.txt"
  cat "$RUN_DIR/environment.log"
  exit 2
fi

echo "python=$PYTHON_BIN" >"$RUN_DIR/summary.txt"
echo "python_version=$("$PYTHON_BIN" --version 2>&1)" >>"$RUN_DIR/summary.txt"
echo "timeout_sec=$TIMEOUT_SEC" >>"$RUN_DIR/summary.txt"
echo "bfia_protocol=BFIA" >>"$RUN_DIR/summary.txt"
echo "alice_endpoint=$PYTHON_ALICE_IP" >>"$RUN_DIR/summary.txt"
echo "bob_endpoint=$PYTHON_BOB_IP" >>"$RUN_DIR/summary.txt"
echo "network_scope=$NETWORK_SCOPE" >>"$RUN_DIR/summary.txt"
echo "通信协议=BFIA互联互通协议" >>"$RUN_DIR/summary.txt"
echo "真实通信端点=Alice(${PYTHON_ALICE_IP}) <-> Bob(${PYTHON_BOB_IP})" >>"$RUN_DIR/summary.txt"

overall_status=0

run_pair() {
  local algo="$1"
  local alice_port="$2"
  local bob_port="$3"
  local pair_dir="$RUN_DIR/$algo"
  local alice_dir="$pair_dir/alice"
  local bob_dir="$pair_dir/bob"
  local alice_log="$pair_dir/alice.stdout.log"
  local bob_log="$pair_dir/bob.stdout.log"
  local alice_pid=""
  local bob_pid=""
  local alice_status=0
  local bob_status=0
  local timed_out=0
  local started_at
  local elapsed
  local alice_marker=0
  local bob_marker=0
  local alice_auc="unavailable"
  local bob_auc="unavailable"
  local alice_report="$pair_dir/alice-report.json"
  local bob_report="$pair_dir/bob-report.json"
  local alice_address="$PYTHON_ALICE_IP:$alice_port"
  local bob_address="$PYTHON_BOB_IP:$bob_port"
  local alice_listen="0.0.0.0:$alice_port"
  local bob_listen="0.0.0.0:$bob_port"
  local alice_send_count=0
  local alice_recv_count=0
  local bob_send_count=0
  local bob_recv_count=0
  local network_evidence=0

  mkdir -p "$alice_dir" "$bob_dir"

  echo "PYTHON_TEST_START algorithm=$algo protocol=BFIA Alice=$alice_address Bob=$bob_address"
  echo "BFIA协议测试：算法=${algo}；真实通信端点=Alice(${alice_address}) <-> Bob(${bob_address})；本地监听=Alice(${alice_listen}),Bob(${bob_listen})"
  (
    cd "$alice_dir" || exit 1
    BFIA_ALGORITHM="$algo" BFIA_SELF_PARTY=alice BFIA_RESULT_FILE="$alice_report" PYTHONPATH="$PYTHONPATH_VALUE" \
      PYTHONUNBUFFERED=1 "$PYTHON_BIN" "$SCRIPT_DIR/$algo/alice.py" \
      --alice-address "$alice_address" \
      --bob-address "$bob_address" \
      --alice-listen "$alice_listen" \
      --bob-listen "$bob_listen" \
      --log-level "$LOG_LEVEL"
  ) >"$alice_log" 2>&1 &
  alice_pid=$!

  (
    cd "$bob_dir" || exit 1
    BFIA_ALGORITHM="$algo" BFIA_SELF_PARTY=bob BFIA_RESULT_FILE="$bob_report" PYTHONPATH="$PYTHONPATH_VALUE" \
      PYTHONUNBUFFERED=1 "$PYTHON_BIN" "$SCRIPT_DIR/$algo/bob.py" \
      --alice-address "$alice_address" \
      --bob-address "$bob_address" \
      --alice-listen "$alice_listen" \
      --bob-listen "$bob_listen" \
      --log-level "$LOG_LEVEL"
  ) >"$bob_log" 2>&1 &
  bob_pid=$!

  started_at="$(date +%s)"
  while kill -0 "$alice_pid" 2>/dev/null || kill -0 "$bob_pid" 2>/dev/null; do
    elapsed=$(( $(date +%s) - started_at ))
    if (( elapsed >= TIMEOUT_SEC )); then
      timed_out=1
      kill "$alice_pid" "$bob_pid" 2>/dev/null || true
      break
    fi
    sleep 1
  done

  wait "$alice_pid" 2>/dev/null || alice_status=$?
  wait "$bob_pid" 2>/dev/null || bob_status=$?

  if rg -q 'BFIA_SGB_STATUS=completed' "$alice_log" 2>/dev/null; then
    alice_marker=1
  fi
  if rg -q 'BFIA_SGB_STATUS=completed' "$bob_log" 2>/dev/null; then
    bob_marker=1
  fi
  if [[ "$algo" == "he_woe" ]]; then
    if rg -q 'BFIA_HE_WOE_STATUS=completed' "$alice_log" 2>/dev/null; then
      alice_marker=1
    fi
    if rg -q 'BFIA_HE_WOE_STATUS=completed' "$bob_log" 2>/dev/null; then
      bob_marker=1
    fi
  fi

  alice_auc="$(rg -o 'auc: [0-9.eE+-]+' "$alice_log" 2>/dev/null | tail -1 || true)"
  bob_auc="$(rg -o 'auc: [0-9.eE+-]+' "$bob_log" 2>/dev/null | tail -1 || true)"
  [[ -n "$alice_auc" ]] || alice_auc="unavailable"
  [[ -n "$bob_auc" ]] || bob_auc="unavailable"

  alice_send_count="$(rg -c 'BFIA协议实际发送' "$alice_log" 2>/dev/null || echo 0)"
  alice_recv_count="$(rg -c 'BFIA协议实际接收' "$alice_log" 2>/dev/null || echo 0)"
  bob_send_count="$(rg -c 'BFIA协议实际发送' "$bob_log" 2>/dev/null || echo 0)"
  bob_recv_count="$(rg -c 'BFIA协议实际接收' "$bob_log" 2>/dev/null || echo 0)"
  if (( alice_send_count > 0 && alice_recv_count > 0 &&
        bob_send_count > 0 && bob_recv_count > 0 )); then
    network_evidence=1
  fi

  local status="PASS"
  if (( timed_out != 0 || alice_status != 0 || bob_status != 0 ||
        alice_marker == 0 || bob_marker == 0 || network_evidence == 0 )); then
    status="FAIL"
    overall_status=1
  fi

  {
    echo "algorithm=$algo"
    echo "status=$status"
    echo "alice_exit=$alice_status"
    echo "bob_exit=$bob_status"
    echo "timed_out=$timed_out"
    echo "alice_marker=$alice_marker"
    echo "bob_marker=$bob_marker"
    echo "alice_auc=$alice_auc"
    echo "bob_auc=$bob_auc"
    echo "network_evidence=$network_evidence"
    echo "alice_send_count=$alice_send_count"
    echo "alice_recv_count=$alice_recv_count"
    echo "bob_send_count=$bob_send_count"
    echo "bob_recv_count=$bob_recv_count"
    echo "bfia_protocol=BFIA"
    echo "alice_endpoint=$alice_address"
    echo "bob_endpoint=$bob_address"
    echo "alice_listen=$alice_listen"
    echo "bob_listen=$bob_listen"
    echo "network_scope=$NETWORK_SCOPE"
    echo "通信协议=BFIA互联互通协议"
    echo "真实通信端点=Alice($alice_address) <-> Bob($bob_address)"
    echo "alice_log=$alice_log"
    echo "bob_log=$bob_log"
    if [[ "$algo" == "he_woe" ]]; then
      echo "alice_report=$alice_report"
      echo "bob_report=$bob_report"
    fi
  } >"$pair_dir/summary.txt"

  cat "$pair_dir/summary.txt" >>"$RUN_DIR/summary.txt"
  echo "PYTHON_TEST_RESULT algorithm=$algo status=$status alice_exit=$alice_status bob_exit=$bob_status"
}

run_pair sgb 19844 19845
run_pair he_woe 19854 19855

python "$SCRIPT_DIR/collect_protocol_audit.py" "$RUN_DIR" --language python >"$RUN_DIR/protocol-check.log" 2>&1 || overall_status=1
python "$SCRIPT_DIR/validate_python_outputs.py" "$RUN_DIR" >"$RUN_DIR/numerical-check.log" 2>&1 || overall_status=1

echo "PYTHON_STATUS=$([[ $overall_status -eq 0 ]] && echo PASS || echo FAIL)"
echo "result_dir=$RUN_DIR"
exit "$overall_status"
