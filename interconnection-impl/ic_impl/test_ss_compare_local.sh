#!/bin/bash
# 本地测试 SS-Compare 算法的双进程运行脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 清理旧进程
echo "清理旧进程..."
pkill -f "ic_main.*ss-compare" 2>/dev/null || true
sleep 2

# 创建必要的目录
mkdir -p logs/ss-compare/alice logs/ss-compare/bob output

# 清理旧的测试输出
echo "清理旧的测试输出..."
rm -f output/result-ss-compare-alice.csv output/result-ss-compare-bob.csv output/result.csv

echo "=========================================="
echo "Starting SS-Compare Test with Two Processes"
echo "=========================================="
echo ""

# 检查可执行文件
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
  echo "错误: 找不到 ic_main 可执行文件"
  echo "请先运行: bazel build -c opt ic_impl/ic_main"
  exit 1
fi

echo "使用可执行文件: $BIN_PATH"
echo ""

# 启动 Alice 进程（rank 0, passive）
echo "[$(date +%H:%M:%S)] 启动 Alice (rank 0, passive)..."
ALICE_LOG="logs/ss-compare/alice/ic_main-$(date +%Y%m%d-%H%M%S).log"
mkdir -p "$(dirname "$ALICE_LOG")"

source env/ss-compare-env-alice.sh
$BIN_PATH > "$ALICE_LOG" 2>&1 &
ALICE_PID=$!
echo "Alice PID: $ALICE_PID"
echo "Alice 日志: $ALICE_LOG"
sleep 3

# 启动 Bob 进程（rank 1, active）
echo "[$(date +%H:%M:%S)] 启动 Bob (rank 1, active)..."
BOB_LOG="logs/ss-compare/bob/ic_main-$(date +%Y%m%d-%H%M%S).log"
mkdir -p "$(dirname "$BOB_LOG")"

source env/ss-compare-env-bob.sh
$BIN_PATH > "$BOB_LOG" 2>&1 &
BOB_PID=$!
echo "Bob PID: $BOB_PID"
echo "Bob 日志: $BOB_LOG"
echo ""

# 等待进程完成
echo "等待计算完成..."
MAX_WAIT=120  # 最多等待120秒
ELAPSED=0
INTERVAL=2

while [ $ELAPSED -lt $MAX_WAIT ]; do
    if ! kill -0 $ALICE_PID 2>/dev/null && ! kill -0 $BOB_PID 2>/dev/null; then
        echo "[$(date +%H:%M:%S)] 两个进程都已完成"
        break
    fi
    sleep $INTERVAL
    ELAPSED=$((ELAPSED + INTERVAL))
    if [ $((ELAPSED % 10)) -eq 0 ]; then
        echo -n "."
    fi
done
echo ""

# 检查进程状态
if kill -0 $ALICE_PID 2>/dev/null || kill -0 $BOB_PID 2>/dev/null; then
    echo "警告: 部分进程仍在运行（等待了 ${MAX_WAIT} 秒）"
    kill -0 $ALICE_PID 2>/dev/null && echo "  Alice (PID: $ALICE_PID) 仍在运行"
    kill -0 $BOB_PID 2>/dev/null && echo "  Bob (PID: $BOB_PID) 仍在运行"
fi

# 等待进程完全退出
sleep 2

# 检查输出文件
echo ""
echo "=========================================="
echo "检查结果"
echo "=========================================="

# 检查 Alice 的输出
if [ -f output/result-ss-compare-alice.csv ]; then
    echo "✓ 找到 Alice 输出: output/result-ss-compare-alice.csv"
    echo "  行数: $(wc -l < output/result-ss-compare-alice.csv)"
    echo "  前10行:"
    head -10 output/result-ss-compare-alice.csv | sed 's/^/    /'
else
    echo "✗ 未找到 Alice 输出文件"
fi

# 检查 Bob 的输出
if [ -f output/result-ss-compare-bob.csv ]; then
    echo ""
    echo "✓ 找到 Bob 输出: output/result-ss-compare-bob.csv"
    echo "  行数: $(wc -l < output/result-ss-compare-bob.csv)"
    echo "  前10行:"
    head -10 output/result-ss-compare-bob.csv | sed 's/^/    /'
else
    echo ""
    echo "✗ 未找到 Bob 输出文件"
fi

# 验证输出格式（应该是 true/false）
if [ -f output/result-ss-compare-alice.csv ] || [ -f output/result-ss-compare-bob.csv ]; then
    echo ""
    echo "=========================================="
    echo "验证输出格式"
    echo "=========================================="
    
    for out_file in output/result-ss-compare-*.csv; do
        if [ -f "$out_file" ]; then
            echo "检查 $out_file:"
            # 检查是否包含 true/false
            if grep -qE "true|false" "$out_file" 2>/dev/null; then
                echo "  ✓ 包含 true/false 值"
            else
                echo "  ⚠ 警告: 不包含 true/false 值"
                echo "  前5行内容:"
                head -5 "$out_file" | sed 's/^/    /'
            fi
            # 统计 true 和 false 的数量
            true_count=$(grep -o "true" "$out_file" | wc -l || echo "0")
            false_count=$(grep -o "false" "$out_file" | wc -l || echo "0")
            echo "  True 数量: $true_count"
            echo "  False 数量: $false_count"
        fi
    done
fi

# 检查日志中的错误
echo ""
echo "=========================================="
echo "检查日志中的错误"
echo "=========================================="

if [ -f "$ALICE_LOG" ]; then
    echo "Alice 日志: $ALICE_LOG"
    if grep -i "error\|fail\|exception" "$ALICE_LOG" | grep -v "Not connected" | head -5; then
        echo "  ⚠ 在 Alice 日志中发现错误"
    else
        echo "  ✓ Alice 日志中没有关键错误"
    fi
    if grep -i "SS-Compare\|comparison result saved\|run SS-Compare" "$ALICE_LOG" | tail -3; then
        echo "  ✓ 找到算法执行完成消息"
    fi
fi

if [ -f "$BOB_LOG" ]; then
    echo ""
    echo "Bob 日志: $BOB_LOG"
    if grep -i "error\|fail\|exception" "$BOB_LOG" | grep -v "Not connected" | head -5; then
        echo "  ⚠ 在 Bob 日志中发现错误"
    else
        echo "  ✓ Bob 日志中没有关键错误"
    fi
    if grep -i "SS-Compare\|comparison result saved\|run SS-Compare" "$BOB_LOG" | tail -3; then
        echo "  ✓ 找到算法执行完成消息"
    fi
fi

echo ""
echo "=========================================="
echo "测试完成"
echo "=========================================="
echo ""
echo "输出文件:"
ls -lh output/result-ss-compare-*.csv 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'

echo ""
echo "日志文件:"
echo "  Alice: $ALICE_LOG"
echo "  Bob: $BOB_LOG"

