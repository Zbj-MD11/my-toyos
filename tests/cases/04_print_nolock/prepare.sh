#!/usr/bin/env bash
# 去掉 print.c 里 print_lk 的 acquire/release，模拟"没有输出锁"
set -eu
work="$1"

python3 - "$work" <<'PY'
import sys
p = sys.argv[1] + '/src/kernel/lib/print.c'
lines = open(p).read().splitlines(True)
lines = [l for l in lines
         if 'spinlock_acquire(&print_lk)' not in l
         and 'spinlock_release(&print_lk)' not in l]
open(p, 'w').writelines(lines)
PY
