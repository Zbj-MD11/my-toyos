#!/usr/bin/env bash
# 用例 18：双核下的串口输入。-smp 2 时只有一个 hart 能 claim 到 UART IRQ，
# 但 uart_intr 的 line_len / last_was_cr 是全局静态变量，要确认换行状态没串。
sleep 1.5
printf 'smp\r'
sleep 1
