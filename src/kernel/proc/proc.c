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

/* ------------本地变量----------- */

// 进程结构体数组 + 第一个用户进程的指针
static proc_t proc_list[N_PROC];
static proc_t *proczero;

// 全局pid + 保护它的锁
static int global_pid;
static spinlock_t pid_lk;

/* 获取一个pid */
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&pid_lk);
    assert(global_pid > 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&pid_lk);
    return tmp;
}

/* 释放进程锁 + trap_user_return */
static void proc_return()
{

}

/* 进程模块初始化 */
void proc_init()
{    
    //初始化global_pid的锁
    spinlock_init(&pid_lk,"global_pid_lk");

    //初始化global_pid
    global_pid=1;
    
        // 初始化进程数组
    for(int i=0;i<N_PROC;i++){
        // 所有进程结构体初始都处于未使用状态
        proc_list[i].state=UNUSED;
        // 初始化每个进程独立的自旋锁
        spinlock_init(&(proc_list[i].lk),"proc_lk");
    }
     // 第一个用户进程尚未创建
    proczero=NULL;
}

/* 
    申请一个UNUSED进程结构体(返回时带锁)
    并执行通用的初始化逻辑
*/
proc_t *proc_alloc()
{
    for(int i=0;i<N_PROC;i++){

        proc_t *p=&proc_list[i];

        // 获取当前进程的锁
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED)
        {
            // 分配进程 PID
            p->pid = alloc_pid();

            // 清空进程名称
            memset(p->name, 0, sizeof(p->name));

            // 初始化进程共享字段
            p->parent = NULL;
            p->exit_code = 0;
            p->sleep_space = NULL;

            // 初始化用户内存管理信息
            p->heap_top = 0;
            p->ustack_npage = 0;
            p->mmap = NULL;

            // 分配 Trapframe 物理页
            p->tf = (trapframe_t *)pmem_alloc(true);

            // 创建用户页表，并映射 Trapframe 和 Trampoline
            p->pgtbl = proc_pgtbl_init((uint64)p->tf);

            // 设置当前进程的内核栈地址
            // 内核栈物理页已经在 kvm_init 中分配并映射
            p->kstack = KSTACK(i);

            // 清空旧进程遗留的内核上下文
            memset(&p->ctx, 0, sizeof(p->ctx));

            // 初始化内核栈指针
            p->ctx.sp = p->kstack + PGSIZE;

            // 新进程第一次获得 CPU 时，先进入 proc_return
            p->ctx.ra = (uint64)proc_return;

            // 保持 UNUSED 状态，等待调用者完成剩余初始化
            // 调用者最终将状态改为 RUNNABLE

            // 带锁返回，调用者负责释放进程锁
            return p;
        }
        // 当前 PCB 已经被使用，释放锁
        spinlock_release(&p->lk);
    }
    // 所有 PCB 都已经被使用
    return NULL;
}

/*
    回收一个进程结构体并释放它包含的资源
    tips: 调用者需要持有进程锁
*/
void proc_free(proc_t *p)
{
    // 检查传入的进程指针是否合法
    assert(p != NULL, "proc_free: NULL process");

    // 调用者已经持有 p->lk，不需要再次加锁

    // 销毁用户页表
    // 同时释放代码段、堆、栈、mmap 对应的用户物理页
    // 以及 Trapframe 和各级页表占用的内核物理页
    if (p->pgtbl != NULL)
    {
        uvm_destroy_pgtbl(p->pgtbl);
        p->pgtbl = NULL;
    }

    // Trapframe 已经随用户页表一起释放
    p->tf = NULL;

    // 遍历进程的 mmap 链表
    // 将所有 mmap 区域描述节点归还全局仓库
    mmap_region_t *cur = p->mmap;

    while (cur != NULL)
    {
        // 提前保存下一个节点
        mmap_region_t *next = cur->next;

        // 归还当前 mmap 描述节点
        mmap_region_free(cur);

        // 继续遍历
        cur = next;
    }

    // 清空 mmap 链表
    p->mmap = NULL;

    // 重置用户内存管理信息
    p->heap_top = 0;
    p->ustack_npage = 0;

    // 重置进程标识信息
    p->pid = 0;
    memset(p->name, 0, sizeof(p->name));

    // 清空进程关系及退出信息
    p->parent = NULL;
    p->exit_code = 0;
    p->sleep_space = NULL;

    // 清空内核上下文
    memset(&p->ctx, 0, sizeof(p->ctx));

    // 内核栈的物理页在 kvm_init 中统一分配并映射
    // 这里只清空记录，不释放内核栈物理页
    p->kstack = 0;

    // 所有资源回收完成
    // 将 PCB 标记为空闲，允许后续重新分配
    p->state = UNUSED;

    // 不释放 p->lk
    // 由调用者负责释放进程锁
}

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
    创建第一个用户态进程 proczero
    初始化用户代码、用户栈及初始执行现场
