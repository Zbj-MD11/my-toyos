#!/usr/bin/env bash
# 用例 19：soak。25 个 tick（约 2.5 秒）里分三段送 "soak-ok" 再回车，
# 期间时钟中断一直在走：检查不丢字符、不丢 tick、不进死锁。
sleep 1
printf 'soak-'
sleep 0.2
printf 'ok'
sleep 0.2
printf '\r'
sleep 1.5
