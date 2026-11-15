#!/usr/bin/env bash
# ============================================================
# tests/run_tests.sh —— lab-2 内存管理测试
#
# 用法:
#   bash tests/run_tests.sh                     # 跑全部用例
#   bash tests/run_tests.sh 15_kvm_map_single   # 只跑一个用例
#   bash tests/run_tests.sh 15_kvm_map_single 17_kvm_map_len_boundary
#
# 每个用例:
#   1. 把 Makefile / common.mk / kernel.ld / src 复制到 tests/.work/<用例>
#   2. 按 stage 注入 main.c（见 tests/templates/）和 tests/suite/lab2_tests.c
#   3. make clean && make build
#   4. QEMU 限时运行，日志存 tests/results/<用例>.log
#   5. 用 cases.tsv 里的 must / mustnot 规则判定 PASS / FAIL
#
# 注意: 所有会 panic 的用例由 timeout 结束，退出码非 0 是预期行为。
# ============================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

WORK="$ROOT/tests/.work"
RESULT="$ROOT/tests/results"
TABLE="$ROOT/tests/cases.tsv"
FILTER="$*"

rm -rf "$WORK" "$RESULT"
mkdir -p "$WORK" "$RESULT"
# 兜底：去掉 Windows 侧编辑可能带来的 CRLF / UTF-8 BOM
sed '1s/^\xEF\xBB\xBF//; s/\r$//' "$TABLE" > "$WORK/cases.tsv"

echo "============================================================"
echo " lab-2 内存管理测试    $(date '+%F %T')"
echo " 编译器: $(riscv64-linux-gnu-gcc --version 2>/dev/null | head -n1)"
echo " QEMU  : $(qemu-system-riscv64 --version 2>/dev/null | head -n1)"
echo "============================================================"
printf "%-30s %-6s %s\n" "用例" "结果" "说明"

# 汇总文件由本脚本自己写（不要用 tee：本脚本开头会 rm -rf results）
SUMMARY="$RESULT/summary.txt"
{
    echo "============================================================"
    echo " lab-2 内存管理测试    $(date '+%F %T')"
    echo " 编译器: $(riscv64-linux-gnu-gcc --version 2>/dev/null | head -n1)"
    echo " QEMU  : $(qemu-system-riscv64 --version 2>/dev/null | head -n1)"
    echo "============================================================"
    printf "%-30s %-6s %s\n" "用例" "结果" "说明"
} > "$SUMMARY"

save_row() {
    printf "%-30s %-6s %s\n" "$1" "$2" "$3" >> "$SUMMARY"
}

PASS=0
FAIL=0
FAILED=""

selected() {
    [ -z "$FILTER" ] && return 0
    for f in $FILTER; do
        [ "$f" = "$1" ] && return 0
    done
    return 1
}

while IFS='|' read -r id stage fn smp tmo must mustnot title; do
    [ -z "$id" ] && continue
    case "$id" in \#*) continue ;; esac
    selected "$id" || continue

    work="$WORK/$id"
    mkdir -p "$work"
    cp "$ROOT/Makefile" "$ROOT/common.mk" "$ROOT/kernel.ld" "$work"/
    cp -r "$ROOT/src" "$work"/

    if [ "$stage" != "orig" ]; then
        cp "$ROOT/tests/suite/lab2_tests.c" "$work/src/kernel/lab2_tests.c"
        sed "s/@CASE@/$fn/g" "$ROOT/tests/templates/main_$stage.c" > "$work/src/kernel/main.c"
    fi

    if ! ( cd "$work" && make clean > build.log 2>&1 && make build >> build.log 2>&1 ) < /dev/null; then
        printf "%-30s %-6s %s\n" "$id" "FAIL" "编译失败: $title"
        save_row "$id" "FAIL" "编译失败: $title"
        cp "$work/build.log" "$RESULT/$id.build.log" 2>/dev/null
        FAIL=$((FAIL + 1))
        FAILED="$FAILED $id"
        continue
    fi

    # < /dev/null 必须保留：否则 -nographic 的 QEMU 可能被挂起，表现为零输出
    timeout --foreground "$tmo" qemu-system-riscv64 \
        -machine virt -bios none -m 128M -smp "$smp" -nographic \
        -kernel "$work/target/kernel/kernel-qemu.elf" \
        < /dev/null > "$work/run.log" 2>&1
    cp "$work/run.log" "$RESULT/$id.log"

    ok=1
    oldifs="$IFS"
    IFS=';'
    for p in $must; do
        grep -Fq -- "$p" "$work/run.log" || ok=0
    done
    for p in $mustnot; do
        [ -z "$p" ] && continue
        grep -Fq -- "$p" "$work/run.log" && ok=0
    done
    IFS="$oldifs"

    if [ "$ok" -eq 1 ]; then
        printf "%-30s %-6s %s\n" "$id" "PASS" "$title"
        save_row "$id" "PASS" "$title"
        PASS=$((PASS + 1))
    else
        printf "%-30s %-6s %s\n" "$id" "FAIL" "$title"
        save_row "$id" "FAIL" "$title"
        FAIL=$((FAIL + 1))
        FAILED="$FAILED $id"
        if [ ! -s "$work/run.log" ]; then
            echo "        提示: 串口零输出，先看 tests/README.md 的「启动零输出」一节"
        fi
    fi
done < "$WORK/cases.tsv"

echo "============================================================"
if [ $((PASS + FAIL)) -eq 0 ]; then
    echo " 没有匹配到任何用例（检查参数或 tests/cases.tsv）"
    echo " 没有匹配到任何用例" >> "$SUMMARY"
    exit 1
fi
echo " PASS=$PASS  FAIL=$FAIL"
[ -n "$FAILED" ] && echo " 失败用例:$FAILED"
{
    echo "============================================================"
    echo " PASS=$PASS  FAIL=$FAIL"
    if [ -n "$FAILED" ]; then echo " 失败用例:$FAILED"; fi
    echo "============================================================"
} >> "$SUMMARY"
echo " 日志目录: tests/results/"
echo " 单用例  : bash tests/show_case.sh <用例名>"
echo "============================================================"

[ "$FAIL" -eq 0 ]
