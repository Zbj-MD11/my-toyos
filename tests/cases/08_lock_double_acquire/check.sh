#!/usr/bin/env bash
# 判定：打印 panic，并指出重复加锁
set -u
log="$1"

grep -q "panic!" "$log" || exit 1
grep -qi "hold the lock" "$log" || exit 1
exit 0
