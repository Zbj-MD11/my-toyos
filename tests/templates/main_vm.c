/* 由 tests/run_tests.sh 生成：stage=vm，物理内存 + 内核页表 + 打开 Sv39 */
#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"

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

        __atomic_store_n(&boot_done, 1, __ATOMIC_RELEASE);
        @CASE@(0);
    }
    else
    {
        while (__atomic_load_n(&boot_done, __ATOMIC_ACQUIRE) == 0)
            ;

        kvm_inithart();
        @CASE@(cpuid);
    }

    while (1)
        ;
    return 0;
}
