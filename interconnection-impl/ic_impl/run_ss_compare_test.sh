#!/bin/bash
# SS-Compare 算法本地测试脚本（两个终端模拟）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "SS-Compare 算法本地测试"
echo "=========================================="
echo ""

# 检查是否需要重新编译
echo "1. 检查构建状态..."
BIN_PATH=""
REPO_ROOT="$SCRIPT_DIR/../.."
IC_IMPL_DIR="$SCRIPT_DIR"
BIN_CANDIDATES=(
  "$REPO_ROOT/bazel-bin/interconnection-impl/ic_impl/ic_main"
  "$IC_IMPL_DIR/../bazel-bin/ic_impl/ic_main"
  "$IC_IMPL_DIR/../../bazel-bin/ic_impl/ic_main"
)

for c in "${BIN_CANDIDATES[@]}"; do
  if [[ -x "$c" ]]; then 
    BIN_PATH="$c"
    break
  fi
done

if [[ -z "$BIN_PATH" ]]; then
  echo "❌ 错误: 找不到 ic_main 可执行文件"
  echo "请先运行: bazel build -c opt ic_impl/ic_main"
  exit 1
fi

echo "✓ 找到可执行文件: $BIN_PATH"

# 检查是否需要重新编译
NEED_REBUILD=false
if [[ -f "factory_ss_compare.cc" ]] && [[ -f "$BIN_PATH" ]]; then
  if [[ "factory_ss_compare.cc" -nt "$BIN_PATH" ]] || \
     [[ "party.cc" -nt "$BIN_PATH" ]] || \
     [[ "algo/ss_compare/ss_compare_handler.cc" -nt "$BIN_PATH" ]]; then
    NEED_REBUILD=true
    echo "⚠ 检测到代码修改，建议重新编译"
  fi
fi

if [[ "$NEED_REBUILD" == "true" ]]; then
  echo ""
  echo "2. 重新编译项目..."
  cd "$REPO_ROOT/interconnection-impl" || cd "$SCRIPT_DIR/.."
  bazel build -c opt ic_impl/ic_main
  echo "✓ 编译完成"
fi

echo ""
echo "3. 检查输入数据文件..."
if [[ ! -f "data/perfect_logit_a.csv" ]]; then
  echo "❌ 错误: 找不到输入文件 data/perfect_logit_a.csv"
  exit 1
fi
if [[ ! -f "data/perfect_logit_b.csv" ]]; then
  echo "❌ 错误: 找不到输入文件 data/perfect_logit_b.csv"
  exit 1
fi
echo "✓ 输入数据文件存在"

echo ""
echo "4. 清理旧的输出和进程..."
pkill -f "ic_main.*ss-compare" 2>/dev/null || true
sleep 1
rm -f output/result-ss-compare-alice.csv output/result-ss-compare-bob.csv
mkdir -p output logs/ss-compare/alice logs/ss-compare/bob

echo ""
echo "=========================================="
echo "准备运行测试"
echo "=========================================="
echo ""
echo "将在两个终端中运行 Alice 和 Bob"
echo ""
echo "终端1命令（Alice, rank 0, passive）:"
echo "  cd $SCRIPT_DIR"
echo "  source env/ss-compare-env-alice.sh"
echo "  $BIN_PATH --disable_handshake"
echo ""
echo "终端2命令（Bob, rank 1, active）:"
echo "  cd $SCRIPT_DIR"
echo "  source env/ss-compare-env-bob.sh"
echo "  $BIN_PATH --disable_handshake"
echo ""
echo "或者使用 run_ic.sh 脚本:"
echo "  终端1: ./run_ic.sh ss-compare alice"
echo "  终端2: ./run_ic.sh ss-compare bob"
echo ""
echo "=========================================="
echo "配置信息"
echo "=========================================="
echo "算法: ss-compare"
echo "协议: Semi2K"
echo "比较列: compare_column_idx=0 (Alice的第一列 x1)"
echo "比较操作: compare_op=0 (大于)"
echo "阈值: 该列的均值"
echo ""
echo "输入文件:"
echo "  Alice: data/perfect_logit_a.csv (11列, 10000样本)"
echo "  Bob: data/perfect_logit_b.csv (10列, 10000样本)"
echo ""
echo "输出文件:"
echo "  Alice: output/result-ss-compare-alice.csv"
echo "  Bob: output/result-ss-compare-bob.csv"
echo ""
echo "输出格式: 每行一个布尔值（true/false）"
echo ""
echo "=========================================="
echo "开始测试..."
echo "=========================================="
echo ""
echo "请按照上述命令在两个终端中分别运行 Alice 和 Bob"
echo "运行完成后，检查输出文件:"
echo "  ls -lh output/result-ss-compare-*.csv"
echo "  head -20 output/result-ss-compare-alice.csv"
echo ""
