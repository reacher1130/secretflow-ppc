#!/bin/bash
# 测试 SS-VIF 算法的双进程运行

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 清理旧进程
pkill -f "ic_main.*ss-vif" 2>/dev/null || true
sleep 2

# 创建日志目录
mkdir -p logs/test output

# 清理旧的测试输出
rm -f output/result-ss-vif-alice.csv output/result-ss-vif-bob.csv output/result.csv

echo "=========================================="
echo "Starting SS-VIF Test with Two Processes"
echo "=========================================="
echo ""

# 启动 Alice 进程（rank 0）
echo "[$(date +%H:%M:%S)] Starting Alice (rank 0)..."
bash run_ic.sh ss-vif alice > logs/test/alice-$(date +%Y%m%d-%H%M%S).log 2>&1 &
ALICE_PID=$!
echo "Alice PID: $ALICE_PID"
sleep 3

# 启动 Bob 进程（rank 1）
echo "[$(date +%H:%M:%S)] Starting Bob (rank 1)..."
bash run_ic.sh ss-vif bob > logs/test/bob-$(date +%Y%m%d-%H%M%S).log 2>&1 &
BOB_PID=$!
echo "Bob PID: $BOB_PID"
echo ""

# 等待进程完成
echo "Waiting for computation to complete..."
MAX_WAIT=120  # 最多等待120秒
ELAPSED=0
INTERVAL=5

while [ $ELAPSED -lt $MAX_WAIT ]; do
    if ! kill -0 $ALICE_PID 2>/dev/null && ! kill -0 $BOB_PID 2>/dev/null; then
        echo "[$(date +%H:%M:%S)] Both processes completed"
        break
    fi
    sleep $INTERVAL
    ELAPSED=$((ELAPSED + INTERVAL))
    echo -n "."
done
echo ""

# 检查进程状态
if kill -0 $ALICE_PID 2>/dev/null || kill -0 $BOB_PID 2>/dev/null; then
    echo "Warning: Some processes are still running after ${MAX_WAIT}s"
    kill -0 $ALICE_PID 2>/dev/null && echo "  Alice (PID: $ALICE_PID) still running"
    kill -0 $BOB_PID 2>/dev/null && echo "  Bob (PID: $BOB_PID) still running"
fi

# 等待进程完全退出
sleep 2

# 检查输出文件
echo ""
echo "=========================================="
echo "Checking Results"
echo "=========================================="

if [ -f output/result-ss-vif-alice.csv ]; then
    echo "✓ Found Alice output: output/result-ss-vif-alice.csv"
    echo "  Lines: $(wc -l < output/result-ss-vif-alice.csv)"
    echo "  First 10 values:"
    head -10 output/result-ss-vif-alice.csv | sed 's/^/    /'
elif [ -f output/result-ss-vif-bob.csv ]; then
    echo "✓ Found Bob output: output/result-ss-vif-bob.csv"
    echo "  Lines: $(wc -l < output/result-ss-vif-bob.csv)"
    echo "  First 10 values:"
    head -10 output/result-ss-vif-bob.csv | sed 's/^/    /'
elif [ -f output/result.csv ]; then
    echo "✓ Found default output: output/result.csv"
    echo "  Lines: $(wc -l < output/result.csv)"
    echo "  First 10 values:"
    head -10 output/result.csv | sed 's/^/    /'
else
    echo "✗ No output file found"
fi

# 检查日志中的错误
echo ""
echo "=========================================="
echo "Checking Logs for Errors"
echo "=========================================="

ALICE_LOG=$(ls -t logs/test/alice-*.log 2>/dev/null | head -1)
BOB_LOG=$(ls -t logs/test/bob-*.log 2>/dev/null | head -1)

if [ -n "$ALICE_LOG" ]; then
    echo "Alice log: $ALICE_LOG"
    if grep -i "error\|fail\|exception" "$ALICE_LOG" | grep -v "Not connected" | head -5; then
        echo "  ⚠ Found errors in Alice log"
    else
        echo "  ✓ No critical errors in Alice log"
    fi
    if grep -i "SS-VIF\|VIF.*saved" "$ALICE_LOG" | tail -3; then
        echo "  ✓ Found VIF completion messages"
    fi
fi

if [ -n "$BOB_LOG" ]; then
    echo "Bob log: $BOB_LOG"
    if grep -i "error\|fail\|exception" "$BOB_LOG" | grep -v "Not connected" | head -5; then
        echo "  ⚠ Found errors in Bob log"
    else
        echo "  ✓ No critical errors in Bob log"
    fi
    if grep -i "SS-VIF\|VIF.*saved" "$BOB_LOG" | tail -3; then
        echo "  ✓ Found VIF completion messages"
    fi
fi

echo ""
echo "=========================================="
echo "Test Complete"
echo "=========================================="
