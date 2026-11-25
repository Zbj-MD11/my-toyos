#!/usr/bin/env bash
# 用例 10：边界值。行首退格（line_len == 0）。
# 输入 a DEL DEL b 回车：第一个 DEL 删掉 a，第二个 DEL 在空行上必须什么都不输出，
# 最后屏幕只有 "b"，并且整个输入窗口里只出现一次 "\b \b"。
sleep 1
printf 'a\177\177b\r'
sleep 1
