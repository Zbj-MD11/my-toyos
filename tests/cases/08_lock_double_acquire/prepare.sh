#!/usr/bin/env bash
# 在 CPU0 初始化 sum_lk 之后，连续 acquire 两次
set -eu
work="$1"

python3 - "$work" <<'PY'
import sys
p = sys.argv[1] + '/src/kernel/main.c'
s = open(p).read()
anchor = '        spinlock_init(&sum_lk,"yj");'
assert anchor in s, 'anchor not found'
add = '''
        spinlock_acquire(&sum_lk);
        spinlock_acquire(&sum_lk);
'''
open(p, 'w').write(s.replace(anchor, anchor + add, 1))
PY
