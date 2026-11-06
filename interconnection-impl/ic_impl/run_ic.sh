#!/usr/bin/env bash

set -euo pipefail

# 用法: ./run_ic.sh <algo> [role]
#   <algo>: ecdh-psi | ss-lr | ss-xgb | ss-pearson | ss-vif | ss-compare
#   [role]: 可选，alice | bob；不提供则自动识别
# 示例:
#   ./run_ic.sh ss-lr            # 自动识别角色
#   ./run_ic.sh ecdh-psi bob     # 指定角色
#   ./run_ic.sh ss-compare       # 运行安全比较算法

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IC_IMPL_DIR="$SCRIPT_DIR"
REPO_ROOT="$IC_IMPL_DIR/../.."
ENV_DIR="$IC_IMPL_DIR/env"
# 自动探测可执行文件位置（兼容在仓库根或 interconnection-impl 作为工作区构建）
BIN_CANDIDATES=(
  "$REPO_ROOT/bazel-bin/interconnection-impl/ic_impl/ic_main"
  "$IC_IMPL_DIR/../bazel-bin/ic_impl/ic_main"
  "$IC_IMPL_DIR/../../bazel-bin/ic_impl/ic_main"
)
BIN_PATH=""
for c in "${BIN_CANDIDATES[@]}"; do
  if [[ -x "$c" ]]; then BIN_PATH="$c"; break; fi
done
LOG_ROOT="$IC_IMPL_DIR/logs"

if [[ $# -lt 1 ]]; then
  echo "用法: $0 <algo> [role]" >&2
  echo "  <algo>: ecdh-psi | ss-lr | ss-xgb | ss-pearson | ss-vif | ss-compare" >&2
  echo "  [role]: 可选，alice | bob；不提供则自动识别" >&2
  exit 1
fi

ALGO_RAW="$1"
ROLE_RAW="${2:-}"

case "$ALGO_RAW" in
  ecdh-psi|ecdh_psi)
    ALGO_NORM="ecdh-psi"
    ;;
  ss-linr|ss_linr)
    ALGO_NORM="ss-lr"
    ;;
  ss-xgb|ss_xgb)
    ALGO_NORM="ss-xgb"
    ;;
  ss-pearson|ss_pearson|ss-pearsonr|ss_pearsonr)
    ALGO_NORM="ss-pearson"
    ;;
  ss-vif|ss_vif)
    ALGO_NORM="ss-vif"
    ;;
  ss-compare|ss_compare)
    ALGO_NORM="ss-compare"
    ;;
  *)
    echo "不支持的算法类型: $ALGO_RAW (支持: ecdh-psi, ss-lr, ss-xgb, ss-pearson, ss-vif, ss-compare)" >&2
    exit 2
    ;;
esac

ROLE_NORM=""
if [[ -n "$ROLE_RAW" ]]; then
  case "$ROLE_RAW" in
    alice|ALICE)
      ROLE_NORM="alice"
      ;;
    bob|BOB)
      ROLE_NORM="bob"
      ;;
    *)
      echo "不支持的角色: $ROLE_RAW (支持: alice, bob)" >&2
      exit 3
      ;;
  esac
else
  # 自动识别：根据本机 IP 与 env 中的 system.transport IP 匹配
  mapfile -t LOCAL_IPS < <(hostname -I 2>/dev/null | tr ' ' '\n' | sed '/^$/d')
  LOCAL_IPS+=("127.0.0.1")
  ENV_ALICE="$ENV_DIR/${ALGO_NORM}-env-alice.sh"
  ENV_BOB="$ENV_DIR/${ALGO_NORM}-env-bob.sh"
  if [[ ! -f "$ENV_ALICE" || ! -f "$ENV_BOB" ]]; then
    echo "缺少 env 文件: $ENV_ALICE 或 $ENV_BOB" >&2
    exit 4
  fi
  extract_transport_ip() {
    local f="$1"
    local tp
    tp=$(sed -n '/system\.transport=/p' "$f" | head -n1 | sed -E 's/.*system\.transport=([^"\ ]*).*/\1/' || true)
    echo "${tp%%:*}"
  }
  IP_ALICE=$(extract_transport_ip "$ENV_ALICE")
  IP_BOB=$(extract_transport_ip "$ENV_BOB")
  match_ip() {
    local target="$1"; shift
    for ip in "$@"; do
      if [[ "$ip" == "$target" ]]; then return 0; fi
    done
    return 1
  }
  if [[ -n "$IP_ALICE" ]] && match_ip "$IP_ALICE" "${LOCAL_IPS[@]}"; then
    ROLE_NORM="alice"
  elif [[ -n "$IP_BOB" ]] && match_ip "$IP_BOB" "${LOCAL_IPS[@]}"; then
    ROLE_NORM="bob"
  else
    # 进一步尝试：根据主机名包含 alice/bob 判断
    hn=$(hostname 2>/dev/null || true)
    shn=$(hostname -s 2>/dev/null || true)
    if echo "$hn $shn" | grep -qi 'alice'; then
      ROLE_NORM="alice"
    elif echo "$hn $shn" | grep -qi 'bob'; then
      ROLE_NORM="bob"
    else
      echo "无法自动识别角色，请显式指定 alice 或 bob" >&2
      exit 3
    fi
  fi
