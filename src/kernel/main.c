#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
#include "proc/mod.h"

/*
 * 多核启动同步标志
 *
 * 0: Hart 0 尚未完成全局初始化
 * 1: 全局初始化完成，其他 Hart 可以继续启动
 */
static int started = 0;

int main(void)
{
    // 获取当前 Hart ID
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

        // 初始化 mmap_region 节点仓库
        mmap_init();

        // 创建内核 Sv39 三级页表
        // 包含 Trampoline 和 proczero 内核栈的映射
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
        // 测试用户页表复制与销毁
        //test_uvm_copy_destroy();

        // 初始化进程管理模块
        proc_init();

        /*
         * 通知其他 Hart：
         * 全局初始化已经完成
         *
         * 必须在启动 proczero 之前执行，
         * 否则其他 Hart 可能一直等待。
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
     * 第三阶段：输出系统启动信息
     * 只由 Hart 0 执行
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
    第四阶段：创建第一个用户进程

    只由 Hart 0 执行
    此时仅创建进程并将其设置为 RUNNABLE
    真正的执行由 proc_scheduler 负责
    */
    if (cpuid == 0)
    {
        printf("Starting proczero...\n");

        proc_make_first();
    }

    /*
    第五阶段：启动进程调度器

    每个 CPU 都需要执行
    调度器负责寻找 RUNNABLE 进程
    并将其切换到 RUNNING 状态
    */
    proc_scheduler();
    
    return 0;

}