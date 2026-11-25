#!/usr/bin/env bash
# 用例 12 判定：只送一个 \r（空行）-> 屏幕上没有任何非空内容，且只有一个换行。
# 如果实现把空行吞掉了、或者卡住，这里的输出窗口会是空的/缺换行。
set -u
log="$1"
exec python3 "$(dirname "$0")/screen.py" --log "$log" \
    --begin "--- begin input ---" --end "--- end input ---" \
    --max-lines 0 --crlf 1
