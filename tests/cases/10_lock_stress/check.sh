#!/usr/bin/env bash
# 判定：两个 hart 都有报告；最大值必须等于 400000（无丢更新）
set -u
log="$1"

cnt="$(grep -c "stress: cpu" "$log")"
[ "$cnt" -eq 2 ] || exit 1

max="$(grep -o "sum = [0-9]*" "$log" | awk '{print $3}' | sort -n | tail -1)"
[ "$max" = "400000" ] || exit 1
exit 0