*/
void proc_make_first()
{
    // 从进程资源仓库申请一个 PCB
    // proc_alloc 返回时已经持有进程锁
    proczero = proc_alloc();

    assert(
        proczero != NULL,
        "proc_make_first: no available process"
    );

    // 检查用户程序是否能够放进一页
    assert(
        initcode_len <= PGSIZE,
        "proc_make_first: initcode too large"
    );

    // 设置进程名称
    memmove(proczero->name, "proczero", sizeof("proczero"));

    // 初始化用户栈，初始大小为一页
    proczero->ustack_npage = 1;

    // 申请用户栈物理页
    void *user_stack_page = pmem_alloc(false);

    // 将用户栈映射到用户页表
    vm_mappages(
        proczero->pgtbl,
        USTACK,
        (uint64)user_stack_page,
        PGSIZE,
        PTE_R | PTE_W | PTE_U
    );

    // 为用户程序申请代码页
    void *user_code_page = pmem_alloc(false);

    // 将 initcode 的机器码复制到代码页
    memmove(user_code_page, initcode, initcode_len);

    // 将用户代码页映射到用户页表
    vm_mappages(
        proczero->pgtbl,
        USER_BASE,
        (uint64)user_code_page,
        PGSIZE,
        PTE_R | PTE_X | PTE_U
    );

    // 初始化用户堆顶
    proczero->heap_top = USER_BASE + PGSIZE;

    // 设置用户程序第一次执行的 PC
    proczero->tf->user_to_kern_epc = USER_BASE;

    // 用户栈向低地址增长
    // 因此初始 sp 指向用户栈顶部
    proczero->tf->sp = USTACK + PGSIZE;

    // 初始化完成，允许调度器选择该进程
    proczero->state = RUNNABLE;

    // 释放 proc_alloc 返回时持有的进程锁
    spinlock_release(&proczero->lk);
}

