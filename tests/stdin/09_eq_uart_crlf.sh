#!/usr/bin/env bash
# 用例 09：\r / 孤立 \n / CRLF 三种换行输入。
# 期望屏幕上是三行 x、y、z，且只产生 3 个换行（z 的 \r\n 不能变成两个换行）。
sleep 1
printf 'x\r'
sleep 0.15
printf 'y\n'
sleep 0.15
printf 'z\r\n'
sleep 1
