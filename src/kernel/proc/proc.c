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
    //初始化 PID
    proczero.pid = 0;

    // 当前进程还没有分配任何 mmap 区域
    proczero.mmap = NULL;

    //分配 Trapframe 物理页
    proczero.tf = (trapframe_t *)pmem_alloc(true);

    //创建用户根页表，并映射 Trampoline 和 Trapframe
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


/*-----------------测试代码-----------------------*/

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