#!/usr/bin/env bash
# run_real_data_tests.sh — one-command real-data test runner (Linux/macOS)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
DATA="$ROOT/data"

echo "SENTINEL real-data test runner"
echo "Project: $ROOT"

required=(
    "crimes/uk_metropolitan_street_2024.csv"
    "crimes/cincinnati_pdi_crimes_sample.csv"
    "crimes/sfpd_incidents_sample.csv"
    "crimes/london_crimes_2024.csv"
    "co_offending/moreno_person_crime.csv"
    "co_offending/chicago_co_offending.csv"
    "weather/london_2024_h1.json"
    "manifest.json"
)

missing=()
for f in "${required[@]}"; do
    if [[ ! -f "$DATA/$f" ]]; then
        missing+=("$f")
    fi
done

if ((${#missing[@]} > 0)); then
    echo "Missing datasets — running fetch script first..."
    python3 "$DATA/scripts/fetch_datasets.py"
    missing=()
    for f in "${required[@]}"; do
        if [[ ! -f "$DATA/$f" ]]; then
            missing+=("$f")
        fi
    done
    if ((${#missing[@]} > 0)); then
        echo "Still missing datasets:" >&2
        printf '  %s\n' "${missing[@]}" >&2
        exit 1
    fi
fi

if [[ ! -d "$BUILD" ]]; then
    cmake -B "$BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="${CC:-clang}" \
        -DCMAKE_CXX_COMPILER="${CXX:-clang++}"
fi

cmake --build "$BUILD" --target test_local_datasets test_real_data_evaluation -j"$(nproc 2>/dev/null || echo 4)"

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

echo ""
echo "--- test_local_datasets ---"
"$BUILD/tests/test_local_datasets"
local_exit=$?

echo ""
echo "--- test_real_data_evaluation (metrics on stderr) ---"
eval_log="$(mktemp)"
set +e
"$BUILD/tests/test_real_data_evaluation" 2>&1 | tee "$eval_log"
eval_exit=${PIPESTATUS[0]}
set -e
grep "EVAL:" "$eval_log" || true

if [[ $local_exit -ne 0 || $eval_exit -ne 0 ]]; then
    echo "FAILED local=$local_exit eval=$eval_exit" >&2
    rm -f "$eval_log"
    exit 1
fi

echo ""
echo "--- EVAL threshold check ---"
chmod +x "$ROOT/scripts/check_eval_thresholds.sh"
grep "EVAL:" "$eval_log" > /tmp/sentinel-eval-lines.log
"$ROOT/scripts/check_eval_thresholds.sh" /tmp/sentinel-eval-lines.log
rm -f "$eval_log" /tmp/sentinel-eval-lines.log

echo ""
echo "All real-data tests passed."
echo "See docs/REAL_DATA_EVALUATION.md for interpretation."
