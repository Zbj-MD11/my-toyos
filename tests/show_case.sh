#!/usr/bin/env bash
# ============================================================
# tests/show_case.sh —— 一屏展示一个用例的说明 / 输出 / 判定
#
# 用法:
#   bash tests/show_case.sh 15_kvm_map_single
#
# 用途: 截图。屏幕上的内容就是"测试证据"：用例说明、启动参数、
#       真实串口输出、PASS/FAIL 判定。
# ============================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

id="${1:-}"
if [ -z "$id" ]; then
    echo "用法: bash tests/show_case.sh <用例名>"
    echo
    echo "可用用例:"
    sed '1s/^\xEF\xBB\xBF//; s/\r$//' "$ROOT/tests/cases.tsv" | grep -v '^#' | cut -d'|' -f1 | sed 's/^/  /'
    exit 1
fi

log="$ROOT/tests/results/$id.log"
row="$(sed '1s/^\xEF\xBB\xBF//; s/\r$//' "$ROOT/tests/cases.tsv" | awk -F'|' -v id="$id" '$1 == id')"

if [ -z "$row" ]; then
    echo "没有这个用例: $id"
    exit 1
fi

if [ ! -f "$log" ]; then
    echo "还没有结果，先运行:"
    echo "  bash tests/run_tests.sh $id"
    exit 1
fi

IFS='|' read -r _ stage _ smp _ must mustnot title <<< "$row"

ok=1
oldifs="$IFS"
IFS=';'
for p in $must; do
    grep -Fq -- "$p" "$log" || ok=0
done
for p in $mustnot; do
    [ -z "$p" ] && continue
    grep -Fq -- "$p" "$log" && ok=0
done
IFS="$oldifs"

echo "============================================================"
echo " 用例: $id"
echo " 说明: $title"
echo " 参数: -machine virt -bios none -m 128M -smp $smp -nographic"
echo " 阶段: $stage（tests/templates/main_$stage.c）"
echo "============================================================"
echo " 串口输出（最多 32 行，完整日志 tests/results/$id.log）"
echo "------------------------------------------------------------"
head -n 32 "$log"
echo "------------------------------------------------------------"
if [ "$ok" -eq 1 ]; then
    echo " 判定: PASS"
else
    echo " 判定: FAIL"
fi
echo "============================================================"
