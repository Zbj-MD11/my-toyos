/*
 * lab-3 测试用例集（中断 + 异常）
 *
 * 本文件不会出现在 src/ 里：tests/run_tests.sh 会把工程复制到
 * tests/.work/<用例>，再把本文件拷成 src/kernel/lab3_tests.c，
 * 并按用例重写 main.c（见 tests/templates/main_test.c）。
 *
 * 判定约定（与 lab-2 一致）：
 *   - 正常用例：最后打印 "RESULT: PASS <名字>"
 *   - 期望 panic 的用例（异常类）：串口里必须出现被测 handler 打印的
 *     "unexpected exception: ..." 和 "panic! trap_kernel_handler"；
 *     如果异常没被捕获，会打印 "LAB3: ..." 自曝
 *
 * 覆盖设计（六类，对应 tests/README.md 的矩阵）：
 *   路径覆盖：trap_kernel_handler 的中断/异常两条大分支、external 的
 *             claim==0 / UART_IRQ、uart_intr 的 7 条小分支、timer 的 hart0/hart≠0
 *   等价类：  字符输入 {可打印, CR, LF, CRLF, BS(0x08), DEL(0x7f)} x
 *             {行内, 行首}，中断源 {时钟, 串口}
 *   边界值：  ticks 下界 0；16550 FIFO 容量 16 字符；line_len == 0 退格；空行
 *   并发：    -smp 2 下时钟不翻倍、hart1 的 SSIP 被清、双核串口
 *   异常注入：ecall(S) / 非法指令 / load page fault / store page fault
 *   压力：    25 个 tick 内 tick 与输入交错（3 秒 soak）
 */
#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"

extern void kernel_vector(void);

/* 见 lab3_tests_asm.S：在 s0-s11 放哨兵，经历若干次 trap 后比较 */
extern uint64 lab3_ctx_probe(void);

/* 串口输入窗口：25 个 tick ≈ 2.5 秒（INTERVAL=1e6，timebase=10MHz） */
#define UART_WINDOW_TICKS 25

static void pass(const char *name)
{
    printf("RESULT: PASS %s\n", name);
}

/* 轮询等待 n 个 tick；时钟坏了就一直等，由外层 timeout 结束 */
static void wait_ticks(uint64 n)
{
    uint64 t0 = timer_get_ticks();

    while (timer_get_ticks() < t0 + n)
        ;
}

/* 供 lab3_tests_asm.S 调用：期间会经历 3 次 trap */
void lab3_ctx_target(void)
{
    wait_ticks(3);
}

/* ============================================================
 * 路径覆盖：trap 初始化后的 CSR 状态
 * ============================================================ */
void lab3_case_trap_init(int hartid)
{
    uint64 stvec = r_stvec();
    uint64 sie = r_sie();
    int ok = 1;

    (void)hartid;

    printf("stvec = %x\n", stvec);
    printf("sie   = %x\n", sie);
    printf("sstatus.sie = %d\n", intr_get());

    if (stvec != (uint64)kernel_vector)
    {
        printf("LAB3: stvec is not kernel_vector\n");
        ok = 0;
    }
    if ((stvec & 3) != 0)
    {
        printf("LAB3: stvec is not 4-byte aligned\n");
        ok = 0;
    }
    if ((sie & SIE_SEIE) == 0)
    {
        printf("LAB3: sie.SEIE not set\n");
        ok = 0;
    }
    if ((sie & SIE_SSIE) == 0)
    {
        printf("LAB3: sie.SSIE not set\n");
        ok = 0;
    }
    if (intr_get() == 0)
    {
        printf("LAB3: sstatus.SIE not set\n");
        ok = 0;
    }
    assert(ok, "trap_init");

    printf("stvec ok\n");
    printf("sie ok\n");
    printf("intr on\n");
    pass("trap_init");
}

/* ============================================================
 * 边界值：启动完成时 ticks 的下界（0）与"不能一开机就很多"
 * 注意：从 timer_init 到 main 要建 128MB 的页表，慢的机器上已经过了 1~2 个 tick，
 *       所以这里判 0 <= ticks <= 3，而不是死磕 == 0；能抓住的是"计数器没清零"
 *       和"时钟周期配错、一开机就刷一大堆 tick"。
 * ============================================================ */
