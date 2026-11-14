#!/usr/bin/env bash
# 判定：两个 hart 都启动；两次 sum 报告的最大值为 2000000
set -u
log="$1"

grep -q "cpu 0 is booting!" "$log" || exit 1
grep -q "cpu 1 is booting!" "$log" || exit 1

max="$(grep -o "sum = [0-9]*" "$log" | awk '{print $3}' | sort -n | tail -1)"
[ "$max" = "2000000" ]
