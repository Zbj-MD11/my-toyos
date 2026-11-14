#!/usr/bin/env bash
# 把 main.c 里所有 sum_lk 的 acquire/release 删掉，模拟"没上锁"
set -eu
work="$1"

python3 - "$work" <<'PY'
import sys
p = sys.argv[1] + '/src/kernel/main.c'
lines = open(p).read().splitlines(True)
lines = [l for l in lines
         if 'spinlock_acquire(&sum_lk)' not in l
         and 'spinlock_release(&sum_lk)' not in l]
open(p, 'w').writelines(lines)
PY
