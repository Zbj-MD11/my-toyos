#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把串口日志里"输入窗口"内的一段输出还原成终端屏幕，用于判定退格/换行类用例。

为什么需要它：
  串口日志是原始字节流，退格在日志里是 "\b \b" 三个字节，直接 grep 看不出
  "屏幕上到底剩了什么"。这里按终端语义重放一遍：\b 左移一列、\r 回到行首、
  \n 提交一行，其余字符覆盖写入。

用法（由 tests/checks/*.sh 调用）：
  python3 screen.py --log LOG --begin "--- begin input ---" --end "--- end input ---" \
          --expect "ac" --crlf 1
  --expect       最后一行的内容（比对前会去掉行尾空格）；不给就不比
  --expect-lines  用 | 连接的全部行，例如 "x|y|z"
  --crlf N       窗口内 "\r\n" 必须出现 N 次
  --bs N         窗口内 "\b \b" 必须出现 N 次
  --max-lines N  最多 N 行非空输出

输出一行结果（run_tests.sh 会把它追加成 "[check] PASS/FAIL ..."），退出码 0/1。
"""

import argparse
import sys


def load_region(path, begin, end):
    # newline="" 很关键：不能把 \r 当成换行吃掉
    with open(path, "r", encoding="utf-8", errors="replace", newline="") as f:
        text = f.read()

    if begin:
        i = text.find(begin)
        if i < 0:
            raise SystemExit("找不到开始标记 %r" % begin)
        i = text.find("\n", i)
        if i < 0:
            raise SystemExit("开始标记后面没有换行")
        text = text[i + 1:]

    if end:
        j = text.find(end)
        if j >= 0:
            text = text[:j]

    return text


def render(text):
    lines = []
    buf = []
    pos = 0

    for ch in text:
        if ch == "\r":
            pos = 0
        elif ch == "\n":
            lines.append("".join(buf).rstrip())
            buf = []
            pos = 0
        elif ch == "\b":
            if pos > 0:
                pos -= 1
        elif ch == "\t":
            # 本次用例不喂 tab，简单按"一个空格"处理
            if pos < len(buf):
                buf[pos] = " "
            else:
                buf.append(" ")
            pos += 1
        else:
            if pos < len(buf):
                buf[pos] = ch
            else:
                buf.append(ch)
            pos += 1

    lines.append("".join(buf).rstrip())

    # 去掉尾部空行（最后一个换行会产生一行空串）
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--begin", default="--- begin input ---")
    ap.add_argument("--end", default="--- end input ---")
    ap.add_argument("--expect", default=None)
    ap.add_argument("--expect-lines", default=None)
    ap.add_argument("--crlf", type=int, default=None)
    ap.add_argument("--bs", type=int, default=None)
    ap.add_argument("--max-lines", type=int, default=None)
    args = ap.parse_args()

    region = load_region(args.log, args.begin, args.end)
    lines = render(region)

    problems = []
    if args.expect is not None:
        last = lines[-1] if lines else ""
        if last != args.expect:
            problems.append("last=%r want %r" % (last, args.expect))
    if args.expect_lines is not None:
        got = "|".join(lines)
        if got != args.expect_lines:
            problems.append("lines=%r want %r" % (got, args.expect_lines))
    if args.crlf is not None:
        n = region.count("\r\n")
        if n != args.crlf:
            problems.append("crlf=%d want %d" % (n, args.crlf))
    if args.bs is not None:
        n = region.count("\b \b")
        if n != args.bs:
            problems.append("bs=%d want %d" % (n, args.bs))
    if args.max_lines is not None:
        n = len([x for x in lines if x])
        if n > args.max_lines:
            problems.append("nonempty_lines=%d want <= %d" % (n, args.max_lines))

    summary = 'rendered=%r crlf=%d bs=%d' % (
        lines,
        region.count("\r\n"),
        region.count("\b \b"),
    )
    if problems:
        print("FAIL %s %s" % (summary, " ".join(problems)))
        return 1
    print("PASS %s" % summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
