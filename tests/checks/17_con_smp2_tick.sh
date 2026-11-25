#!/usr/bin/env bash
# 用例 17 判定：-smp 2 跑 4 秒，数 TICK 行数。
#
# 依据（src/kernel/trap/timer.c）：timer_interrupt_handler() 里只有
#   if (mycpuid() == 0) timer_update();
# 所以双核时 ticks 的增速必须还是 1 倍（约 10 个/秒，4 秒约 35~40 行）。
# 如果测出 ~70 行，说明要么 cpuid 守卫失效、要么 SSIP 没清导致重复计数——是 bug。
# 如果测出 0 行，说明时钟根本没走（这由用例 04 单独判定）。
#
# 阈值放宽到 10..55：既容忍 VMware 里 TCG 的抖动，又能卡住"翻倍"。
set -u
log="$1"
n=$(grep -c '^TICK ' "$log" 2>/dev/null || true)
n=${n:-0}

if [ "$n" -ge 10 ] && [ "$n" -le 55 ]; then
    echo "PASS ticks=$n expected 10..55 (1x, not 2x)"
    exit 0
fi
echo "FAIL ticks=$n expected 10..55 (1x, not 2x)"
exit 1
