#!/usr/bin/env bash
# 在 print_init() 里插入一行格式测试
set -eu
work="$1"

python3 - "$work" <<'PY'
import sys
p = sys.argv[1] + '/src/kernel/lib/print.c'
s = open(p).read()
anchor = '    spinlock_init(&print_lk, "printf");'
assert anchor in s
add = r'''
    printf("fmt: %d %x %p %c %s\n", -123, 0x123456789abcdef0ULL, 42, 'A', "ok");
'''
open(p, 'w').write(s.replace(anchor, anchor + add, 1))
PY
