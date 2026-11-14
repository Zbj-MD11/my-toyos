#!/usr/bin/env bash
# ============================================================
# tests/run_tests.sh —— lab-1 功能测试
#
# 用法:
#   bash tests/run_tests.sh            # 跑全部用例
#   bash tests/run_tests.sh 05_printf_formats   # 只跑一个
#
# 每个用例:
#   1. 把工程复制到 tests/.work/<用例>
#   2. 按用例的 prepare.sh 修改代码（不加修改的就是原样）
#   3. make clean && make build
#   4. QEMU 限时运行，输出存 tests/results/<用例>.log
#   5. 用 check.sh 判定 PASS / FAIL
# ============================================================

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

WORK="$ROOT/tests/.work"
RESULT="$ROOT/tests/results"
rm -rf "$WORK" "$RESULT"
mkdir -p "$WORK" "$RESULT"

if [ "$#" -gt 0 ]; then
    CASES=("$@")
else
    CASES=()
    for d in "$ROOT"/tests/cases/*/; do
        CASES+=("$(basename "$d")")
    done
fi

echo "============================================================"
echo " lab-1 功能测试    $(date '+%F %T')"
echo "============================================================"
printf "%-26s %-6s %s\n" "用例" "结果" "说明"

PASS=0
FAIL=0

for id in "${CASES[@]}"; do
    dir="$ROOT/tests/cases/$id"
    work="$WORK/$id"
    mkdir -p "$work"

    cp "$ROOT/Makefile" "$ROOT/common.mk" "$ROOT/kernel.ld" "$work"/
    cp -r "$ROOT/src" "$work"/

    # 用例自带的源文件覆盖（例如替换 main.c）
    if [ -d "$dir/files" ]; then
        cp -r "$dir/files/." "$work"/
    fi

    TITLE="$id"
    ARGS="-bios none -m 128M -smp 2"
    TIMEOUT=6
    # shellcheck disable=SC1090
    . "$dir/case.conf"

    if [ -f "$dir/prepare.sh" ]; then
        if ! bash "$dir/prepare.sh" "$work" > "$work/prepare.log" 2>&1; then
            printf "%-26s %-6s %s\n" "$id" "FAIL" "prepare 失败"
            FAIL=$((FAIL + 1))
            continue
        fi
    fi

    if ! ( cd "$work" && make clean > build.log 2>&1 && make build >> build.log 2>&1 ); then
        printf "%-26s %-6s %s\n" "$id" "FAIL" "编译失败"
        cp "$work/build.log" "$RESULT/$id.build.log" 2>/dev/null
        FAIL=$((FAIL + 1))
        continue
    fi

    # shellcheck disable=SC2086
    timeout --foreground "$TIMEOUT" qemu-system-riscv64 -machine virt \
        -kernel "$work/target/kernel/kernel-qemu.elf" -nographic $ARGS \
        < /dev/null > "$work/run.log" 2>&1
    cp "$work/run.log" "$RESULT/$id.log"

    if bash "$dir/check.sh" "$work/run.log" > "$work/check.log" 2>&1; then
        printf "%-26s %-6s %s\n" "$id" "PASS" "$TITLE"
        PASS=$((PASS + 1))
    else
        printf "%-26s %-6s %s\n" "$id" "FAIL" "$TITLE"
        FAIL=$((FAIL + 1))
    fi
done

echo "============================================================"
echo " PASS=$PASS  FAIL=$FAIL"
echo " 详细日志: tests/results/"
echo "============================================================"
[ "$FAIL" -eq 0 ]
