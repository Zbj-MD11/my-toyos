#!/usr/bin/env bash
# 用例 10 判定：a DEL DEL b -> 屏幕只有 "b"，且整个窗口只出现一次 "\b \b"。
# 行首退格如果处理错了（比如照样输出退格、或者 line_len 变成负数），这里的
# bs 次数或最后一行就会对不上。
set -u
log="$1"
exec python3 "$(dirname "$0")/screen.py" --log "$log" \
    --begin "--- begin input ---" --end "--- end input ---" \
    --expect "b" --bs 1
