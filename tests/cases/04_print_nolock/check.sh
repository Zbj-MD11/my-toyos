#!/usr/bin/env bash
# 判定：出现同一行里既有 A 又有 B 的输出（字符级交错）
set -u
log="$1"

grep -Eq "A.*B|B.*A" "$log"
