#include "mod.h"
#include "../../user/syscall_num.h"

// in trampoline.S
extern char trampoline[];   // 用户态与内核态共享的跳板代码
extern char user_vector[];  // 用户态 trap 的汇编入口
extern char user_return[];  // 恢复用户现场并执行 sret

// in trap.S
extern char kernel_vector[]; // 内核态 trap 的汇编入口

// in trap_kernel.c
extern char *interrupt_info[16];
extern char *exception_info[16];


/*
 * 用户态 trap 处理函数
 *
 * user_vector 保存用户寄存器、切换内核页表和内核栈后，
 * 会跳转到这里。
 */
void trap_user_handler()
{
    // 1. 现在已经处于 S-mode，并使用内核页表
    // 后续如果内核再次发生 trap，应进入 kernel_vector
    w_stvec((uint64)kernel_vector);

    // 2. 获取当前进程
    proc_t *p = myproc();

    // 3. 保存用户程序发生 trap 时的 PC
    p->tf->user_to_kern_epc = r_sepc();

    // 4. 读取 trap 原因和附加信息
    uint64 scause = r_scause();
    uint64 stval = r_stval();

    // 用户 trap 进入 S-mode 后，SPP 应为 0
    assert(
        (r_sstatus() & SSTATUS_SPP) == 0,
        "trap_user_handler: not from U-mode"
    );

    // scause 最高位标识中断，其余位表示原因编号
    uint64 trap_id = scause & ~(1ULL << 63);

    if (scause & (1ULL << 63))
    {
        /*
         * 5. 处理中断
         * 中断返回时，不需要将用户 PC 加 4
         */
        switch (trap_id)
        {
        case 1:
            // S-mode 软件中断：处理时钟事件
            timer_interrupt_handler();
            break;

        case 9:
            // S-mode 外部中断：例如 UART
            external_interrupt_handler();
            break;

        default:
            if (trap_id < 16)
                printf("unexpected user interrupt: %s\n",
                       interrupt_info[trap_id]);
            else
                printf("unexpected user interrupt: %d\n",
                       (int)trap_id);

            printf("scause = %x, sepc = %x, stval = %x\n",
                   scause, p->tf->user_to_kern_epc, stval);

            panic("trap_user_handler");
        }
    }
    else
    {
        /*
         * 6. 处理同步异常
         */
        switch (trap_id)
        {
        case 8:
            // 用户态 ecall 指令长度为 4 字节
            // 返回时跳过本次 ecall
            p->tf->user_to_kern_epc += 4;

            // a7 保存系统调用编号
            switch (p->tf->a7)
            {
            case SYS_helloworld:
                printf("proczero: hello world!\n");

                // a0 是系统调用的返回值寄存器
                p->tf->a0 = 0;
                break;

            default:
                printf("unknown syscall: %d\n",
                       (int)p->tf->a7);

                panic("trap_user_handler: unknown syscall");
            }
            break;

        default:
            if (trap_id < 16)
                printf("unexpected user exception: %s\n",
                       exception_info[trap_id]);
            else
                printf("unexpected user exception: %d\n",
                       (int)trap_id);

            printf("scause = %x, sepc = %x, stval = %x\n",
                   scause, p->tf->user_to_kern_epc, stval);

            panic("trap_user_handler");
        }
    }

    // 7. trap 处理完毕，准备返回 U-mode
    trap_user_return();
}


/*
 * 从内核态返回用户态
 *
 * 第一次启动 proczero，以及每次用户 trap 处理结束后，
 * 都会进入这个函数。
 */
void trap_user_return()
{
    // 1. 关闭 S-mode 中断
    intr_off();

    // 2. 获取当前进程
    proc_t *p = myproc();

    // 3. 设置下一次用户态 trap 的入口
    // 通过 TRAMPOLINE 的高虚拟地址访问 user_vector
    uint64 user_vector_va =
        TRAMPOLINE +
        ((uint64)user_vector - (uint64)trampoline);

    w_stvec(user_vector_va);

    // 4. 提前保存下一次进入内核所需的运行环境
    p->tf->user_to_kern_satp = r_satp();

    // 内核栈向低地址增长，保存栈顶地址
    p->tf->user_to_kern_sp = p->kstack + PGSIZE;

    // 用户 trap 进入内核后执行的 C 函数
    p->tf->user_to_kern_trapvector =
        (uint64)trap_user_handler;

    // 保存当前 Hart ID，供 user_vector 恢复 tp
    p->tf->user_to_kern_hartid = mycpuid();

    // 5. 设置 sstatus，为 sret 做准备
    uint64 sstatus = r_sstatus();

    // SPP = 0：返回 U-mode
    sstatus &= ~SSTATUS_SPP;

    // SPIE = 1：准备恢复中断使能状态
    sstatus |= SSTATUS_SPIE;

    w_sstatus(sstatus);

    // 6. 设置用户程序恢复执行的位置
    w_sepc(p->tf->user_to_kern_epc);

    // 7. 计算 user_return 的高虚拟地址
    uint64 user_return_va =
        TRAMPOLINE +
        ((uint64)user_return - (uint64)trampoline);

    // 将高虚拟地址转换为函数指针
    void (*user_return_func)(uint64, uint64) =
        (void (*)(uint64, uint64))user_return_va;

    // 8. 进入 trampoline.S
    // a0 = TRAPFRAME 的用户页表虚拟地址
    // a1 = 用户页表编码后的 satp 值
    user_return_func(TRAPFRAME, MAKE_SATP(p->pgtbl));
}