/*
    父进程产生子进程
    UNUSED -> RUNNABLE
    父进程返回子进程 PID
    分配失败返回 -1
*/
int proc_fork()
{
    // 获取当前父进程
    proc_t *parent = myproc();

    // 从进程资源仓库申请一个子进程
    // proc_alloc 返回时已经持有子进程锁
    proc_t *child = proc_alloc();

    // PCB 仓库已满
    if (child == NULL)
    {
        return -1;
    }

    // 复制父进程名称
    memmove(
        child->name,
        parent->name,
        sizeof(parent->name)
    );

    // 建立父子关系
    child->parent = parent;

    // 继承父进程的用户内存布局信息
    child->heap_top = parent->heap_top;
    child->ustack_npage = parent->ustack_npage;

    // 复制父进程的 Trapframe 内容
    // 父子进程使用不同的 Trapframe 物理页
    memmove(
        child->tf,
        parent->tf,
        sizeof(trapframe_t)
    );

    // 子进程中的 fork 返回值必须为 0
    // RISC-V 使用 a0 寄存器保存函数返回值
    child->tf->a0 = 0;

    // 复制父进程的用户地址空间
    // 为子进程分配独立的用户物理页
    // 复制代码段、堆、mmap 和用户栈的内容
    uvm_copy_pgtbl(
        parent->pgtbl,
        child->pgtbl,
        parent->heap_top,
        parent->ustack_npage,
        parent->mmap
    );

    // 复制父进程的 mmap 区域描述链表
    // 子进程需要自己的描述节点，不能共享父进程的链表
    mmap_region_t *src = parent->mmap;

    // link 始终指向新链表中需要填写的 next 指针位置
    mmap_region_t **link = &child->mmap;

    while (src != NULL)
    {
        // 从全局 mmap 仓库申请新的描述节点
        mmap_region_t *node = mmap_region_alloc();

        // 复制 mmap 区域的虚拟地址与页数
        node->begin = src->begin;
        node->npages = src->npages;
        node->next = NULL;

        // 将新节点连接到子进程的 mmap 链表
        *link = node;

        // 准备连接下一个节点
        link = &node->next;

        // 继续遍历父进程的 mmap 链表
        src = src->next;
    }

    // 保存子进程 PID
    int child_pid = child->pid;

    // 子进程初始化完成，可以参与调度
    child->state = RUNNABLE;

    // 释放 proc_alloc 返回时持有的进程锁
    spinlock_release(&child->lk);

    // 父进程中的 fork 返回子进程 PID
    return child_pid;
}
/*
    进程主动放弃CPU控制权
    RUNNING->RUNNABLE
*/
void proc_yield()
{
    cpu_t *cpu=mycpu();

    proc_t *p=cpu->proc;

    spinlock_acquire(&(p->lk));

    // 将进程重新设置为可运行状态
    p->state=RUNNABLE;

    // 保存当前进程上下文，切换回调度器
    proc_sched();

    // 进程再次获得 CPU 后，释放进程锁
    spinlock_release(&(p->lk));


}

/*
    当父进程退出时, 让它的所有子进程认proczero为父
    因为proczero永不退出, 可以回收子进程的资源
*/
static void proc_reparent(proc_t *parent)
{
    for(int i=0;i<N_PROC;i++){

        proc_t *p=&proc_list[i];

         // 父进程不可能是自己的子进程
        if (p == parent)
        {
            continue;
        }

        spinlock_acquire(&(p->lk));

        if(p->parent==parent){
            // 将它交给 proczero 管理
            p->parent=proczero;
        }

        spinlock_release(&(p->lk));
    }
}

/*
    唤醒等待呼叫的进程
    由proc_exit调用
    tips: 调用者需要持有p的进程锁
*/
static void proc_try_wakeup(proc_t *p)
{
    // 检查父进程是否正在等待子进程退出
    if (p->state == SLEEPING && p->sleep_space == p)
    {
        // 将父进程重新设置为可运行状态
        p->state = RUNNABLE;
    }
}

/*
    进程退出
    RUNNING -> ZOMBIE
*/
void proc_exit(int exit_code)
{
    proc_t* proc=myproc();

    // proczero 是所有进程的根进程，不允许退出
    assert(proc != proczero, "proc_exit: proczero cannot exit");

    // 将当前进程的所有子进程交给 proczero
    // 此时还没有持有当前进程锁
    proc_reparent(proc);

    // 获取当前进程的父进程
    proc_t *parent = proc->parent;
    // 获取父进程锁
    spinlock_acquire(&parent->lk);
    // 保存进程退出状态
    proc->exit_code=exit_code;
    // 将当前进程设置为僵尸状态
    proc->state=ZOMBIE;
    // 尝试唤醒正在等待的父进程
    proc_try_wakeup(parent);

    // 父进程已经收到唤醒通知
    spinlock_release(&parent->lk);


     // 当前进程带着自己的锁切换回调度器
    // ZOMBIE 进程不会再被调度
    proc_sched();

    // 正常情况下不应该执行到这里
    panic("proc_exit: zombie process resumed");
}

