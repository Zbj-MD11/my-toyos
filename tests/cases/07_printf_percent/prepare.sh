#!/usr/bin/env bash
# 插入特殊格式测试
set -eu
work="$1"

python3 - "$work" <<'PY'
import sys
p = sys.argv[1] + '/src/kernel/lib/print.c'
s = open(p).read()
anchor = '    spinlock_init(&print_lk, "printf");'
assert anchor in s
add = r'''
    printf("p1:%%\n");
    printf("p2:%s\n", (char*)0);
    printf("p3:%q\n", 1);
    printf("p4:100%%\n");
    printf("p5:%s\n", "");
'''
open(p, 'w').write(s.replace(anchor, anchor + add, 1))
PY
