


#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"

static int started = 0;

int main()
{
    int cpuid = r_tp();

    if (cpuid == 0) {

        // 初始化串口和 printf
        print_init();

        // 初始化物理内存管理
        pmem_init();

        // 创建内核页表
        kvm_init();

        // 当前 hart 启用 Sv39
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);

        test_mapping_and_unmapping();

        // 通知其他 hart 初始化完成
        __atomic_store_n(&started, 1, __ATOMIC_RELEASE);

    } else {

        // 等待 Hart 0 完成初始化
        while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
            ;

        // 每个 hart 都需要启用自己的内核页表
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);
    }

    while (1)
        ;

    return 0;
}