/*
    父进程等待一个子进程进入 ZOMBIE 状态
    如果等到：回收子进程，返回子进程 PID，并传出退出状态
    如果没有孩子：返回 -1
    如果孩子尚未退出：进入睡眠等待
*/
int proc_wait(uint64 user_addr)
{
    // 获取当前父进程
    proc_t *parent = myproc();

    // 获取父进程锁
    // 保护检查子进程状态与进入睡眠的过程
    spinlock_acquire(&parent->lk);

    while (1)
    {
        // 记录当前进程是否还有子进程
        int have_child = 0;

        // 遍历整个 PCB 资源仓库
        for (int i = 0; i < N_PROC; i++)
        {
            proc_t *p = &proc_list[i];

            // 避免重复获取父进程自己的锁
            if (p == parent)
            {
                continue;
            }

            // 获取当前 PCB 的锁
            spinlock_acquire(&p->lk);

            // 检查当前进程是否为自己的子进程
            if (p->parent == parent)
            {
                have_child = 1;

                // 找到已经退出的子进程
                if (p->state == ZOMBIE)
                {
                    // 回收前保存 PID 和退出状态
                    int pid = p->pid;
                    int exit_code = p->exit_code;

                    // 如果用户提供了地址
                    // 将子进程退出状态复制到父进程用户空间
                    if (user_addr != 0)
                    {
                        uvm_copyout(
                            parent->pgtbl,
                            user_addr,
                            (uint64)&exit_code,
                            sizeof(exit_code)
                        );
                    }

                    // 回收子进程的资源和 PCB
                    // 调用者已经持有子进程锁
                    proc_free(p);

                    // 释放子进程锁
                    spinlock_release(&p->lk);

                    // 释放父进程锁
                    spinlock_release(&parent->lk);

                    // 返回被回收子进程的 PID
                    return pid;
                }
            }

            // 当前 PCB 不是可回收的子进程
            spinlock_release(&p->lk);
        }

        // 遍历结束后，没有找到任何子进程
        if (!have_child)
        {
            spinlock_release(&parent->lk);
            return -1;
        }

        // 仍然有子进程，但尚未进入 ZOMBIE 状态
        // 让当前父进程进入睡眠，等待子进程退出
        proc_sleep(parent, &parent->lk);

        // 父进程被唤醒后重新检查所有子进程
    }
}

/*
    进程等待sleep_space对应的资源, 进入睡眠状态
    RUNNING -> SLEEPING
*/
void proc_sleep(void *sleep_space, spinlock_t *lock)
{
    proc_t *p = myproc();

    // 如果传入的锁不是当前进程自己的锁
    // 则先获取进程锁，再释放外部锁
    if (lock != &p->lk)
    {
        spinlock_acquire(&p->lk);
        spinlock_release(lock);
    }

    // 设置等待资源
    p->sleep_space = sleep_space;

    // 当前进程进入睡眠状态
    p->state = SLEEPING;

    // 交出 CPU
    // 此时必须仍然持有进程锁
    proc_sched();

    // 这里是进程被唤醒、再次获得 CPU 后的执行位置

    // 清空等待资源标识
    p->sleep_space = NULL;

    // 如果之前释放过外部锁
    // 则释放进程锁，并重新获取外部锁
    if (lock != &p->lk)
    {
        spinlock_release(&p->lk);
        spinlock_acquire(lock);
    }



}

/*
    唤醒所有等待sleep_space的进程
    SLEEPING -> RUNNABLE
*/
void proc_wakeup(void *sleep_space)
{
    assert(sleep_space!=NULL,"proc_wakeup:Invalid sleep_space(NULLPTR)");
    for(int i=0;i<N_PROC;i++){

        proc_t *p=&proc_list[i];

        spinlock_acquire(&(p->lk));

         // 检查当前进程是否正在等待指定资源
        if(p->state==SLEEPING){
            if(p->sleep_space==sleep_space){
                // 将进程重新设置为可运行状态
                p->state=RUNNABLE;
            }
        }

        spinlock_release(&(p->lk));
    }
}

