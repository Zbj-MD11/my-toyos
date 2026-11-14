#!/usr/bin/env bash
# 判定：五个边界值输出正确
set -u
log="$1"

grep -q "^b1:-2147483648$" "$log" || exit 1
grep -q "^b2:2147483647$"  "$log" || exit 1
grep -q "^b3:0$"           "$log" || exit 1
grep -q "^b4:-1$"          "$log" || exit 1
grep -q "^b5:0xffffffffffffffff$" "$log" || exit 1
exit 0