void lab3_case_tick_zero(int hartid)
{
    uint64 t;

    if (hartid)
        return;

    t = timer_get_ticks();
    printf("ticks0 = %d\n", (int)t);
    assert(t <= 3, "tick_zero: ticks too large right after boot, timer fires too fast");
    pass("tick_zero");
}

/* ============================================================
 * 路径覆盖：MTIMECMP -> timer_vector -> SSIP -> timer_interrupt_handler
 *           -> timer_update，ticks 必须单调递增
 * ============================================================ */
void lab3_case_tick_monotonic(int hartid)
{
    uint64 last, cur, target, sc, st, i;

    if (hartid)
        return;

    last = timer_get_ticks();
    target = last + 5;
    printf("waiting for 5 ticks, first = %d\n", (int)last);

    while ((cur = timer_get_ticks()) < target)
    {
        assert(cur >= last, "tick_monotonic: ticks went backwards");
        if (cur != last)
        {
            printf("tick %d\n", (int)cur);
            last = cur;
        }
    }

    printf("reached 5 ticks\n");
    printf("monotonic ok\n");

    /* 关键的一步：这次 tick 必须以 S-mode 软件中断投递
       （scause 最高位 = 1，原因编号 = 1）。
       M-mode 的 timer_vector 里 csrw sip,2 之后才会这样；如果 mideleg 把
       机器时钟中断（编号 7）或 STIP 也委托下来，这里会看到别的编号，
       而 trap_kernel_handler 会走 default 分支 panic。 */
    sc = 0;
    for (i = 0; i < 10; i++)
    {
        sc = r_scause();
        if ((sc >> 63) != 0 && (sc & 0xf) == 1)
            break;
        wait_ticks(1);
    }
    st = r_sstatus();
    printf("scause = %x\n", sc);
    printf("sstatus = %x\n", st);
    assert((sc >> 63) != 0, "tick_monotonic: scause high bit is not set");
    assert((sc & 0xf) == 1, "tick_monotonic: timer is not delivered as S-software interrupt");
    assert((st & SSTATUS_SPIE) != 0, "tick_monotonic: sstatus.SPIE is not set after a trap");

    printf("timer delivered as SSI\n");
    pass("tick_monotonic");
}

/* ============================================================
 * 路径覆盖：trap.S 的上下文保存/恢复（12 个 callee-saved 寄存器）
 * ============================================================ */
void lab3_case_ctx_regs(int hartid)
{
    uint64 r;

    if (hartid)
        return;

    printf("ctx probe: s0-s11 sentinels across 3 ticks\n");
    r = lab3_ctx_probe();
    printf("ctx probe returns %d (0 = all preserved)\n", (int)r);
    assert(r == 0, "trap.S clobbered a callee-saved register");
    printf("ctx: s0-s11 preserved\n");
    pass("ctx_regs");
}

/* ============================================================
 * 串口：打开输入窗口，期间不打印，保证回显是连续的
 * 返回实际经过的 tick 数
 * ============================================================ */
static uint64 uart_window(void)
{
    uint64 t0, t;

    printf("--- begin input ---\n");
    t0 = timer_get_ticks();
    t = t0;
    while (t < t0 + UART_WINDOW_TICKS)
        t = timer_get_ticks();
    printf("--- end input ---\n");
    printf("uart window closed at tick %d\n", (int)(t - t0));
    return t - t0;
}

void lab3_case_uart_window(int hartid)
{
    if (hartid)
        return;

    (void)uart_window();
    pass("uart_window");
}

/* ============================================================
 * 异常：S-mode 的 ecall / 非法指令 / 非法地址访问
 * 这里的"正确结果"就是被 trap_kernel_handler 打印后 panic
 * ============================================================ */
void lab3_case_exc_ecall(int hartid)
{
    if (hartid)
        return;

    printf("raising ecall from S-mode (expect panic)\n");
    asm volatile("ecall" ::: "memory");
    assert(false, "LAB3: ecall returned, medeleg or handler is broken");
}