/* 
    用户进程切换到调度器
    tips: 调用者保证持有当前进程的锁
*/
void proc_sched()
{
    // 获取当前 CPU
    cpu_t *cpu=mycpu();

    // 获取当前 CPU 正在运行的进程
    proc_t *proc=cpu->proc;

    // 确保当前 CPU 关联了有效进程
    assert(proc != NULL, "proc_sched: no current process");

    // 调用者必须已经持有当前进程的锁
    assert(spinlock_holding(&proc->lk),"proc_sched:");

    // 进程必须已经离开 RUNNING 状态
    assert(proc->state!=RUNNING,"proc_sched:the proc is still running");

    // 保存当前进程的内核上下文
    // 恢复当前 CPU 的调度器上下文
    swtch(&proc->ctx, &cpu->ctx);
}

/* 
    调度器
    RUNNABLE->RUNNING
*/
void proc_scheduler()
{

    cpu_t *cpu = mycpu();

    // 当前 CPU 进入调度器，不属于任何用户进程
    cpu->proc = NULL;

    //如果直接用这里的for循环查找，则会有天然的优先级关系
    // 调度器永不退出
    while(1){
        // 循环扫描整个 PCB 资源仓库
        for(int i=0;i<N_PROC;i++){

            proc_t *p = &proc_list[i];

            spinlock_acquire(&(p->lk));

            if(p->state==RUNNABLE){

                p->state=RUNNING;

                // 将进程关联到当前 CPU
                cpu->proc = p;

                // 保存调度器上下文
                // 恢复目标进程的内核上下文
                swtch(&cpu->ctx,&p->ctx);

                // 进程交还 CPU 后，从这里恢复执行
                // 当前 CPU 重新进入调度器
                cpu->proc = NULL;
            }

            spinlock_release(&(p->lk));
        }
    }
}


/*-----------------lab-5测试代码-----------------------*/

// 为测试页表分配并映射一张用户物理页
static void test5_map_page(
    pgtbl_t pgtbl,
    uint64 va,
    int perm,
    unsigned char value
)
{
    // 申请用户物理页
    void *page = pmem_alloc(false);

    // 使用固定数据填充整个页面
    memset(page, value, PGSIZE);

    // 建立虚拟地址到物理页的映射
    vm_mappages(
        pgtbl,
        va,
        (uint64)page,
        PGSIZE,
        perm
    );
}

// 检查某个虚拟页面是否被正确复制
static void test5_check_page(
    pgtbl_t old,
    pgtbl_t new,
    uint64 va
)
{
    // 获取两个页表中对应的 PTE
    pte_t *old_pte = vm_getpte(old, va, false);
    pte_t *new_pte = vm_getpte(new, va, false);

    assert(
        old_pte != NULL && new_pte != NULL,
        "test5: PTE not found"
    );

    assert(
        (*old_pte & PTE_V) && (*new_pte & PTE_V),
        "test5: invalid PTE"
    );

    // 获取两个 PTE 对应的物理地址
    uint64 old_pa = PTE_TO_PA(*old_pte);
    uint64 new_pa = PTE_TO_PA(*new_pte);

    // 复制后的物理页面必须独立
    assert(
        old_pa != new_pa,
        "test5: physical pages are shared"
    );

    // 页表权限必须一致
    assert(
        PTE_FLAGS(*old_pte) == PTE_FLAGS(*new_pte),
        "test5: PTE flags mismatch"
    );

    // 逐字节检查整个页面
    unsigned char *old_data = (unsigned char *)old_pa;
    unsigned char *new_data = (unsigned char *)new_pa;

    for (uint32 i = 0; i < PGSIZE; i++)
    {
        assert(
            old_data[i] == new_data[i],
            "test5: page content mismatch"
        );
    }
}

