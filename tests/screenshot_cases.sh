#!/usr/bin/env bash
# ============================================================
# tests/screenshot_cases.sh —— 自动为每个用例截图
#
# 依赖: gnome-screenshot（sudo apt install -y gnome-screenshot）
# 产物: docs/testing/images/<用例名>.png
#
# 原理: 逐个显示用例结果 → 截取整个屏幕 → 下一项
# ============================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

OUT="$ROOT/docs/testing/images"
mkdir -p "$OUT"

if ! command -v gnome-screenshot >/dev/null 2>&1; then
    echo "缺少 gnome-screenshot，请先执行:"
    echo "  sudo apt install -y gnome-screenshot"
    exit 1
fi

if [ ! -d "$ROOT/tests/results" ]; then
    echo "还没有测试结果，请先运行: bash tests/run_tests.sh"
    exit 1
fi

for d in "$ROOT"/tests/cases/*/; do
    id="$(basename "$d")"
    echo "正在截图: $id"
    bash "$ROOT/tests/show_case.sh" "$id"
    sleep 1
    gnome-screenshot -f "$OUT/$id.png" >/dev/null 2>&1
    if [ -s "$OUT/$id.png" ]; then
        echo "  已保存 docs/testing/images/$id.png"
    else
        echo "  截图失败: $id"
    fi
    sleep 1
    clear
done

echo "============================================================"
echo " 截图完成，文件列表:"
ls -l "$OUT"
echo "============================================================"
