#include "mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return();

// 第一个用户进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成trapframe和trampoline的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    //分配一个内核物理内存作为用户的根页表
   pgtbl_t user_pgtbl_root = pmem_alloc(true);

   //映射trampoline
    vm_mappages(
        user_pgtbl_root,
        (uint64)TRAMPOLINE,
        (uint64)trampoline,
        PGSIZE,
        PTE_R | PTE_X
    );

    //映射Trapframe
    vm_mappages(
        user_pgtbl_root,
        (uint64)TRAPFRAME,
        (uint64)trapframe,
        PGSIZE,
        PTE_R | PTE_W
    );

    return user_pgtbl_root;

}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问

	注意: 用用户空间的地址映射需要标记 PTE_U
*/
void proc_make_first()
{
    // 1. 初始化 PID
    proczero.pid = 0;

    // 2. 分配 Trapframe 物理页
    proczero.tf = (trapframe_t *)pmem_alloc(true);

    // 3. 创建用户根页表，并映射 Trampoline 和 Trapframe
    proczero.pgtbl = proc_pgtbl_init((uint64)proczero.tf);

    //这里直接设置为1页用户占
    proczero.ustack_npage=1;
    void *user_stack_page=pmem_alloc(false);
    vm_mappages(
        proczero.pgtbl,
        (uint64)USTACK,
        (uint64)user_stack_page,
        PGSIZE*proczero.ustack_npage,
        PTE_R | PTE_W | PTE_U

    );

    //用户代码段
    void *user_code_page=pmem_alloc(false);

    // 将用户程序机器码复制到新分配的物理页中
    memmove(user_code_page,initcode,initcode_len);

    // 在用户页表中映射代码页
    vm_mappages(
        proczero.pgtbl,
        (uint64)USER_BASE,
        (uint64)user_code_page,
        PGSIZE,
        PTE_R | PTE_X | PTE_U
    );
    //初始化用户进程的内存边界和执行现场
    proczero.heap_top = USER_BASE + PGSIZE;

    // 用户程序第一次执行的入口地址
    proczero.tf->user_to_kern_epc=USER_BASE;

    // 用户栈向低地址增长，因此初始 sp 设置为用户栈顶
    proczero.tf->sp = USTACK+PGSIZE;

    //初始化进程的内核栈及内核上下文
    proczero.kstack = KSTACK(0);

    // swtch() 恢复进程上下文后，使用该内核栈
    proczero.ctx.sp = KSTACK(0) + PGSIZE;

    // swtch() 最后的 ret 将跳转到 trap_user_return()
    proczero.ctx.ra = (uint64)trap_user_return;

    //将 proczero 关联到当前 Hart
    cpu_t *cpu = mycpu();
    cpu->proc=&proczero;
    //保存当前内核上下文，切换到 proczero 的内核上下文
    swtch(&cpu->ctx, &proczero.ctx);
}