#!/bin/bash
# 测试 SS-Compare 算法的双进程运行

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 清理旧进程
pkill -f "ic_main.*ss-compare" 2>/dev/null || true
sleep 2

# 创建日志目录
mkdir -p logs/test output

# 清理旧的测试输出
rm -f output/result_rank0.csv output/result_rank1.csv output/result-ss-compare-alice.csv output/result-ss-compare-bob.csv output/result.csv

echo "=========================================="
echo "Starting SS-Compare Test with Two Processes"
echo "=========================================="
echo ""

# 启动 Alice 进程（rank 0）
echo "[$(date +%H:%M:%S)] Starting Alice (rank 0)..."
bash run_ic.sh ss-compare alice > logs/test/alice-$(date +%Y%m%d-%H%M%S).log 2>&1 &
ALICE_PID=$!
echo "Alice PID: $ALICE_PID"
sleep 3

# 启动 Bob 进程（rank 1）
echo "[$(date +%H:%M:%S)] Starting Bob (rank 1)..."
bash run_ic.sh ss-compare bob > logs/test/bob-$(date +%Y%m%d-%H%M%S).log 2>&1 &
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

# 检查 rank 0 的输出（Alice）
if [ -f output/result_rank0.csv ]; then
    echo "✓ Found Alice output (rank 0): output/result_rank0.csv"
    echo "  Lines: $(wc -l < output/result_rank0.csv)"
    echo "  First 10 rows:"
    head -10 output/result_rank0.csv | sed 's/^/    /'
elif [ -f output/result-ss-compare-alice.csv ]; then
    echo "✓ Found Alice output: output/result-ss-compare-alice.csv"
    echo "  Lines: $(wc -l < output/result-ss-compare-alice.csv)"
    echo "  First 10 rows:"
    head -10 output/result-ss-compare-alice.csv | sed 's/^/    /'
fi

# 检查 rank 1 的输出（Bob）
if [ -f output/result_rank1.csv ]; then
    echo ""
    echo "✓ Found Bob output (rank 1): output/result_rank1.csv"
    echo "  Lines: $(wc -l < output/result_rank1.csv)"
    echo "  First 10 rows:"
    head -10 output/result_rank1.csv | sed 's/^/    /'
elif [ -f output/result-ss-compare-bob.csv ]; then
    echo ""
    echo "✓ Found Bob output: output/result-ss-compare-bob.csv"
    echo "  Lines: $(wc -l < output/result-ss-compare-bob.csv)"
    echo "  First 10 rows:"
    head -10 output/result-ss-compare-bob.csv | sed 's/^/    /'
fi

# 检查是否有默认输出
if [ -f output/result.csv ]; then
    echo ""
    echo "✓ Found default output: output/result.csv"
    echo "  Lines: $(wc -l < output/result.csv)"
    echo "  First 10 rows:"
    head -10 output/result.csv | sed 's/^/    /'
fi

# 验证输出格式（应该是 true/false）
if [ -f output/result_rank0.csv ] || [ -f output/result_rank1.csv ]; then
    echo ""
    echo "=========================================="
    echo "Validating Output Format"
    echo "=========================================="
    
    for out_file in output/result_rank*.csv output/result-ss-compare-*.csv; do
        if [ -f "$out_file" ]; then
            echo "Checking $out_file:"
            # 检查是否包含 true/false
            if grep -qE "true|false" "$out_file" 2>/dev/null; then
                echo "  ✓ Contains true/false values"
            else
                echo "  ⚠ Warning: Does not contain true/false values"
            fi
            # 统计 true 和 false 的数量
            true_count=$(grep -o "true" "$out_file" | wc -l || echo "0")
            false_count=$(grep -o "false" "$out_file" | wc -l || echo "0")
            echo "  True count: $true_count"
            echo "  False count: $false_count"
        fi
    done
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
    if grep -i "SS-Compare\|comparison result saved" "$ALICE_LOG" | tail -3; then
        echo "  ✓ Found comparison completion messages"
    fi
fi

if [ -n "$BOB_LOG" ]; then
    echo "Bob log: $BOB_LOG"
    if grep -i "error\|fail\|exception" "$BOB_LOG" | grep -v "Not connected" | head -5; then
        echo "  ⚠ Found errors in Bob log"
    else
        echo "  ✓ No critical errors in Bob log"
    fi
    if grep -i "SS-Compare\|comparison result saved" "$BOB_LOG" | tail -3; then
        echo "  ✓ Found comparison completion messages"
    fi
fi

echo ""
echo "=========================================="
echo "Test Complete"
echo "=========================================="
echo ""
echo "Output files:"
ls -lh output/result* 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'

