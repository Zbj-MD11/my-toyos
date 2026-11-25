#!/usr/bin/env bash
# 用例 07：边界值。一次写 16 个字符 = 16550 FIFO 的容量，一个都不能丢。
# 注意两点：
#   1) 一次写 17 个会触发 FIFO overrun（QEMU 的 16550 模型直接丢字符），
#      那是硬件模型行为，不是组员代码的问题，所以这里只到 16；
#   2) 回车单独一次写、隔 0.3 秒，别和 16 个字符挤进同一个 FIFO 窗口。
sleep 1
printf '0123456789abcdef'
sleep 0.3
printf '\r'
sleep 1
