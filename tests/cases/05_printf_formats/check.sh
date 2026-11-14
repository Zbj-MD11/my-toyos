#!/usr/bin/env bash
# 判定：%d %x %p %c %s 均符合约定
set -u
log="$1"

grep -q "^fmt: -123 0x123456789abcdef0 2a A ok$" "$log"
