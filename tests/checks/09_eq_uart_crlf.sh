#!/usr/bin/env bash
# 用例 09 判定：x\r / y\n / z\r\n -> 屏幕三行 x、y、z，且总共只有 3 个换行。
# （如果实现把 CRLF 当两次换行，crlf 会变成 4，这里会 FAIL）
set -u
log="$1"
exec python3 "$(dirname "$0")/screen.py" --log "$log" \
    --begin "--- begin input ---" --end "--- end input ---" \
    --expect-lines "x|y|z" --crlf 3
