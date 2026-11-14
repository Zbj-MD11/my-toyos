#!/usr/bin/env bash
# 判定：只出现 cpu 0；不出现 cpu 1；sum 为 1000000
set -u
log="$1"

grep -q "cpu 0 is booting!" "$log" || exit 1
grep -q "cpu 1 is booting!" "$log" && exit 1
grep -q "sum = 1000000" "$log" || exit 1
exit 0
