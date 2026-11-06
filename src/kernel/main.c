
#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"

/*
 * 多核启动同步标志
 *
 * 0: Hart 0 尚未完成全局初始化
 * 1: 全局初始化完成，其他 Hart 可以继续启动
 */
static int started = 0;

int main(void)
{
    int cpuid = r_tp();

    if (cpuid == 0)
    {
        /*
         * 第一阶段：全局初始化
         * 只由 Hart 0 执行一次
         */

        // 初始化 UART 和 printf 自旋锁
        print_init();

        // 初始化内核和用户物理页分配器
        pmem_init();

        // 创建内核 Sv39 三级页表
        kvm_init();

        /*
         * 第二阶段：启用虚拟内存
         */

        // Hart 0 加载内核页表，启用 Sv39
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);

        /*
         * 第三阶段：内核启动成功
         */

        printf("\n");
        printf("================================\n");
        printf("       Welcome to ToyOS!        \n");
        printf("================================\n");
        printf("Hello, World!\n");
        printf("Physical memory initialized.\n");
        printf("Sv39 virtual memory enabled.\n");
        printf("\n");

        /*
         * 通知其他 Hart：
         * 全局物理内存与内核页表初始化完毕
         */
        __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
    }
    else
    {
        /*
         * 其他 Hart 等待 Hart 0 完成全局初始化
         */
        while (__atomic_load_n(&started,
                               __ATOMIC_ACQUIRE) == 0)
            ;

        /*
         * 每个 Hart 都有独立的 satp 寄存器，
         * 因此都必须独立启用内核页表。
         */
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);
    }

    /*
     * 当前 Lab 2 尚未实现进程调度。
     * 初始化完成后保持内核运行。
     */
    while (1)
        ;

    return 0;
}
