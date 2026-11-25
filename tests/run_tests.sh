#!/usr/bin/env bash
# ============================================================
# tests/run_tests.sh —— lab-3 中断 / 异常测试
#
# 用法:
#   bash tests/run_tests.sh                     # 跑全部用例
#   bash tests/run_tests.sh 04_path_tick_monotonic
#   bash tests/run_tests.sh 06_eq_uart_echo 08_eq_uart_backspace
#
# 每个用例:
#   1. 把 Makefile / common.mk / kernel.ld / src 复制到 tests/.work/<用例>
#   2. 注入 tests/suite/lab3_tests.c + lab3_tests_asm.S，按用例重写 main.c
#      （stage=orig 的用例不注入，跑组员原样的 main.c）
#   3. make clean && make build
#   4. QEMU 限时运行；有 tests/stdin/<用例>.sh 的话用它喂串口输入，
#      否则 < /dev/null（必须的：-nographic 下连着终端会让 QEMU 挂起）
#   5. 有 tests/checks/<用例>.sh 的话再跑一遍判定（数行数 / 还原屏幕），
#      把结论追加成 "[check] PASS ..." 一行写进日志
#   6. 按 cases.tsv 的 must / mustnot 判定 PASS / FAIL
#
# 注意: 会 panic 的用例靠 timeout 结束，QEMU 退出码非 0 是预期行为。
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

SUMMARY="$RESULT/summary.txt"
STATUS="$RESULT/status.tsv"
: > "$STATUS"

{
    echo "============================================================"
    echo " lab-3 中断与异常测试    $(date '+%F %T')"
    echo " 编译器: $(riscv64-linux-gnu-gcc --version 2>/dev/null | head -n1)"
    echo " QEMU  : $(qemu-system-riscv64 --version 2>/dev/null | head -n1)"
    echo "============================================================"
    printf "%-24s %-6s %s\n" "用例" "结果" "说明"
} | tee "$SUMMARY"

PASS=0
FAIL=0
FAILED=""

report() { # id 结果 说明 类别
    printf "%-24s %-6s %s\n" "$1" "$2" "$3"
    printf "%-24s %-6s %s\n" "$1" "$2" "$3" >> "$SUMMARY"
    printf "%s|%s|%s\n" "$1" "$4" "$2" >> "$STATUS"
}

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
        cp "$ROOT/tests/suite/lab3_tests.c" "$work/src/kernel/lab3_tests.c"
        # 注意：探针必须放子目录！Makefile 收集 .S 时顶层目录排在子目录前面，
        # 如果放成 src/kernel/lab3_tests_asm.S，它会排在 boot/entry.S 前面，
        # 把 _entry 挤离 0x80000000，一上电就跳进探针里死等（表现为串口零输出）。
        cp "$ROOT/tests/suite/lab3_tests_asm.S" "$work/src/kernel/trap/lab3_tests_asm.S"
        sed -e "s/@CASE@/$fn/g" -e "s/@ID@/$id/g" \
            "$ROOT/tests/templates/main_test.c" > "$work/src/kernel/main.c"
    fi

    if ! ( cd "$work" && make clean > build.log 2>&1 && make build >> build.log 2>&1 ) < /dev/null; then
        report "$id" "FAIL" "编译失败: $title" "$stage"
        cp "$work/build.log" "$RESULT/$id.build.log" 2>/dev/null
        FAIL=$((FAIL + 1))
        FAILED="$FAILED $id"
        continue
    fi

    # 入口自检：0x80000000 必须是 _entry。不是的话一上电就跳进别的函数，
    # 表现就是"串口零输出"（2026-11-25 踩过：注入的 .S 排到了 entry.S 前面）
    if command -v riscv64-linux-gnu-nm >/dev/null 2>&1; then
        first_sym=$(riscv64-linux-gnu-nm -n "$work/target/kernel/kernel-qemu.elf" 2>/dev/null |
                    awk '$2 ~ /^[tT]$/ {print $1" "$3; exit}')
        if [ "${first_sym:-}" != "0000000080000000 _entry" ]; then
            echo "  [warn] $id: 0x80000000 处是 ${first_sym:-?}，不是 _entry" >&2
        fi
    fi

    qemu=(qemu-system-riscv64 -machine virt -bios none -m 128M
          -smp "$smp" -nographic -kernel "$work/target/kernel/kernel-qemu.elf")

    if [ -f "$ROOT/tests/stdin/$id.sh" ]; then
        # 串口输入用例：用进程替换喂数据，不能 < /dev/null（否则收不到输入），
        # 也不能让 stdin 连着终端（-nographic 会挂起）
        timeout --foreground "$tmo" "${qemu[@]}" \
            < <(bash "$ROOT/tests/stdin/$id.sh") > "$work/run.log" 2>&1
    else
        timeout --foreground "$tmo" "${qemu[@]}" \
            < /dev/null > "$work/run.log" 2>&1
    fi

    if [ -f "$ROOT/tests/checks/$id.sh" ]; then
        # 约定：check 脚本最后一行是 "PASS 细节" 或 "FAIL 细节"，原样写回日志，
        # 这样 cases.tsv 里用 must="[check] PASS" 判定，截图里也能看到数字证据
        bash "$ROOT/tests/checks/$id.sh" "$work/run.log" > "$work/check.out" 2>&1 || true
        echo "[check] $(tail -n 1 "$work/check.out")" >> "$work/run.log"
    fi
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
        report "$id" "PASS" "$title" "$stage"
        PASS=$((PASS + 1))
    else
        report "$id" "FAIL" "$title" "$stage"
        FAIL=$((FAIL + 1))
        FAILED="$FAILED $id"
        if [ ! -s "$work/run.log" ]; then
            echo "        提示: 串口零输出。先跑 bash tools/lab3_test.sh --smoke 看是不是 PMP/启动问题"
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
    echo "------------------------------------------------------------"
    echo " PASS=$PASS  FAIL=$FAIL"
    if [ -n "$FAILED" ]; then echo " 失败用例:$FAILED"; fi
    echo "------------------------------------------------------------"
    echo " 按类别（tests/cases.tsv 的 stage 列）:"
    awk -F'|' '{tot[$2]++; if ($3 == "PASS") p[$2]++}
                END { for (k in tot) printf "   %-5s PASS %d / %d\n", k, p[k] + 0, tot[k] }' \
        "$STATUS" | sort
} >> "$SUMMARY"

echo "------------------------------------------------------------"
echo " 按类别:"
awk -F'|' '{tot[$2]++; if ($3 == "PASS") p[$2]++}
            END { for (k in tot) printf "   %-5s PASS %d / %d\n", k, p[k] + 0, tot[k] }' \
    "$STATUS" | sort
echo " 日志目录: tests/results/"
echo " 单用例  : bash tests/show_case.sh <用例名>"
echo "============================================================"

[ "$FAIL" -eq 0 ]