void lab3_case_exc_illegal(int hartid)
{
    if (hartid)
        return;

    printf("executing illegal instruction (expect panic)\n");
    asm volatile(".word 0x00000000" ::: "memory");
    assert(false, "LAB3: illegal instruction was not trapped");
}

void lab3_case_exc_load(int hartid)
{
    /* 地址放在 volatile 变量里：避免编译器在 -O 下把"空指针解引用"当已知错误，
       触发 -Wnull-dereference（配合 -Werror 会让用例编译失败） */
    static volatile uint64 bad_addr;
    static volatile uint64 sink;
    volatile uint64 *p;

    if (hartid)
        return;

    bad_addr = 0;
    p = (volatile uint64 *)bad_addr;
    printf("load from va 0 (expect load page fault)\n");
    sink = *p;
    printf("load value = %x (unexpected)\n", (uint64)sink);
    assert(false, "LAB3: unmapped load was not trapped");
}

void lab3_case_exc_store(int hartid)
{
    static volatile uint64 bad_addr;
    volatile uint64 *p;

    if (hartid)
        return;

    bad_addr = 0;
    p = (volatile uint64 *)bad_addr;
    printf("store to va 0 (expect store page fault)\n");
    *p = 1;
    assert(false, "LAB3: unmapped store was not trapped");
}

/* ============================================================
 * 并发：双核下 ticks 不应该翻倍（timer_interrupt_handler 只让 hart0 更新）
 * 每个 tick 打印一行 TICK，由 tests/checks/17_con_smp2_tick.sh 数行数
 * ============================================================ */
void lab3_case_smp_tick_rate(int hartid)
{
    uint64 last, cur;

    if (hartid)
    {
        wait_ticks(3);
        return;
    }

    last = timer_get_ticks();
    printf("TICK %d\n", (int)last);

    while (1)
    {
        cur = timer_get_ticks();
        if (cur != last)
        {
            printf("TICK %d\n", (int)cur);
            last = cur;
        }
    }
}

/* ============================================================
 * 并发：双核下串口中断只被一个 hart claim；hart1 必须清掉自己的 SSIP，
 *       否则它会永远停在 kernel_vector 里（表现为 hart1 不再前进）
 * ============================================================ */
void lab3_case_smp_uart(int hartid)
{
    uint64 i;
    uint64 sc = 0;
    uint64 sip_bit;

    if (hartid)
    {
        /* hart1 必须自己经历过 S-mode 软件中断：scause 的高位是"中断"，
           低 4 位是 1（SSI）。这直接证明 per-hart 的 MTIMECMP/mscratch 生效了 */
        for (i = 0; i < 10; i++)
        {
            sc = r_scause();
            if ((sc >> 63) != 0 && (sc & 0xf) == 1)
                break;
            wait_ticks(1);
        }
        printf("hart1 scause = %x\n", sc);
        assert((sc >> 63) != 0, "hart1: no interrupt was ever taken");
        assert((sc & 0xf) == 1, "hart1: last trap is not an S-software interrupt");

        /* SSIP 必须被 hart1 的 handler 清掉，否则它会永远重入 kernel_vector */
        for (i = 0; i < 5; i++)
        {
            sip_bit = r_sip() & 2;
            if (sip_bit == 0)
                break;
            wait_ticks(1);
        }
        sip_bit = r_sip() & 2;
        printf("hart1 sip cleared = %d\n", (int)(sip_bit == 0));
        assert(sip_bit == 0, "hart1 SSIP is stuck");
        return;
    }

    (void)uart_window();
    pass("smp_uart");
}

/* ============================================================
 * 压力：25 个 tick 内 tick 与串口输入交错，不丢字符、不丢 tick、不死锁
 * ============================================================ */
void lab3_case_soak(int hartid)
{
    uint64 elapsed;

    if (hartid)
        return;

    elapsed = uart_window();
    printf("soak ticks = %d\n", (int)elapsed);
    assert(elapsed >= UART_WINDOW_TICKS, "soak: ticks did not advance");
    pass("soak");
}
