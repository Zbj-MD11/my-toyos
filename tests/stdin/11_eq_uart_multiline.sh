#!/usr/bin/env bash
# 用例 11：连续三行。如果 plic_claim 之后忘了 plic_complete，
# 只有第一行能回显，后面两行不会再有串口中断。
sleep 1
printf 'one\r'
sleep 0.4
printf 'two\r'
sleep 0.4
printf 'three\r'
sleep 0.6