fi

ENV_SCRIPT="$ENV_DIR/${ALGO_NORM}-env-${ROLE_NORM}.sh"
if [[ ! -f "$ENV_SCRIPT" ]]; then
  echo "未找到环境脚本: $ENV_SCRIPT" >&2
  exit 4
fi

# 从 env 脚本提取 KEY=VALUE 对并导出到当前进程，而不进入交互 shell。
# 规则：抓取从 `exec env \` 开始到包含 `$SHELL` 的行之间的每一行，每行一个赋值，去掉行尾续行反斜杠与外层引号。
mapfile -t KV_LINES < <(
  awk '
    /^exec env[[:space:]]*\\/ { inblk=1; next }
    inblk { print; if ($0 ~ /\$SHELL/) inblk=0 }
  ' "$ENV_SCRIPT"
)

KV_PAIRS=()
for line in "${KV_LINES[@]}"; do
  # 去除行尾反斜杠与前后空白
  line="${line% \\}"
  line="${line%%[[:space:]]}"
  line="${line##[[:space:]]}"
  # 跳过空行与 $SHELL
  [[ -z "$line" ]] && continue
  [[ "$line" =~ \$SHELL ]] && continue
  # 去除首尾成对引号（支持 ' 或 ")
  first=${line:0:1}
  last=${line: -1}
  if [[ "$first" == '"' || "$first" == "'" ]]; then
    if [[ "$last" == "$first" && ${#line} -ge 2 ]]; then
      line="${line:1:${#line}-2}"
    fi
  fi
  KV_PAIRS+=("$line")
done

if [[ ${#KV_PAIRS[@]} -eq 0 ]]; then
  echo "解析 env 失败: $ENV_SCRIPT" >&2
  exit 6
fi

# 准备传给 env 的参数（不能用 export，因键名含点不符合 shell 标识符规则）
ENV_ARGS=( )
for pair in "${KV_PAIRS[@]}"; do
  if [[ "$pair" != *=* ]]; then
    echo "跳过无效 env 条目: $pair" >&2
    continue
  fi
  ENV_ARGS+=("$pair")
done

# 确认二进制存在
if [[ -z "$BIN_PATH" ]]; then
  echo "未找到可执行文件。请先构建 ic_main：" >&2
  echo "  (在 interconnection-impl 目录下)  bazel build -c opt ic_impl/ic_main" >&2
  echo "  (在仓库根目录下)            bazel build -c opt //interconnection-impl/ic_impl:ic_main" >&2
  exit 5
fi

# 准备日志目录与文件
TS="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$ALGO_NORM/$ROLE_NORM"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/ic_main-$TS.log"
ln -snf "$LOG_FILE" "$LOG_DIR/latest.log"
ln -snf "$LOG_FILE" "$LOG_ROOT/latest-$ALGO_NORM-$ROLE_NORM.log"

# 打印运行头信息（同时写入日志）
{
  echo "========== IC Run =========="
  echo "time:        $TS"
  echo "algorithm:   $ALGO_NORM"
  echo "role:        $ROLE_NORM"
  echo "binary:      $BIN_PATH"
  echo "env script:  $ENV_SCRIPT"
  echo "log file:    $LOG_FILE"
  echo "============================"
} | tee -a "$LOG_FILE"

# 运行 ic_main，默认关闭握手，捕获终端输出到日志（通过 env 注入带点的键名）
env "${ENV_ARGS[@]}" "$BIN_PATH" --disable_handshake 2>&1 | tee -a "$LOG_FILE"
