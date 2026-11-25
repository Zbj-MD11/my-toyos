#!/usr/bin/env bash
# 用例 08：退格 0x08。输入 a b BS c 回车 -> 屏幕最终应是 "ac"。
sleep 1
printf 'ab\bc\r'
sleep 1
