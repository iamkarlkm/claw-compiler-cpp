#!/bin/bash
set -euo pipefail

CURRENT="${1:-benchmark/results/latest.json}"
BASELINE="${2:-}"
THRESHOLD="${THRESHOLD:-20}"

if [ -z "$BASELINE" ]; then
    # Find the most recent baseline (excluding current)
    BASELINE=$(ls -t benchmark/results/benchmark_*.json 2>/dev/null | grep -v "$(basename "$CURRENT")" | head -1 || true)
fi

if [ -z "$BASELINE" ] || [ ! -f "$BASELINE" ]; then
    echo "No baseline found. Skipping regression check."
    exit 0
fi

echo "Comparing $CURRENT against baseline $BASELINE (threshold: ${THRESHOLD}%)"

REGRESSIONS=0
while IFS= read -r line; do
    bench=$(echo "$line" | sed -n 's/.*"benchmark":"\([^"]*\)".*/\1/p')
    mode=$(echo "$line" | sed -n 's/.*"mode":"\([^"]*\)".*/\1/p')
    time=$(echo "$line" | sed -n 's/.*"time_ms":\([0-9]*\).*/\1/p')
    
    if [ -z "$bench" ] || [ -z "$time" ]; then
        continue
    fi
    
    base_time=$(grep "\"benchmark\":\"$bench\".*\"mode\":\"$mode\"" "$BASELINE" | sed -n 's/.*"time_ms":\([0-9]*\).*/\1/p' | head -1 || true)
    
    if [ -z "$base_time" ] || [ "$base_time" -eq 0 ]; then
        continue
    fi
    
    increase=$(( (time - base_time) * 100 / base_time ))
    
    if [ "$increase" -gt "$THRESHOLD" ]; then
        echo "REGRESSION: $bench/$mode: ${base_time}ms -> ${time}ms (+${increase}%)"
        REGRESSIONS=$((REGRESSIONS + 1))
    elif [ "$increase" -lt -$THRESHOLD ]; then
        echo "IMPROVEMENT: $bench/$mode: ${base_time}ms -> ${time}ms (${increase}%)"
    fi
done < <(grep '"benchmark"' "$CURRENT")

if [ "$REGRESSIONS" -gt 0 ]; then
    echo ""
    echo "$REGRESSIONS regression(s) detected above ${THRESHOLD}% threshold"
    exit 1
fi

echo "No regressions detected."
