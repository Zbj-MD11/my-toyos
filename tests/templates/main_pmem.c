/* 由 tests/run_tests.sh 生成：stage=pmem，只初始化物理内存（不开 MMU） */
#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"

void @CASE@(int hartid);

int main(void)
{
    int cpuid = (int)r_tp();

    if (cpuid == 0)
    {
        print_init();
        pmem_init();
        @CASE@(0);
    }

    while (1)
        ;
    return 0;
}
