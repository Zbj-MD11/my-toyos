#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 tests/results/*.log 渲染成"终端风格"的 PNG，用于测试文档里的截图。

为什么不用 gnome-screenshot：
  - 不需要桌面会话，headless（ssh）也能出图；
  - 38 张图一次生成，内容就是真实的串口日志，不会截到别的窗口。

用法：
  python3 tests/render_shot.py                  # 渲染 tests/results/ 全部用例
  python3 tests/render_shot.py --only 13_pmem_double_free
  python3 tests/render_shot.py --results DIR --out DIR   # 调试用

依赖：pillow（sudo apt install -y python3-pil）
"""

import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BG = (18, 22, 26)
TITLE_BG = (38, 44, 50)
FG = (218, 222, 226)
DIM = (128, 138, 148)
GREEN = (120, 200, 120)
RED = (236, 110, 110)
YELLOW = (238, 205, 130)

WIDTH = 1120
PAD = 26
LINE_H = 22
FONT_SIZE = 15
TITLE_H = 46
FOOTER_H = 30
MAX_LINES = 64

MONO_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
    r"C:\Windows\Fonts\consola.ttf",
]

CJK_CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
]


def pick_font(candidates, size):
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size), True
            except OSError:
                continue
    return ImageFont.load_default(), False


def is_ascii(text):
    try:
        text.encode("ascii")
        return True
    except UnicodeEncodeError:
        return False


def wrap(text, width):
    """按显示宽度粗略折行（中文按 2 个字符宽算）"""
    if not text:
        return [""]
    lines = []
    cur = ""
    w = 0
    for ch in text:
        cw = 1 if ord(ch) < 128 else 2
        if w + cw > width and cur:
            lines.append(cur)
            cur, w = ch, cw
        else:
            cur += ch
            w += cw
    lines.append(cur)
    return lines


def load_cases(path):
    rows = {}
    with open(path, encoding="utf-8-sig") as f:
        for line in f.read().replace("\r", "").splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("|")
            if len(parts) != 8:
                continue
            cid, stage, fn, smp, tmo, must, mustnot, title = parts
            rows[cid] = dict(
                stage=stage, smp=smp, must=must, mustnot=mustnot, title=title
            )
    return rows


def verdict(log, must, mustnot):
    for p in [x for x in must.split(";") if x]:
        if p not in log:
            return False
    for p in [x for x in mustnot.split(";") if x]:
        if p and p in log:
            return False
    return True


def color_of(line):
    if line.startswith("panic!"):
        return RED
    if "RESULT: PASS" in line:
        return GREEN
    if line.startswith("=") or line.startswith("-"):
        return DIM
    return FG


def render(lines, title, right, subtitle, out_path, body_font, cjk_font, has_cjk):
    wrapped = []
    for ln in lines:
        wrapped.extend(wrap(ln, 118))
    if len(wrapped) > MAX_LINES:
        wrapped = wrapped[:MAX_LINES] + ["... (共 %d 行，完整日志见 tests/results/)" % len(wrapped)]

    height = TITLE_H + PAD * 2 + len(wrapped) * LINE_H + FOOTER_H
    img = Image.new("RGB", (WIDTH, height), BG)
    d = ImageDraw.Draw(img)

    d.rectangle([0, 0, WIDTH, TITLE_H], fill=TITLE_BG)
    # 标题栏左侧三个小圆点，模仿终端窗口
    for i, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        d.ellipse([PAD + i * 22, 17, PAD + i * 22 + 12, 29], fill=c)

    tfont = cjk_font if has_cjk else body_font
    d.text((PAD + 78, 13), title, font=tfont, fill=FG)
    if right:
        tw = d.textlength(right, font=body_font)
        d.text((WIDTH - PAD - tw, 14), right, font=body_font, fill=GREEN if right == "PASS" else RED)

    y = TITLE_H + PAD
    for ln in wrapped:
        lf = body_font if (is_ascii(ln) or not has_cjk) else cjk_font
        d.text((PAD, y), ln, font=lf, fill=color_of(ln))
        y += LINE_H

    d.line([(0, height - FOOTER_H), (WIDTH, height - FOOTER_H)], fill=TITLE_BG)
    d.text((PAD, height - FOOTER_H + 7), subtitle, font=tfont, fill=DIM)

    img.save(out_path)
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default=os.path.join(ROOT, "tests", "results"))
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "testing", "images"))
    ap.add_argument("--cases", default=os.path.join(ROOT, "tests", "cases.tsv"))
    ap.add_argument("--only", default="")
    args = ap.parse_args()

    cases = load_cases(args.cases)
    os.makedirs(args.out, exist_ok=True)

    body_font, _ = pick_font(MONO_CANDIDATES, FONT_SIZE)
    cjk_font, has_cjk = pick_font(CJK_CANDIDATES, FONT_SIZE + 1)

    made = 0
    for cid, meta in cases.items():
        if args.only and cid != args.only:
            continue
        log_path = os.path.join(args.results, cid + ".log")
        if not os.path.exists(log_path):
            continue

        with open(log_path, encoding="utf-8-sig", errors="replace") as f:
            text = f.read().replace("\r", "").lstrip("\ufeff").rstrip("\n")
        ok = verdict(text, meta["must"], meta["mustnot"])
        title = "%s  %s" % (cid, meta["title"] if has_cjk else "")
        right = "PASS" if ok else "FAIL"
        subtitle = "-machine virt -bios none -m 128M -smp %s -nographic    %s" % (
            meta["smp"],
            "判定: " + right + ("（与 tests/cases.tsv 规则一致）" if has_cjk else ""),
        )
        out = render(
            text.split("\n"),
            title.strip(),
            right,
            subtitle if has_cjk else "-machine virt -bios none -m 128M -smp %s -nographic  %s" % (meta["smp"], right),
            os.path.join(args.out, cid + ".png"),
            body_font,
            cjk_font,
            has_cjk,
        )
        made += 1
        print("  %s" % out)

    summary = os.path.join(args.results, "summary.txt")
    if os.path.exists(summary) and not args.only:
        with open(summary, encoding="utf-8-sig", errors="replace") as f:
            text = f.read().replace("\r", "").lstrip("\ufeff").rstrip("\n")
        render(
            text.split("\n"),
            "bash tests/run_tests.sh  —— 汇总",
            "",
            "lab-2 内存管理测试汇总（tests/results/summary.txt）" if has_cjk else "lab-2 summary",
            os.path.join(args.out, "00-summary.png"),
            cjk_font if has_cjk else body_font,
            cjk_font,
            has_cjk,
        )
        made += 1
        print("  %s" % os.path.join(args.out, "00-summary.png"))

    if not has_cjk:
        print("提示: 没找到中文字体，图片标题只保留用例 id（sudo apt install -y fonts-noto-cjk）")
    print("共生成 %d 张图片 -> %s" % (made, args.out))
    if made == 0:
        print("没有可渲染的日志，先跑: bash tests/run_tests.sh")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
