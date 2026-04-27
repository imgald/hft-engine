#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# stress_test.sh — Send a high volume of FIX orders to the matching engine
#                  and measure end-to-end throughput.
#
# Usage:
#   ./scripts/stress_test.sh [num_orders] [port]
#
# Example:
#   ./scripts/stress_test.sh 10000 9001
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

NUM_ORDERS=${1:-5000}
PORT=${2:-9001}
SOH=$'\x01'

echo ""
echo "══════════════════════════════════════════════════════"
echo "  HFT Engine Stress Test"
echo "  Orders : $NUM_ORDERS pairs (buy + sell = $((NUM_ORDERS * 2)) total)"
echo "  Port   : $PORT"
echo "══════════════════════════════════════════════════════"
echo ""

# Check engine is running
if ! nc -z localhost $PORT 2>/dev/null; then
    echo "ERROR: Engine not running on port $PORT"
    echo "Start with: ./build/Release/bin/hft_server"
    exit 1
fi

echo "Engine is up. Sending orders..."

# Build order batch in memory, then pipe to nc
{
    for i in $(seq 1 $NUM_ORDERS); do
        PRICE=$((18900 + (i % 20)))   # prices from 18900 to 18919
        QTY=100

        # Sell order
        MSG="8=FIX.4.2${SOH}35=D${SOH}11=S${i}${SOH}55=AAPL${SOH}54=2${SOH}38=${QTY}${SOH}44=${PRICE}${SOH}40=2${SOH}10=000${SOH}"
        printf "%s" "$MSG"

        # Buy order at same price (will match)
        MSG="8=FIX.4.2${SOH}35=D${SOH}11=B${i}${SOH}55=AAPL${SOH}54=1${SOH}38=${QTY}${SOH}44=${PRICE}${SOH}40=2${SOH}10=000${SOH}"
        printf "%s" "$MSG"
    done
    sleep 0.5   # wait for engine to process
} | nc localhost $PORT > /tmp/hft_stress_output.txt 2>&1 &

NC_PID=$!

# Time the operation
START_NS=$(date +%s%N)
wait $NC_PID 2>/dev/null || true
END_NS=$(date +%s%N)

ELAPSED_MS=$(( (END_NS - START_NS) / 1000000 ))
ELAPSED_S=$(echo "scale=3; $ELAPSED_MS / 1000" | bc)
TOTAL_ORDERS=$(( NUM_ORDERS * 2 ))
THROUGHPUT=$(echo "scale=0; $TOTAL_ORDERS * 1000 / $ELAPSED_MS" | bc 2>/dev/null || echo "N/A")

# Count execution reports received
EXEC_REPORTS=$(grep -c "35=8" /tmp/hft_stress_output.txt 2>/dev/null || echo 0)

echo ""
echo "Results:"
echo "  Total orders sent : $TOTAL_ORDERS"
echo "  Elapsed time      : ${ELAPSED_S}s"
echo "  Throughput        : ~${THROUGHPUT} orders/sec (end-to-end with network)"
echo "  Exec reports recv : $EXEC_REPORTS"
echo ""
echo "Note: In-process throughput (no network) is ~13.6M orders/sec"
echo "      (see bench/bench_order_book.cpp benchmark results)"
echo ""