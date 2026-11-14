#!/usr/bin/env bash
# 判定：打印 panic，并指出当前 CPU 没有持有锁
set -u
log="$1"

grep -q "panic!" "$log" || exit 1
grep -qi "does not hold the lock" "$log" || exit 1
exit 0
