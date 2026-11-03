


#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"

// test_case_2() 定义在 pmem.c 中
void test_case_2(void);

// 两个 hart 共享的初始化完成标志
static int started = 0;

int main(void)
{
    int cpuid = r_tp();

    if (cpuid == 0) {
        // 只有 Hart 0 负责初始化
        print_init();
        pmem_init();

        printf("cpu %d is booting!\n", cpuid);

        // 通知其他 hart：初始化已经完成
        __atomic_store_n(&started, 1, __ATOMIC_RELEASE);

        // 只有 Hart 0 执行测试 2
        printf("Starting physical memory test_case_2...\n");
        test_case_2();

        printf("Physical memory test_case_2 finished!\n");

    } else {
        // 等待 Hart 0 完成初始化
        while (!__atomic_load_n(&started, __ATOMIC_ACQUIRE))
            ;

        printf("cpu %d is booting!\n", cpuid);

        // Hart 1 不参与本次测试
    }

    while (1)
        ;

    return 0;
}
