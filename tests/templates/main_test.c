/*
 * 由 tests/run_tests.sh 生成：按用例重写 src/kernel/main.c
 *
 * 这里刻意复刻组员 main.c 的初始化顺序（少一步测的就不是他的代码了）：
 *   hart0: print_init -> pmem_init -> kvm_init -> kvm_inithart -> trap_kernel_init
 *   其它 hart 等 hart0 完成全局初始化后 kvm_inithart
 *   每个 hart: w_sie(SSIE|SEIE) -> trap_kernel_inithart()（内含 plic_inithart / stvec / intr_on）
 *
 * @CASE@ 由 run_tests.sh 替换成用例函数名，@ID@ 替换成用例 id。
 */
#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"

void @CASE@(int hartid);

static int boot_done = 0;

int main(void)
{
    int cpuid = (int)r_tp();

    if (cpuid == 0)
    {
        print_init();
        pmem_init();
        kvm_init();
        kvm_inithart();

        trap_kernel_init();

        __atomic_store_n(&boot_done, 1, __ATOMIC_RELEASE);
    }
    else
    {
        while (__atomic_load_n(&boot_done, __ATOMIC_ACQUIRE) == 0)
            ;

        kvm_inithart();
    }

    // S-mode 中断使能（组员 main.c 里就是这两步）
    w_sie(r_sie() | SIE_SSIE | SIE_SEIE);
    trap_kernel_inithart();

    if (cpuid == 0)
        printf("\n===== case @ID@ =====\n");

    @CASE@(cpuid);

    while (1)
        ;
    return 0;
}