// 测试用户页表复制与销毁
void test_uvm_copy_destroy()
{
    printf("\n===== Test 5: UVM Copy & Destroy =====\n");

    // 为两张测试页表分别申请 Trapframe
    void *old_tf = pmem_alloc(true);
    void *new_tf = pmem_alloc(true);

    // 创建两张独立的用户页表
    // proc_pgtbl_init 会完成 Trapframe 和 Trampoline 的映射
    pgtbl_t old = proc_pgtbl_init((uint64)old_tf);
    pgtbl_t new = proc_pgtbl_init((uint64)new_tf);

    // 创建用户代码页
    test5_map_page(
        old,
        USER_BASE,
        PTE_R | PTE_X | PTE_U,
        0x11
    );

    // 创建用户堆
    // 故意让 heap_top 不按页对齐
    uint64 heap_begin = USER_BASE + PGSIZE;
    uint64 heap_top = heap_begin + 100;

    test5_map_page(
        old,
        heap_begin,
        PTE_R | PTE_W | PTE_U,
        0x22
    );

    // 创建两页用户栈
    uint64 ustack_npage = 2;

    test5_map_page(
        old,
        USTACK - PGSIZE,
        PTE_R | PTE_W | PTE_U,
        0x33
    );

    test5_map_page(
        old,
        USTACK,
        PTE_R | PTE_W | PTE_U,
        0x44
    );

    // 从全局仓库申请两个 mmap 描述节点
    mmap_region_t *region1 = mmap_region_alloc();
    mmap_region_t *region2 = mmap_region_alloc();

    // 创建第一个 mmap 区域
    region1->begin = MMAP_BEGIN + 2 * PGSIZE;
    region1->npages = 1;
    region1->next = region2;

    // 创建第二个 mmap 区域
    // 与第一个区域之间故意留出空隙
    region2->begin = MMAP_BEGIN + 6 * PGSIZE;
    region2->npages = 1;
    region2->next = NULL;

    // 为两个 mmap 区域建立实际物理页映射
    test5_map_page(
        old,
        region1->begin,
        PTE_R | PTE_W | PTE_U,
        0x55
    );

    test5_map_page(
        old,
        region2->begin,
        PTE_R | PTE_W | PTE_U,
        0x66
    );

    printf("test5: source page table created\n");

    // 复制用户页表
    uvm_copy_pgtbl(
        old,
        new,
        heap_top,
        ustack_npage,
        region1
    );

    printf("test5: page table copied\n");

    // 检查用户代码页
    test5_check_page(old, new, USER_BASE);

    // 检查用户堆
    test5_check_page(old, new, heap_begin);

    // 检查用户栈的两张页面
    test5_check_page(old, new, USTACK - PGSIZE);
    test5_check_page(old, new, USTACK);

    // 检查两个不连续的 mmap 区域
    test5_check_page(old, new, region1->begin);
    test5_check_page(old, new, region2->begin);

    printf("test5: page content and flags verified\n");

    // 检查 mmap 空隙没有被错误映射
    uint64 gap_va = MMAP_BEGIN + 3 * PGSIZE;
    pte_t *gap_pte = vm_getpte(new, gap_va, false);

    assert(
        gap_pte == NULL || ((*gap_pte & PTE_V) == 0),
        "test5: mmap gap was unexpectedly mapped"
    );

    printf("test5: mmap gap verified\n");

    // 修改新页表中的堆数据
    pte_t *old_heap_pte = vm_getpte(old, heap_begin, false);
    pte_t *new_heap_pte = vm_getpte(new, heap_begin, false);

    unsigned char *old_heap_data =
        (unsigned char *)PTE_TO_PA(*old_heap_pte);

    unsigned char *new_heap_data =
        (unsigned char *)PTE_TO_PA(*new_heap_pte);

    new_heap_data[0] = 0xEE;

    // 新页表的修改不能影响旧页表
    assert(
        old_heap_data[0] == 0x22,
        "test5: modifying copied page changed original"
    );

    printf("test5: independent physical pages verified\n");

    // 销毁新页表
    uvm_destroy_pgtbl(new);

    printf("test5: copied page table destroyed\n");

    // 销毁旧页表
    uvm_destroy_pgtbl(old);

    printf("test5: source page table destroyed\n");

    // 归还 mmap 描述节点
    // 页表销毁不会自动归还 mmap_region_t
    mmap_region_free(region1);
    mmap_region_free(region2);

    printf("===== Test 5 Passed! =====\n\n");
}