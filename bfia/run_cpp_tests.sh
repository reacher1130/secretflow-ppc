#!/usr/bin/env bash

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CPP_ROOT="$REPO_ROOT/interconnection-impl"
DATA_ROOT="${BFIA_CPP_DATA_ROOT:-}"

RESULT_ROOT="${BFIA_RESULT_ROOT:-$SCRIPT_DIR/results}"
RUN_ID="${BFIA_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
RUN_DIR="$RESULT_ROOT/cpp/$RUN_ID"
TIMEOUT_SEC="${BFIA_CPP_TIMEOUT_SEC:-300}"
BASE_PORT="${BFIA_CPP_BASE_PORT:-29972}"
SAMPLE_ROWS="${BFIA_CPP_SAMPLE_ROWS:-256}"
CPP_RUNTIME="${BFIA_CPP_RUNTIME:-auto}"
CPP_IMAGE="${BFIA_CPP_IMAGE:-secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/bfia-cpp-runtime:linux-amd64-slim}"
CPP_LEGACY_IMAGE="${BFIA_CPP_LEGACY_IMAGE:-secretflow-registry.cn-hangzhou.cr.aliyuncs.com/secretflow/ss-compare:latest}"
CPP_BINARY_IN_DOCKER="${BFIA_CPP_BINARY_IN_DOCKER:-auto}"
DOCKER_SUBNET="${BFIA_CPP_DOCKER_SUBNET:-172.31.240.0/24}"
DOCKER_ALICE_IP="${BFIA_CPP_DOCKER_ALICE_IP:-172.31.240.2}"
DOCKER_BOB_IP="${BFIA_CPP_DOCKER_BOB_IP:-172.31.240.3}"
CPP_IMAGE_EXPLICIT=0
CPP_IMAGE_FALLBACK_USED=""
if [[ -n "${BFIA_CPP_IMAGE:-}" ]]; then
  CPP_IMAGE_EXPLICIT=1
fi

mkdir -p "$RUN_DIR"
ln -sfn "$RUN_DIR" "$RESULT_ROOT/cpp/latest"

