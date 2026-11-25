# lab3_tests_asm.S —— 由 tests/run_tests.sh 注入到 src/kernel/lab3_tests_asm.S
#
# 目的：验证 trap.S 的上下文保存/恢复（32 个通用寄存器里的 12 个 callee-saved）
# 做法：在 s0-s11 里放 12 个不同的哨兵 -> 调用 C 函数 lab3_ctx_target（期间经历
#       3 次时钟中断，也就是 3 次 kernel_vector 的存/取）-> 逐个比较
# 返回：a0 = 0 表示全部保持不变；1..12 表示第几个 s 寄存器被改掉了
#
# 说明：这里必须用汇编。C 代码没法安全地"把 s0-s11 全部占用并跨函数调用比较"，
#       编译器自己就要用这些寄存器存局部变量。

.globl lab3_ctx_probe
lab3_ctx_probe:
        addi sp, sp, -112
        sd   ra, 0(sp)
        sd   s0, 8(sp)
        sd   s1, 16(sp)
        sd   s2, 24(sp)
        sd   s3, 32(sp)
        sd   s4, 40(sp)
        sd   s5, 48(sp)
        sd   s6, 56(sp)
        sd   s7, 64(sp)
        sd   s8, 72(sp)
        sd   s9, 80(sp)
        sd   s10, 88(sp)
        sd   s11, 96(sp)

        li   s0, 0x5a5a
        li   s1, 0x5a5b
        li   s2, 0x5a5c
        li   s3, 0x5a5d
        li   s4, 0x5a5e
        li   s5, 0x5a5f
        li   s6, 0x5a60
        li   s7, 0x5a61
        li   s8, 0x5a62
        li   s9, 0x5a63
        li   s10, 0x5a64
        li   s11, 0x5a65

        call lab3_ctx_target

        li   t0, 0x5a5a
        bne  s0, t0, .L3_bad1
        li   t0, 0x5a5b
        bne  s1, t0, .L3_bad2
        li   t0, 0x5a5c
        bne  s2, t0, .L3_bad3
        li   t0, 0x5a5d
        bne  s3, t0, .L3_bad4
        li   t0, 0x5a5e
        bne  s4, t0, .L3_bad5
        li   t0, 0x5a5f
        bne  s5, t0, .L3_bad6
        li   t0, 0x5a60
        bne  s6, t0, .L3_bad7
        li   t0, 0x5a61
        bne  s7, t0, .L3_bad8
        li   t0, 0x5a62
        bne  s8, t0, .L3_bad9
        li   t0, 0x5a63
        bne  s9, t0, .L3_bad10
        li   t0, 0x5a64
        bne  s10, t0, .L3_bad11
        li   t0, 0x5a65
        bne  s11, t0, .L3_bad12

        li   a0, 0
        j    .L3_out

.L3_bad1:
        li   a0, 1
        j    .L3_out
.L3_bad2:
        li   a0, 2
        j    .L3_out
.L3_bad3:
        li   a0, 3
        j    .L3_out
.L3_bad4:
        li   a0, 4
        j    .L3_out
.L3_bad5:
        li   a0, 5
        j    .L3_out
.L3_bad6:
        li   a0, 6
        j    .L3_out
.L3_bad7:
        li   a0, 7
        j    .L3_out
.L3_bad8:
        li   a0, 8
        j    .L3_out
.L3_bad9:
        li   a0, 9
        j    .L3_out
.L3_bad10:
        li   a0, 10
        j    .L3_out
.L3_bad11:
        li   a0, 11
        j    .L3_out
.L3_bad12:
        li   a0, 12

.L3_out:
        ld   ra, 0(sp)
        ld   s0, 8(sp)
        ld   s1, 16(sp)
        ld   s2, 24(sp)
        ld   s3, 32(sp)
        ld   s4, 40(sp)
        ld   s5, 48(sp)
        ld   s6, 56(sp)
        ld   s7, 64(sp)
        ld   s8, 72(sp)
        ld   s9, 80(sp)
        ld   s10, 88(sp)
        ld   s11, 96(sp)
        addi sp, sp, 112
        ret
