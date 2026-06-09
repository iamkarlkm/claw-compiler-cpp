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
    "aot:--aot"
)

TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
GIT_SHA=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")

RESULT_FILE="$OUTDIR/benchmark_${TIMESTAMP}.json"

echo "{"
echo '  "timestamp": "'"$TIMESTAMP"'",'
echo '  "git_sha": "'"$GIT_SHA"'",'
echo '  "results": ['

FIRST=1
for bench in "${BENCHMARKS[@]}"; do
    for mode_spec in "${MODES[@]}"; do
        IFS=':' read -r mode_name mode_flag <<< "$mode_spec"

        # Skip AOT if not supported for this benchmark
        if [ "$mode_name" = "aot" ]; then
            if ! $CLAW --aot "$bench" >/dev/null 2>&1; then
                continue
            fi
            # AOT produces binary, run it
            bin_name="aot_$(basename "$bench" .claw)"
            if [ ! -f "$bin_name" ]; then
                continue
            fi
        fi

        # Warmup run
        if [ "$mode_name" = "aot" ]; then
            bin_name="aot_$(basename "$bench" .claw)"
            ./"$bin_name" >/dev/null 2>&1 || true
        else
            $CLAW $mode_flag "$bench" >/dev/null 2>&1 || true
        fi

        # Multiple runs for statistics
        times=()
        for i in 1 2 3; do
            start=$(date +%s%N)
            if [ "$mode_name" = "aot" ]; then
                bin_name="aot_$(basename "$bench" .claw)"
                ./"$bin_name" >/dev/null 2>&1 || true
            else
                $CLAW $mode_flag "$bench" >/dev/null 2>&1 || true
            fi
            end=$(date +%s%N)
            elapsed_ms=$(( (end - start) / 1000000 ))
            times+=("$elapsed_ms")
        done

        # Calculate average and min
        total=0
        min=${times[0]}
        for t in "${times[@]}"; do
            total=$((total + t))
            if [ "$t" -lt "$min" ]; then
                min=$t
            fi
        done
        avg=$((total / ${#times[@]}))

        if [ "$FIRST" -eq 1 ]; then
            FIRST=0
        else
            echo ","
        fi
        echo -n '    {"benchmark":"'$(basename "$bench" .claw)'","mode":"'$mode_name'","avg_ms":'$avg',"min_ms":'$min',"runs":['"${times[0]}"','"${times[1]}"','"${times[2]}"']}'
    done
done

echo ""
echo "  ]"
echo "}"
