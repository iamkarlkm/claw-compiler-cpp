#!/bin/bash
set -euo pipefail

CLAW="${CLAW:-./claw}"
OUTDIR="${OUTDIR:-benchmark/results}"
mkdir -p "$OUTDIR"

BENCHMARKS=(
    "tests/benchmark/fibonacci.claw"
    "tests/benchmark/sum.claw"
    "tests/benchmark/prime.claw"
)

MODES=(
    "run:--run"
    "bytecode:--mode=bytecode"
    "jit:--mode=jit"
)

TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
GIT_SHA=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")

RESULT_FILE="$OUTDIR/benchmark_${TIMESTAMP}.json"

cat > "$RESULT_FILE" <<EOF
{
  "timestamp": "$TIMESTAMP",
  "git_sha": "$GIT_SHA",
  "results": [
EOF

FIRST=1
for bench in "${BENCHMARKS[@]}"; do
    for mode_spec in "${MODES[@]}"; do
        IFS=':' read -r mode_name mode_flag <<< "$mode_spec"

        # Warmup
        $CLAW $mode_flag "$bench" >/dev/null 2>&1 || true

        # Measure
        start=$(date +%s%N)
        $CLAW $mode_flag "$bench" >/dev/null 2>&1 || true
        end=$(date +%s%N)

        elapsed_ms=$(( (end - start) / 1000000 ))

        if [ "$FIRST" -eq 1 ]; then
            FIRST=0
        else
            echo "," >> "$RESULT_FILE"
        fi

        echo -n '    {"benchmark":"'$(basename "$bench" .claw)'","mode":"'$mode_name'","time_ms":'$elapsed_ms'}' >> "$RESULT_FILE"
    done
done

cat >> "$RESULT_FILE" <<EOF

  ]
}
EOF

echo ""
echo "Benchmark results written to $RESULT_FILE"
