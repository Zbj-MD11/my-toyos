#!/usr/bin/env bash
# ============================================================
# tests/show_case.sh —— 单独展示一个用例的测试过程与结果
#
# 用法:
#   bash tests/show_case.sh 01_boot_dual
#
# 用途: 截图。屏幕会显示用例说明、启动参数、完整输出和判定，
#       一屏就是一张"测试证据"。
# ============================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

id="${1:-}"
if [ -z "$id" ]; then
    echo "用法: bash tests/show_case.sh <用例名>"
    echo
    echo "可用用例:"
    for d in "$ROOT"/tests/cases/*/; do
        echo "  $(basename "$d")"
    done
    exit 1
fi

dir="$ROOT/tests/cases/$id"
log="$ROOT/tests/results/$id.log"

if [ ! -d "$dir" ]; then
    echo "没有这个用例: $id"
    exit 1
fi

if [ ! -f "$log" ]; then
    echo "还没有结果，先运行:"
    echo "  bash tests/run_tests.sh $id"
    exit 1
fi

TITLE="$id"
ARGS=""
TIMEOUT=0
# shellcheck disable=SC1090
. "$dir/case.conf"

echo "============================================================"
echo " 用例: $id"
echo " 说明: $TITLE"
echo " 参数: -machine virt -nographic $ARGS"
echo "============================================================"
echo " 输出（最多 40 行，完整日志: tests/results/$id.log）"
echo "------------------------------------------------------------"
head -n 40 "$log"
echo "============================================================"
if bash "$dir/check.sh" "$log" >/dev/null 2>&1; then
    echo " 判定: PASS"
else
    echo " 判定: FAIL"
fi
echo "============================================================"
