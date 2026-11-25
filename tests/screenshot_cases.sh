#!/usr/bin/env bash
# ============================================================
# tests/screenshot_cases.sh —— 为 tests/screenshot.list 里的用例自动截图
#
# 依赖: gnome-screenshot（sudo apt install -y gnome-screenshot）
# 产物: docs/testing/images/<用例名>.png
#
# 前置: 先跑过 bash tests/run_tests.sh（结果在 tests/results/）
# ============================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

OUT="$ROOT/docs/testing/images"
LIST="$ROOT/tests/screenshot.list"
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

while read -r id; do
    [ -z "$id" ] && continue
    case "$id" in \#*) continue ;; esac
    echo "正在截图: $id"
    clear
    bash "$ROOT/tests/show_case.sh" "$id" < /dev/null
    sleep 1
    gnome-screenshot -f "$OUT/$id.png" >/dev/null 2>&1
    if [ -s "$OUT/$id.png" ]; then
        echo ""
        echo "  已保存 docs/testing/images/$id.png"
    else
        echo "  截图失败: $id"
    fi
    sleep 1
done < "$LIST"

echo "============================================================"
echo " 截图完成，文件列表:"
ls -1 "$OUT"
echo "============================================================"