BIN_PATH=""
for candidate in \
  "$CPP_ROOT"/.cache/bazel-home/_bazel_root/*/execroot/interconnection-impl/bazel-out/k8-opt/bin/ic_impl/ic_main \
  "$CPP_ROOT/bazel-bin/ic_impl/ic_main" \
  "$REPO_ROOT/bazel-bin/interconnection-impl/ic_impl/ic_main" \
  "$REPO_ROOT/bazel-bin/ic_impl/ic_main"; do
  if [[ -x "$candidate" ]]; then
    BIN_PATH="$candidate"
    break
  fi
done

DOCKER_AVAILABLE=0
IMAGE_AVAILABLE=0
if command -v docker >/dev/null 2>&1; then
  DOCKER_AVAILABLE=1
  if docker image inspect "$CPP_IMAGE" >/dev/null 2>&1; then
    IMAGE_AVAILABLE=1
  elif (( CPP_IMAGE_EXPLICIT == 0 )) && docker image inspect "$CPP_LEGACY_IMAGE" >/dev/null 2>&1; then
    CPP_IMAGE_FALLBACK_USED="$CPP_LEGACY_IMAGE"
    CPP_IMAGE="$CPP_LEGACY_IMAGE"
    IMAGE_AVAILABLE=1
    echo "BFIA C++ 精简运行镜像未找到，回退到兼容镜像：$CPP_IMAGE" >&2
  fi
fi

RUNTIME=""
case "$CPP_RUNTIME" in
  docker)
    if (( IMAGE_AVAILABLE == 0 )); then
      echo "CPP_STATUS=FAIL reason=docker-image-not-found image=$CPP_IMAGE" | tee "$RUN_DIR/summary.txt"
      exit 2
    fi
    RUNTIME="docker"
    ;;
  binary)
    if [[ -z "$BIN_PATH" ]]; then
      echo "CPP_STATUS=FAIL reason=ic-main-not-found" | tee "$RUN_DIR/summary.txt"
      exit 2
    fi
    RUNTIME="binary"
    ;;
  auto)
    if (( IMAGE_AVAILABLE != 0 )); then
      RUNTIME="docker"
    elif [[ -n "$BIN_PATH" ]]; then
      RUNTIME="binary"
    else
      {
        echo "CPP_STATUS=FAIL reason=no-cpp-runtime"
        echo "docker_image=$CPP_IMAGE"
        echo "binary=$BIN_PATH"
      } | tee "$RUN_DIR/summary.txt"
      exit 2
    fi
    ;;
  *)
    echo "CPP_STATUS=FAIL reason=invalid-runtime runtime=$CPP_RUNTIME" | tee "$RUN_DIR/summary.txt"
    exit 2
    ;;
esac

if [[ "$RUNTIME" == "docker" && "$DOCKER_AVAILABLE" == "0" ]]; then
  echo "CPP_STATUS=FAIL reason=docker-not-found" | tee "$RUN_DIR/summary.txt"
  exit 2
fi

if [[ "$RUNTIME" == "docker" ]]; then
  if [[ "$CPP_BINARY_IN_DOCKER" == "auto" ]]; then
    if [[ -n "$BIN_PATH" ]]; then
      CPP_BINARY_IN_DOCKER=1
    else
      CPP_BINARY_IN_DOCKER=0
    fi
  fi
else
  CPP_BINARY_IN_DOCKER=0
fi

if [[ "$CPP_BINARY_IN_DOCKER" != "0" && "$CPP_BINARY_IN_DOCKER" != "1" ]]; then
  echo "CPP_STATUS=FAIL reason=invalid-binary-in-docker value=$CPP_BINARY_IN_DOCKER" | tee "$RUN_DIR/summary.txt"
  exit 2
fi

if [[ "$RUNTIME" == "docker" && "$CPP_BINARY_IN_DOCKER" == "1" && -z "$BIN_PATH" ]]; then
  echo "CPP_STATUS=FAIL reason=ic-main-not-found-for-docker image=$CPP_IMAGE" | tee "$RUN_DIR/summary.txt"
  exit 2
fi

if [[ -z "$DATA_ROOT" ]]; then
  DATA_ROOT="$RUN_DIR/fixtures"
  python3 "$SCRIPT_DIR/generate_cpp_fixtures.py" "$DATA_ROOT" --rows "$SAMPLE_ROWS" || exit 2
fi

if [[ ! -d "$DATA_ROOT" ]]; then
  echo "CPP_STATUS=FAIL reason=data-directory-not-found path=$DATA_ROOT" | tee "$RUN_DIR/summary.txt"
  exit 2
fi

echo "runtime=$RUNTIME" >"$RUN_DIR/summary.txt"
echo "binary=$BIN_PATH" >>"$RUN_DIR/summary.txt"
echo "image=$CPP_IMAGE" >>"$RUN_DIR/summary.txt"
echo "timeout_sec=$TIMEOUT_SEC" >>"$RUN_DIR/summary.txt"
echo "sample_rows=$SAMPLE_ROWS" >>"$RUN_DIR/summary.txt"
echo "binary_in_docker=$CPP_BINARY_IN_DOCKER" >>"$RUN_DIR/summary.txt"
if [[ -n "$CPP_IMAGE_FALLBACK_USED" ]]; then
  echo "image_fallback=$CPP_IMAGE_FALLBACK_USED" >>"$RUN_DIR/summary.txt"
fi
echo "bfia_protocol=BFIA" >>"$RUN_DIR/summary.txt"
echo "platform=linux/amd64" >>"$RUN_DIR/summary.txt"

NETWORK=""
NETWORK_CREATED=0
if [[ "$RUNTIME" == "docker" ]]; then
  NETWORK="bfia-cpp-$RUN_ID"
  if ! docker network create --subnet "$DOCKER_SUBNET" "$NETWORK" >"$RUN_DIR/network.log" 2>&1; then
    echo "CPP_STATUS=FAIL reason=docker-network-create-failed" | tee -a "$RUN_DIR/summary.txt"
    cat "$RUN_DIR/network.log"
    exit 2
  fi
  NETWORK_CREATED=1
fi

cleanup() {
  if [[ -n "${ALICE_CONTAINER:-}" ]]; then
    docker rm -f "$ALICE_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ -n "${BOB_CONTAINER:-}" ]]; then
    docker rm -f "$BOB_CONTAINER" >/dev/null 2>&1 || true
  fi
  if (( NETWORK_CREATED != 0 )); then
    docker network rm "$NETWORK" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

ENV_PAIRS=()
EXTRA_ARGS=()
CPP_ALGO=""
ALICE_INPUT=""
BOB_INPUT=""
FEATURE_NUMS=""
FEATURE_SELECT=""
FIELD_NAMES=""
HAS_LABEL_ALICE=false

add_env() {
  ENV_PAIRS+=("$1=$2")
}

configure_algo() {
  local algo="$1"
  CPP_ALGO=""
  ALICE_INPUT="perfect_logit_a.csv"
  BOB_INPUT="perfect_logit_b.csv"
  FEATURE_NUMS='{"host.0":8,"guest.0":7}'
  FEATURE_SELECT='{"host.0":["x9","x2","x3","x4","x5","x6","x7","x8"],"guest.0":["x14","x15","x16","x17","x18","x19","x20"]}'
  FIELD_NAMES=""
  HAS_LABEL_ALICE=false
  EXTRA_ARGS=()

  case "$algo" in
    ecdh-psi)
      CPP_ALGO="ecdh_psi"
      ALICE_INPUT="psi_1.csv"
      BOB_INPUT="psi_2.csv"
      FIELD_NAMES='{"host.0":"id1","guest.0":"id2"}'
      ;;
    ss-lr)
      CPP_ALGO="ss_lr"
      HAS_LABEL_ALICE=true
      ;;
    ss-xgb)
      CPP_ALGO="ss_xgb"
      HAS_LABEL_ALICE=true
      EXTRA_ARGS=(-ssxgb_sync_fixed_splits=true -ssxgb_num_bins=4)
      ;;
    ss-pearson)
      CPP_ALGO="ss_pearsonr"
      ;;
    ss-vif)
      CPP_ALGO="ss_vif"
      ;;
    ss-compare)
      CPP_ALGO="ss_compare"
      FEATURE_NUMS='{"host.0":11,"guest.0":10}'
      FEATURE_SELECT=""
      ;;
    *)
      return 1
      ;;
  esac
}

build_env() {
  local role="$1"
  local storage_root="$2"
  local alice_port="$3"
  local bob_port="$4"
  local output_ns="output/alice"
  local output_name="result.csv"
  if [[ "$CPP_ALGO" == "ss_lr" || "$CPP_ALGO" == "ss_xgb" ]]; then output_name="result.shares.json"; fi
  local input_name="$ALICE_INPUT"
  local self_role="host.0"
  local node_host="127.0.0.1:$alice_port"
  local node_guest="127.0.0.1:$bob_port"
  local local_transport="127.0.0.1:$alice_port"

  if [[ "$role" == "bob" ]]; then
    output_ns="output/bob"
    input_name="$BOB_INPUT"
    self_role="guest.0"
    local_transport="127.0.0.1:$bob_port"
  fi

  if [[ "$RUNTIME" == "docker" ]]; then
    storage_root="/results"
    if [[ "$role" == "alice" ]]; then
      local_transport="0.0.0.0:$alice_port"
    else
      local_transport="0.0.0.0:$bob_port"
    fi
    # The black-box URI is built as `<node_id>:<node_ip>`.  Supplying a
    # Docker service name here therefore creates `bob:bob:29973`, which is
    # ambiguous to the BRPC parser.  Static container IPs keep the URI
    # unambiguous and make the result reproducible across runs.
    node_host="$DOCKER_ALICE_IP:$alice_port"
    node_guest="$DOCKER_BOB_IP:$bob_port"
  fi

  ENV_PAIRS=()
  add_env "system.storage.host.url" "file://$storage_root"
  add_env "runtime.component.input.train_data" "{\"namespace\":\"data\",\"name\":\"$input_name\"}"
  add_env "runtime.component.output.train_data" "{\"namespace\":\"$output_ns\",\"name\":\"$output_name\"}"
  add_env "runtime.component.parameter.algo" "$CPP_ALGO"
  if [[ "$CPP_ALGO" == "ecdh_psi" ]]; then
    add_env "runtime.component.parameter.protocol_families" "ecc"
  else
    add_env "runtime.component.parameter.protocol_families" "ss"
  fi
  add_env "runtime.component.parameter.start_transport" "true"
  add_env "system.transport" "$local_transport"
  add_env "config.self_role" "$self_role"
  add_env "config.inst_id.host.0" "alice"
  add_env "config.inst_id.guest.0" "bob"
  add_env "config.node_id.host.0" "alice"
  add_env "config.node_id.guest.0" "bob"
  add_env "config.node_ip.host.0" "$node_host"
  add_env "config.node_ip.guest.0" "$node_guest"
  add_env "config.task_id" "bfia-cpp-$RUN_ID-$CPP_ALGO"
  add_env "config.session_id" "bfia-cpp-$RUN_ID-$CPP_ALGO"
  add_env "config.trace_id" "bfia-cpp-$RUN_ID-$CPP_ALGO"
  add_env "config.token" "bfia-cpp-$RUN_ID-$CPP_ALGO"

  local self_endpoint="$node_host"
  local peer_endpoint="$node_guest"
  local self_party="$role"
  local peer_party="bob"
  if [[ "$role" == "bob" ]]; then
    self_endpoint="$node_guest"
    peer_endpoint="$node_host"
    peer_party="alice"
  fi
  add_env "BFIA_PROTOCOL" "BFIA"
  add_env "BFIA_STRICT_PROTOCOL" "1"
  add_env "BFIA_ALGORITHM" "$CPP_ALGO"
  add_env "BFIA_SELF_PARTY" "$self_party"
  add_env "BFIA_PEER_PARTY" "$peer_party"
  add_env "BFIA_SELF_ENDPOINT" "$self_endpoint"
  add_env "BFIA_PEER_ENDPOINT" "$peer_endpoint"
  add_env "BFIA_ALICE_ENDPOINT" "$node_host"
  add_env "BFIA_BOB_ENDPOINT" "$node_guest"
  add_env "BFIA_RUNTIME" "$RUNTIME"
  add_env "BFIA_PLATFORM" "linux/amd64"

  if [[ "$CPP_ALGO" == "ecdh_psi" ]]; then
    add_env "runtime.component.parameter.curve_type" "curve25519"
    add_env "runtime.component.parameter.hash_type" "sha_256"
    add_env "runtime.component.parameter.hash2curve_strategy" "direct_hash_as_point_x"
    add_env "runtime.component.parameter.point_octet_format" "uncompressed"
    add_env "runtime.component.parameter.bit_length_after_truncated" "-1"
    add_env "runtime.component.parameter.result_to_rank" "${BFIA_RESULT_RANK:--1}"
    add_env "runtime.component.parameter.field_names" "$FIELD_NAMES"
  else
    if [[ "$role" == "alice" ]]; then
      add_env "runtime.component.parameter.protocol" "${BFIA_ALICE_PROTOCOL:-semi2k}"
    else
      add_env "runtime.component.parameter.protocol" "${BFIA_BOB_PROTOCOL:-semi2k}"
    fi
    add_env "runtime.component.parameter.field" "64"
    add_env "runtime.component.parameter.fxp_bits" "${BFIA_FXP_BITS:-18}"
    add_env "runtime.component.parameter.trunc_mode" "probabilistic"
    add_env "runtime.component.parameter.shard_serialize_format" "raw"
    add_env "runtime.component.parameter.use_ttp" "false"
    add_env "runtime.component.parameter.result_to_rank" "${BFIA_RESULT_RANK:--1}"
    add_env "runtime.component.parameter.feature_nums" "$FEATURE_NUMS"
    if [[ -n "$FEATURE_SELECT" ]]; then
      add_env "runtime.component.parameter.feature_select" "$FEATURE_SELECT"
    fi
  fi

  case "$CPP_ALGO" in
    ss_lr)
      add_env "runtime.component.parameter.label_owner" "host.0"
      add_env "runtime.component.parameter.label_name" "y"
      add_env "runtime.component.parameter.optimizer" "sgd"
      add_env "runtime.component.parameter.learning_rate" "${BFIA_LR_RATE:-0.0001}"
      add_env "runtime.component.parameter.num_epoch" "${BFIA_LR_EPOCHS:-1}"
      add_env "runtime.component.parameter.batch_size" "128"
      add_env "runtime.component.parameter.last_batch_policy" "discard"
      ;;
    ss_xgb)
      add_env "runtime.component.parameter.label_owner" "host.0"
      add_env "runtime.component.parameter.label_name" "y"
      add_env "runtime.component.parameter.num_boost_round" "${BFIA_XGB_ROUNDS:-1}"
      add_env "runtime.component.parameter.max_depth" "1"
      add_env "runtime.component.parameter.learning_rate" "0.1"
      add_env "runtime.component.parameter.reg_lambda" "1.0"
      add_env "runtime.component.parameter.gamma" "0.0"
      add_env "runtime.component.parameter.base_score" "0.5"
      add_env "runtime.component.parameter.seed" "42"
      add_env "runtime.component.parameter.objective" "1"
      add_env "runtime.component.parameter.num_bins" "4"
      add_env "runtime.component.parameter.sketch_eps" "0.25"
      add_env "runtime.component.parameter.colsample_by_tree" "1.0"
      ;;
    ss_compare)
      if [[ "$role" == "alice" ]]; then
        add_env "runtime.component.parameter.compare_column_idx" "0"
        add_env "runtime.component.parameter.compare_column_idx_b" "-1"
      else
        add_env "runtime.component.parameter.compare_column_idx" "-1"
        add_env "runtime.component.parameter.compare_column_idx_b" "0"
      fi
      add_env "runtime.component.parameter.compare_op" "0"
      ;;
  esac
}

wait_native_pair() {
  local alice_pid="$1"
  local bob_pid="$2"
  local started_at
  local elapsed
  local timed_out=0
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
  NATIVE_TIMED_OUT="$timed_out"
}

wait_docker_pair() {
  local alice_name="$1"
  local bob_name="$2"
  local started_at
  local elapsed
  local timed_out=0
  local early_exit=0
  local alice_running=true
  local bob_running=true
  started_at="$(date +%s)"
  while [[ "$alice_running" == "true" || "$bob_running" == "true" ]]; do
    alice_running="$(docker inspect --format '{{.State.Running}}' "$alice_name" 2>/dev/null || echo false)"
    bob_running="$(docker inspect --format '{{.State.Running}}' "$bob_name" 2>/dev/null || echo false)"
    if [[ "$alice_running" != "true" && "$bob_running" != "true" ]]; then
      break
    fi
    if [[ "$alice_running" != "true" && "$bob_running" == "true" ]]; then
      local alice_exit
      alice_exit="$(docker inspect --format '{{.State.ExitCode}}' "$alice_name" 2>/dev/null || echo 1)"
      if [[ "$alice_exit" != "0" ]]; then
        early_exit=1
        docker kill "$bob_name" >/dev/null 2>&1 || true
        break
      fi
    fi
    if [[ "$bob_running" != "true" && "$alice_running" == "true" ]]; then
      local bob_exit
      bob_exit="$(docker inspect --format '{{.State.ExitCode}}' "$bob_name" 2>/dev/null || echo 1)"
      if [[ "$bob_exit" != "0" ]]; then
        early_exit=1
        docker kill "$alice_name" >/dev/null 2>&1 || true
        break
      fi
    fi
    elapsed=$(( $(date +%s) - started_at ))
    if (( elapsed >= TIMEOUT_SEC )); then
      timed_out=1
      docker kill "$alice_name" "$bob_name" >/dev/null 2>&1 || true
      break
    fi
    sleep 1
  done
  DOCKER_TIMED_OUT="$timed_out"
  DOCKER_EARLY_EXIT="$early_exit"
}

count_lines() {
  local file="$1"
  if [[ -f "$file" ]]; then
    wc -l <"$file" | tr -d ' '
  else
    echo 0
  fi
}

run_one() {
  local algo="$1"
  local index="$2"
  local pair_dir="$RUN_DIR/$algo"
  local alice_log="$pair_dir/alice.log"
  local bob_log="$pair_dir/bob.log"
  local alice_port=$((BASE_PORT + index * 10))
  local bob_port=$((alice_port + 1))
  local alice_endpoint="$DOCKER_ALICE_IP:$alice_port"
  local bob_endpoint="$DOCKER_BOB_IP:$bob_port"
  local alice_pid=""
  local bob_pid=""
  local alice_status=0
  local bob_status=0
  local timed_out=0
  local early_exit=0
  local result_name="result.csv"
  if [[ "$algo" == "ss-lr" || "$algo" == "ss-xgb" ]]; then result_name="result.shares.json"; fi
  local alice_output="$pair_dir/output/alice/$result_name"
  local bob_output="$pair_dir/output/bob/$result_name"
  local status="PASS"
  local alice_env_file="$pair_dir/alice.env"
  local bob_env_file="$pair_dir/bob.env"
  local alice_command_args
  local bob_command_args
  local network_evidence=0

  if [[ "$RUNTIME" != "docker" ]]; then
    alice_endpoint="127.0.0.1:$alice_port"
    bob_endpoint="127.0.0.1:$bob_port"
  fi

  configure_algo "$algo" || {
    echo "CPP_TEST_RESULT algorithm=$algo status=FAIL reason=unsupported-algorithm"
    overall_status=1
    return
  }

  mkdir -p "$pair_dir/data" "$pair_dir/output/alice" "$pair_dir/output/bob"
  if [[ "$ALICE_INPUT" == perfect_logit_* ]]; then
    head -n $((SAMPLE_ROWS + 1)) "$DATA_ROOT/$ALICE_INPUT" \
      >"$pair_dir/data/$ALICE_INPUT" 2>"$pair_dir/data-copy.log"
  else
    cp "$DATA_ROOT/$ALICE_INPUT" "$pair_dir/data/$ALICE_INPUT" \
      2>"$pair_dir/data-copy.log"
  fi
  if [[ ! -s "$pair_dir/data/$ALICE_INPUT" ]]; then
    echo "CPP_TEST_RESULT algorithm=$algo status=FAIL reason=alice-input-missing"
    overall_status=1
    return
  fi
  if [[ "$BOB_INPUT" != "$ALICE_INPUT" ]]; then
    if [[ "$BOB_INPUT" == perfect_logit_* ]]; then
      head -n $((SAMPLE_ROWS + 1)) "$DATA_ROOT/$BOB_INPUT" \
        >"$pair_dir/data/$BOB_INPUT" 2>>"$pair_dir/data-copy.log"
    else
      cp "$DATA_ROOT/$BOB_INPUT" "$pair_dir/data/$BOB_INPUT" \
        2>>"$pair_dir/data-copy.log"
    fi
    if [[ ! -s "$pair_dir/data/$BOB_INPUT" ]]; then
      echo "CPP_TEST_RESULT algorithm=$algo status=FAIL reason=bob-input-missing"
      overall_status=1
      return
    fi
  fi

  echo "CPP_TEST_START algorithm=$algo runtime=$RUNTIME protocol=BFIA Alice=$alice_endpoint Bob=$bob_endpoint"
  echo "BFIA协议测试：算法=${algo}；运行时=${RUNTIME}；平台=linux/amd64；真实通信端点=Alice(${alice_endpoint}) <-> Bob(${bob_endpoint})"

  build_env alice "$pair_dir" "$alice_port" "$bob_port"
  printf '%s\n' "${ENV_PAIRS[@]}" >"$alice_env_file"
  alice_command_args=(-disable_handshake=0 "-has_label=$HAS_LABEL_ALICE")
  if (( ${#EXTRA_ARGS[@]} > 0 )); then
    alice_command_args+=("${EXTRA_ARGS[@]}")
  fi
  if [[ "$RUNTIME" == "docker" ]]; then
    local alice_docker_env=()
    local alice_mounts=(-v "$pair_dir:/results")
    local alice_entrypoint="/app/ic_main"
    for pair in "${ENV_PAIRS[@]}"; do
      alice_docker_env+=(-e "$pair")
    done
    if [[ "$CPP_BINARY_IN_DOCKER" == "1" ]]; then
      alice_entrypoint="/bfia/ic_main"
      alice_mounts+=(-v "$BIN_PATH:/bfia/ic_main:ro")
    fi
    ALICE_CONTAINER="bfia-cpp-$RUN_ID-$algo-alice"
    docker rm -f "$ALICE_CONTAINER" >/dev/null 2>&1 || true
    if ! docker run -d --platform linux/amd64 --name "$ALICE_CONTAINER" \
      --network "$NETWORK" --ip "$DOCKER_ALICE_IP" --network-alias alice -w /results \
      "${alice_mounts[@]}" \
      --entrypoint "$alice_entrypoint" \
      "${alice_docker_env[@]}" "$CPP_IMAGE" \
      "${alice_command_args[@]}" \
      >"$pair_dir/alice.container-id" 2>"$pair_dir/alice.start.log"; then
      alice_status=1
    fi
  else
    (
      cd "$pair_dir" || exit 1
      env "${ENV_PAIRS[@]}" "$BIN_PATH" "${alice_command_args[@]}"
    ) >"$alice_log" 2>&1 &
    alice_pid=$!
  fi

  build_env bob "$pair_dir" "$alice_port" "$bob_port"
  printf '%s\n' "${ENV_PAIRS[@]}" >"$bob_env_file"
  bob_command_args=(-disable_handshake=0 -has_label=false)
  if (( ${#EXTRA_ARGS[@]} > 0 )); then
    bob_command_args+=("${EXTRA_ARGS[@]}")
  fi
  if [[ "$RUNTIME" == "docker" ]]; then
    local bob_docker_env=()
    local bob_mounts=(-v "$pair_dir:/results")
    local bob_entrypoint="/app/ic_main"
    for pair in "${ENV_PAIRS[@]}"; do
      bob_docker_env+=(-e "$pair")
    done
    if [[ "$CPP_BINARY_IN_DOCKER" == "1" ]]; then
      bob_entrypoint="/bfia/ic_main"
      bob_mounts+=(-v "$BIN_PATH:/bfia/ic_main:ro")
    fi
    BOB_CONTAINER="bfia-cpp-$RUN_ID-$algo-bob"
    docker rm -f "$BOB_CONTAINER" >/dev/null 2>&1 || true
    if ! docker run -d --platform linux/amd64 --name "$BOB_CONTAINER" \
      --network "$NETWORK" --ip "$DOCKER_BOB_IP" --network-alias bob -w /results \
      "${bob_mounts[@]}" \
      --entrypoint "$bob_entrypoint" \
      "${bob_docker_env[@]}" "$CPP_IMAGE" \
      "${bob_command_args[@]}" \
      >"$pair_dir/bob.container-id" 2>"$pair_dir/bob.start.log"; then
      bob_status=1
    fi
    wait_docker_pair "$ALICE_CONTAINER" "$BOB_CONTAINER"
    timed_out="$DOCKER_TIMED_OUT"
    early_exit="$DOCKER_EARLY_EXIT"
    docker logs "$ALICE_CONTAINER" >"$alice_log" 2>&1 || true
    docker logs "$BOB_CONTAINER" >"$bob_log" 2>&1 || true
    alice_status="$(docker inspect --format '{{.State.ExitCode}}' "$ALICE_CONTAINER" \
      2>/dev/null || true)"
    bob_status="$(docker inspect --format '{{.State.ExitCode}}' "$BOB_CONTAINER" \
      2>/dev/null || true)"
    [[ -n "$alice_status" ]] || alice_status=1
    [[ -n "$bob_status" ]] || bob_status=1
    docker rm -f "$ALICE_CONTAINER" "$BOB_CONTAINER" >/dev/null 2>&1 || true
    ALICE_CONTAINER=""
    BOB_CONTAINER=""
  else
    (
      cd "$pair_dir" || exit 1
      env "${ENV_PAIRS[@]}" "$BIN_PATH" "${bob_command_args[@]}"
    ) >"$bob_log" 2>&1 &
    bob_pid=$!
    wait_native_pair "$alice_pid" "$bob_pid"
    timed_out="$NATIVE_TIMED_OUT"
    wait "$alice_pid" 2>/dev/null || alice_status=$?
    wait "$bob_pid" 2>/dev/null || bob_status=$?
  fi

  if (( timed_out != 0 || alice_status != 0 || bob_status != 0 )); then
    status="FAIL"
  fi
  if [[ ( "$algo" == "ss-pearson" || "$algo" == "ss-vif" || "$algo" == "ss-compare" ) && "${BFIA_RESULT_RANK:--1}" != "-1" ]]; then
    for role in alice bob; do
      if rg -Fq 'semantics=no_plaintext' "$pair_dir/$role.log"; then
        [[ ! -e "$pair_dir/output/$role/result.csv" ]] || status="FAIL"
      else
        [[ -s "$pair_dir/output/$role/result.csv" ]] || status="FAIL"
      fi
    done
  elif [[ ! -s "$alice_output" || ! -s "$bob_output" ]]; then
    status="FAIL"
  fi
  if [[ "$algo" == "ss-xgb" ]]; then
    if [[ ! -s "$pair_dir/output/alice/result.shares.json.model.json" || \
          ! -s "$pair_dir/output/bob/result.shares.json.model.json" ]]; then
      status="FAIL"
    fi
  fi

  if rg -Fq 'BFIA协议启动' "$alice_log" 2>/dev/null && \
     rg -Fq 'BFIA协议启动' "$bob_log" 2>/dev/null && \
     rg -Fq 'BFIA协议双方通信完成' "$alice_log" 2>/dev/null && \
     rg -Fq 'BFIA协议双方通信完成' "$bob_log" 2>/dev/null && \
     rg -Fq "$alice_endpoint" "$alice_log" 2>/dev/null && \
     rg -Fq "$bob_endpoint" "$alice_log" 2>/dev/null && \
     rg -Fq "$alice_endpoint" "$bob_log" 2>/dev/null && \
     rg -Fq "$bob_endpoint" "$bob_log" 2>/dev/null; then
    network_evidence=1
  else
    status="FAIL"
  fi

  {
    echo "algorithm=$algo"
    echo "status=$status"
    echo "runtime=$RUNTIME"
    echo "bfia_protocol=BFIA"
    echo "platform=linux/amd64"
    echo "alice_endpoint=$alice_endpoint"
    echo "bob_endpoint=$bob_endpoint"
    echo "通信协议=BFIA互联互通协议"
    echo "真实通信端点=Alice($alice_endpoint) <-> Bob($bob_endpoint)"
    echo "network_evidence=$network_evidence"
    echo "alice_exit=$alice_status"
    echo "bob_exit=$bob_status"
    echo "timed_out=$timed_out"
    echo "early_exit=$early_exit"
    echo "alice_output=$alice_output"
    echo "alice_output_lines=$(count_lines "$alice_output")"
    echo "bob_output=$bob_output"
    echo "bob_output_lines=$(count_lines "$bob_output")"
    echo "alice_log=$alice_log"
    echo "bob_log=$bob_log"
    if [[ "$algo" == "ss-xgb" ]]; then
      echo "alice_model=$pair_dir/output/alice/result.shares.json.model.json"
      echo "bob_model=$pair_dir/output/bob/result.shares.json.model.json"
    fi
    echo "metrics:"
    rg -i 'BFIA协议|Timing summary|Accuracy|saved to|training finished|run SS|ECDH-PSI' \
      "$alice_log" "$bob_log" 2>/dev/null | tail -12 || true
    echo "diagnostics:"
    rg -i 'error|failed|exception|fatal|run .* failed' \
      "$alice_log" "$bob_log" 2>/dev/null | tail -20 || true
  } >"$pair_dir/summary.txt"
  cat "$pair_dir/summary.txt" >>"$RUN_DIR/summary.txt"

  if [[ "$status" != "PASS" ]]; then
    overall_status=1
  fi
  echo "CPP_TEST_RESULT algorithm=$algo status=$status alice_exit=$alice_status bob_exit=$bob_status"
}

overall_status=0
if [[ -n "${BFIA_CPP_ALGOS:-}" ]]; then
  IFS=', ' read -r -a algorithms <<<"$BFIA_CPP_ALGOS"
else
  algorithms=(ecdh-psi ss-lr ss-xgb ss-pearson ss-vif ss-compare)
fi
index=0
for algorithm in "${algorithms[@]}"; do
  run_one "$algorithm" "$index"
  index=$((index + 1))
done

python3 "$SCRIPT_DIR/collect_protocol_audit.py" "$RUN_DIR" --language cpp --algorithms "${algorithms[@]}" >"$RUN_DIR/protocol-check.log" 2>&1 || overall_status=1

echo "CPP_STATUS=$([[ $overall_status -eq 0 ]] && echo PASS || echo FAIL)"
echo "result_dir=$RUN_DIR"
exit "$overall_status"
