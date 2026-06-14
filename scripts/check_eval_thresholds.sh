#!/usr/bin/env bash
# check_eval_thresholds.sh - fail CI if real-data EVAL metrics regress
set -euo pipefail

LOG_FILE="${1:-}"
MIN_UK_PAI5="${MIN_UK_PAI5:-6.0}"
MIN_WEATHER_HIT_RATE="${MIN_WEATHER_HIT_RATE:-0.5}"
MIN_UK_PASS_RATE="${MIN_UK_PASS_RATE:-0.85}"

all_lines=()
if [[ -n "$LOG_FILE" ]]; then
    if [[ ! -f "$LOG_FILE" ]]; then
        echo "Log file not found: $LOG_FILE" >&2
        exit 1
    fi
    mapfile -t all_lines < "$LOG_FILE"
else
    mapfile -t all_lines
fi

lines=()
for line in "${all_lines[@]}"; do
    case "$line" in
        *EVAL:*) lines+=("$line") ;;
    esac
done

if ((${#lines[@]} == 0)); then
    echo "No EVAL: lines found in input" >&2
    exit 1
fi

uk_pai5=""
weather_hit_rate=""
uk_pass_rate=""
failures=()

for line in "${lines[@]}"; do
    echo "$line"
    if [[ "$line" =~ EVAL:[[:space:]]+uk_rows=[0-9]+[[:space:]]+coords=[0-9]+[[:space:]]+dates=[0-9]+[[:space:]]+types=[0-9]+[[:space:]]+passRate=([0-9.]+) ]]; then
        uk_pass_rate="${BASH_REMATCH[1]}"
    fi
    if [[ "$line" =~ EVAL:[[:space:]]+uk_pai5=([0-9.]+) ]]; then
        uk_pai5="${BASH_REMATCH[1]}"
    fi
    if [[ "$line" =~ EVAL:[[:space:]]+weather_cached=[0-9]+[[:space:]]+lookups=[0-9]+[[:space:]]+hits=[0-9]+[[:space:]]+hitRate=([0-9.]+) ]]; then
        weather_hit_rate="${BASH_REMATCH[1]}"
    fi
done

if [[ -z "$uk_pai5" ]]; then
    failures+=("Missing EVAL uk_pai5 line")
elif awk -v v="$uk_pai5" -v m="$MIN_UK_PAI5" 'BEGIN { exit !(v < m) }'; then
    failures+=("uk_pai5=$uk_pai5 below minimum $MIN_UK_PAI5")
fi

if [[ -z "$weather_hit_rate" ]]; then
    failures+=("Missing EVAL weather hitRate line")
elif awk -v v="$weather_hit_rate" -v m="$MIN_WEATHER_HIT_RATE" 'BEGIN { exit !(v < m) }'; then
    failures+=("weather hitRate=$weather_hit_rate below minimum $MIN_WEATHER_HIT_RATE")
fi

if [[ -z "$uk_pass_rate" ]]; then
    failures+=("Missing EVAL uk passRate line")
elif awk -v v="$uk_pass_rate" -v m="$MIN_UK_PASS_RATE" 'BEGIN { exit !(v < m) }'; then
    failures+=("UK passRate=$uk_pass_rate below minimum $MIN_UK_PASS_RATE (uk pass issues)")
fi

if ((${#failures[@]} > 0)); then
    echo ""
    echo "EVAL threshold check FAILED:"
    for f in "${failures[@]}"; do
        echo "  - $f"
    done
    exit 1
fi

echo ""
echo "EVAL threshold check passed (uk_pai5=$uk_pai5, weather hitRate=$weather_hit_rate, uk passRate=$uk_pass_rate)."
