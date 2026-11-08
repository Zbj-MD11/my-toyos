#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"

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

        // 初始化物理内存管理
        pmem_init();

        // 创建内核 Sv39 三级页表
        kvm_init();

        // Hart 0 启用内核页表
        kvm_inithart();

        /*
         * 初始化中断系统的全局资源
         *
         * 内部完成：
         * 1. plic_init()
         * 2. timer_create()
         */
        trap_kernel_init();

        /*
         * 通知其他 Hart：
         * 全局初始化已经完成
         */
        __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
    }
    else
    {
        /*
         * 其他 Hart 等待 Hart 0 完成全局初始化
         */
        while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
            ;

        // 每个 Hart 独立启用内核页表
        kvm_inithart();
    }

    /*
     * 第二阶段：每 Hart 独立初始化
     */

    // 开启 S-mode 软件中断和外部中断的局部使能
    w_sie(r_sie() | SIE_SSIE | SIE_SEIE);

    /*
     * 初始化当前 Hart 的中断环境
     *
     * 内部完成：
     * 1. plic_inithart()
     * 2. 设置 stvec = kernel_vector
     * 3. 开启 S-mode 全局中断
     */
    trap_kernel_inithart();

    printf("cpu %d is booting!\n", cpuid);

    /*
     * 第三阶段：输出启动信息
     */
    if (cpuid == 0)
    {
        printf("\n");
        printf("================================\n");
        printf("       Welcome to ToyOS!        \n");
        printf("================================\n");
        printf("Hello, World!\n");
        printf("Physical memory initialized.\n");
        printf("Sv39 virtual memory enabled.\n");
        printf("Interrupt system initialized.\n");
        printf("\n");
    }

    /*
     * 当前尚未实现进程调度，
     * 初始化完成后保持内核运行
     */
    uint64 last_ticks = 0;

    while (1)
    {
        
        if (cpuid == 0)
        {
            uint64 ticks = timer_get_ticks();

            if (ticks != last_ticks)
            {
                printf("ticks = %d\n", (int)ticks);
                last_ticks = ticks;
            }
        }
        
    }

    return 0;
}