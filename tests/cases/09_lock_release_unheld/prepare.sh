#!/usr/bin/env bash
# 在 CPU0 初始化 sum_lk 之后，直接 release（没有 acquire）
set -eu
work="$1"

python3 - "$work" <<'PY'
import sys
p = sys.argv[1] + '/src/kernel/main.c'
s = open(p).read()
anchor = '        spinlock_init(&sum_lk,"yj");'
assert anchor in s, 'anchor not found'
add = '''
        spinlock_release(&sum_lk);
'''
open(p, 'w').write(s.replace(anchor, anchor + add, 1))
PY
