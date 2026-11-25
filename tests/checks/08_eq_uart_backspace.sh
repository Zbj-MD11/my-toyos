#!/usr/bin/env bash
# 用例 08 判定：a b BS c 回车 -> 屏幕最后一行必须是 "ac"，且只产生 1 个换行。
set -u
log="$1"
exec python3 "$(dirname "$0")/screen.py" --log "$log" \
    --begin "--- begin input ---" --end "--- end input ---" \
    --expect "ac" --crlf 1
