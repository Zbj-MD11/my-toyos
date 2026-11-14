#!/usr/bin/env bash
# 判定：
#   p1:%%    应输出 %
#   p2:%s NULL  应安全，输出空
#   p3:%q    未知格式符，不崩溃
#   p4:100%% 应输出 100%
#   p5:%s 空串  应输出空
# 当前实现 p1/p4 会失败（BUG-5），这个用例的 FAIL 是预期结果。
set -u
log="$1"

grep -q "^p1:%$"      "$log" || exit 1
grep -q "^p2:$"       "$log" || exit 1
grep -q "^p3:$"       "$log" || exit 1
grep -q "^p4:100%$"   "$log" || exit 1
grep -q "^p5:$"       "$log" || exit 1
exit 0
