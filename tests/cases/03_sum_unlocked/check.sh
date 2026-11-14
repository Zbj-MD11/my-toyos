#!/usr/bin/env bash
# 判定：两核都启动，但 sum 的最大值明显小于 2000000（丢更新）
set -u
log="$1"

grep -q "cpu 0 is booting!" "$log" || exit 1
grep -q "cpu 1 is booting!" "$log" || exit 1

max="$(grep -o "sum = [0-9]*" "$log" | awk '{print $3}' | sort -n | tail -1)"
[ -n "$max" ] || exit 1
[ "$max" -gt 0 ] || exit 1
[ "$max" -lt 2000000 ]
