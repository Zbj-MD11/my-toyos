#!/usr/bin/env bash
# 插入数值边界测试
set -eu
work="$1"

python3 - "$work" <<'PY'
import sys
p = sys.argv[1] + '/src/kernel/lib/print.c'
s = open(p).read()
anchor = '    spinlock_init(&print_lk, "printf");'
assert anchor in s
add = r'''
    printf("b1:%d\n", -2147483647 - 1);
    printf("b2:%d\n", 2147483647);
    printf("b3:%d\n", 0);
    printf("b4:%d\n", -1);
    printf("b5:%x\n", 0xffffffffffffffffULL);
'''
open(p, 'w').write(s.replace(anchor, anchor + add, 1))
PY
