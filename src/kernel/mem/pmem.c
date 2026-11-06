#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{

    //初始化内核空间
    kern_region.begin=(uint64)ALLOC_BEGIN;
    kern_region.end=kern_region.begin+KERN_PAGES*PGSIZE;
    kern_region.list_head.next = NULL;
    spinlock_init(&kern_region.lk,"OS_mem_lock");
    kern_region.allocable=0;

    //开始插入链表
    for(uint64 i=kern_region.begin;i<kern_region.end;i+=PGSIZE){
        page_node_t *page = (page_node_t *)i;
        page->next=kern_region.list_head.next;
        kern_region.list_head.next=page;
        kern_region.allocable++;
    }
    //初始化用户空间
    user_region.begin=kern_region.end;
    user_region.end=(uint64)ALLOC_END;
    user_region.list_head.next = NULL;
    spinlock_init(&user_region.lk,"USR_mem_lock");
    user_region.allocable=0;

    //开始插入链表
    for(uint64 i=user_region.begin;i<user_region.end;i+=PGSIZE){
        page_node_t *page = (page_node_t *)i;
        page->next=user_region.list_head.next;
        user_region.list_head.next=page;
        user_region.allocable++;
    }

}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void* pmem_alloc(bool in_kernel)
{
    page_node_t *page;

    if(in_kernel){

        spinlock_acquire(&kern_region.lk);
        if(!kern_region.list_head.next){
            /*

            这里本来可以不用if，直接在assert里面传入逻辑表达式
            但是万一以后有别的错误处理呢？
            我就先这么设计一个分支，让他能释放内存锁
            逻辑也是等价的，看看后面用的用不上           
            */
            spinlock_release(&kern_region.lk);
            assert(false,"pmem_alloc:No available OS_Page");

        }
        
        page = kern_region.list_head.next;
        kern_region.list_head.next=kern_region.list_head.next->next;
        kern_region.allocable--;

        spinlock_release(&kern_region.lk);

    }else{

        spinlock_acquire(&user_region.lk);
        if(!user_region.list_head.next){

            spinlock_release(&user_region.lk);
            assert(false,"pmem_alloc:No available USER_Page");

        }
        
        page = user_region.list_head.next;
        user_region.list_head.next=user_region.list_head.next->next;
        user_region.allocable--;

        spinlock_release(&user_region.lk);
    }
    //清0后再返回
    memset(page, 0, PGSIZE);

    return page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    //先检查在不在可以释放的区间
    if(page<(uint64)ALLOC_BEGIN||page>=(uint64)ALLOC_END){
        assert(false,"pmem_free:Invalid page address");
    }
    //再检查是否对齐
    if((page-(uint64)ALLOC_BEGIN)%PGSIZE!=0){
        assert(false,"pmem_free:Page address doesn't match size");
    }
    
    
    if(in_kernel){

        if(page<kern_region.begin||page>=kern_region.end){
            assert(false,"pmem_free:Invalid OS_page address");
        }
        spinlock_acquire(&kern_region.lk);
        page_node_t *node = (page_node_t *)page;
        node->next=kern_region.list_head.next;
        kern_region.list_head.next=node;
        kern_region.allocable++;
        spinlock_release(&kern_region.lk);

    }else{
        if(page<user_region.begin||page>=user_region.end){
            assert(false,"pmem_free:Invalid USER_page address");
        }
        spinlock_acquire(&user_region.lk);
        page_node_t *node = (page_node_t *)page;
        node->next=user_region.list_head.next;
        user_region.list_head.next=node;
        user_region.allocable++;
        spinlock_release(&user_region.lk);
    }
}





/*--------------------------------- 测试代码 ----------------------------------*/

// 测试目标：耗尽内核/用户区域内存 
void test_case_1()
{
    int i=0;

    while (1)
    {
        
        pmem_alloc(true);
        i++;
        printf("%d\n",i);
        
    }
}

#define TEST_CNT 10

// 测试目标: 常规申请和释放操作
void test_case_2()
{
    alloc_region_t *user_ar = &user_region;
    uint64 user_pages[TEST_CNT];

    for (int i = 0; i < TEST_CNT; i++)
        user_pages[i] = 0;

    printf("=== test_case_2: Phase 1 - Allocate User Pages ===\n");
    for (int i = 0; i < TEST_CNT; i++)
    {
        user_pages[i] = (uint64)pmem_alloc(false);

        printf("Allocated user page[%d] @ %p\n", i, (void *)user_pages[i]);

        if (!(user_pages[i] >= user_ar->begin && user_pages[i] < user_ar->end))
        {
            printf("Assertion failed: Page address out of bounds! Page: %p, Region: [%p, %p)\n",
                   (void *)user_pages[i], (void *)user_ar->begin, (void *)user_ar->end);
            panic("Page address out of user region bounds");
        }

        memset((void *)user_pages[i], 0xAA, PGSIZE);
    }

    printf("=== test_case_2: Phase 2 - Pre-free Check ===\n");
    spinlock_acquire(&user_ar->lk);
    int expected_before = (user_ar->end - user_ar->begin) / PGSIZE - TEST_CNT;
    int actual = user_ar->allocable;
    printf("Expected allocable: %d, Actual: %d\n", expected_before, actual);
    assert(user_ar->allocable == expected_before, "Allocable count incorrect before free");
    spinlock_release(&user_ar->lk);

    printf("=== test_case_2: Phase 3 - Free Pages ===\n");
    for (int i = 0; i < TEST_CNT; i++)
    {
        pmem_free(user_pages[i], false);
        printf("Free user page[%d] @ %p\n", i, (void *)user_pages[i]);
    }

    printf("=== test_case_2: Phase 4 - Post-free Check ===\n");
    spinlock_acquire(&user_ar->lk);
    int expected_after = (user_ar->end - user_ar->begin) / PGSIZE;
    actual = user_ar->allocable;
    printf("Expected allocable: %d, Actual: %d\n", expected_after, actual);
    assert(user_ar->allocable == expected_after, "Allocable count not restored after free");
    if (user_ar->list_head.next != NULL)
        printf("Free list head @ %p\n", user_ar->list_head.next);
    else
        panic("Free list is empty after freeing pages");
    spinlock_release(&user_ar->lk);

    printf("=== test_case_2: Phase 5 - Reallocate & Verify Zero ===\n");
    for (int i = 0; i < TEST_CNT; i++)
    {
        void *page = pmem_alloc(false);
        printf("Reallocated page[%d] @ %p\n", i, page);

        bool non_zero = false;
        for (int j = 0; j < PGSIZE / sizeof(int); j++)
        {
            if (((int *)page)[j] != 0)
            {
                non_zero = true;
                printf("Non-zero value detected at offset %d: 0x%x\n", j, ((int *)page)[j]);
                break;
            }
        }
        assert(!non_zero, "Memory not zeroed after free");
        printf("Zero verification passed\n");
    }

    printf("test_case_2 passed!\n");
}


#define PMEM_ALIGN_CNT 16

void test_pmem_alignment(void)
{
    printf("\n=== test_pmem_alignment ===\n");

    for (int mode = 0; mode < 2; mode++)
    {
        bool in_kernel = (mode == 1);
        uint64 pages[PMEM_ALIGN_CNT];

        for (int i = 0; i < PMEM_ALIGN_CNT; i++)
        {
            pages[i] = (uint64)pmem_alloc(in_kernel);

            assert(pages[i] % PGSIZE == 0,
                   "test_pmem_alignment: page is not aligned");
        }

        // 归还本次测试申请的全部页面
        for (int i = 0; i < PMEM_ALIGN_CNT; i++)
        {
            pmem_free(pages[i], in_kernel);
        }

        printf("mode %d alignment passed!\n", mode);
    }

    printf("test_pmem_alignment passed!\n");
}

#define PMEM_UNIQUE_CNT 16

void test_pmem_unique(void)
{
    printf("\n=== test_pmem_unique ===\n");

    uint64 pages[2][PMEM_UNIQUE_CNT];

    // mode 0: 用户页
    // mode 1: 内核页
    for (int mode = 0; mode < 2; mode++)
    {
        bool in_kernel = (mode == 1);

        for (int i = 0; i < PMEM_UNIQUE_CNT; i++)
        {
            pages[mode][i] = (uint64)pmem_alloc(in_kernel);

            // 检查本次分配是否与之前尚未释放的页面重复
            for (int m = 0; m <= mode; m++)
            {
                int count = (m == mode) ? i : PMEM_UNIQUE_CNT;

                for (int j = 0; j < count; j++)
                {
                    assert(pages[mode][i] != pages[m][j],
                           "test_pmem_unique: duplicate physical page");
                }
            }
        }
    }

    // 验证完成后归还全部物理页
    for (int mode = 0; mode < 2; mode++)
    {
        for (int i = 0; i < PMEM_UNIQUE_CNT; i++)
        {
            pmem_free(pages[mode][i], mode == 1);
        }
    }

    printf("test_pmem_unique passed!\n");
}

void test_pmem_invalid_free(int kind)
{
    printf("\n=== test_pmem_invalid_free ===\n");

    uint64 pa;

    if (kind == 1)
    {
        // 情况 1：未对齐地址
        pa = (uint64)pmem_alloc(false);

        printf("Testing unaligned free, expect panic...\n");

        pmem_free(pa + 1, false);
    }
    else if (kind == 2)
    {
        // 情况 2：恰好等于用户区域结束地址
        pa = user_region.end;

        printf("Testing out-of-range free, expect panic...\n");

        pmem_free(pa, false);
    }
    else if (kind == 3)
    {
        // 情况 3：把用户页作为内核页释放
        pa = (uint64)pmem_alloc(false);

        printf("Testing wrong-region free, expect panic...\n");

        pmem_free(pa, true);
    }
    else
    {
        panic("test_pmem_invalid_free: invalid test kind");
    }

    // 如果程序运行到这里，说明非法释放没有被拒绝
    panic("BUG: invalid free was accepted");
}

void test_pmem_double_free(void)
{
    printf("\n=== test_pmem_double_free ===\n");

    uint64 pa = (uint64)pmem_alloc(false);

    printf("First free...\n");
    pmem_free(pa, false);

    printf("Second free, expect rejection...\n");
    pmem_free(pa, false);

    // 如果能够继续执行，就说明重复释放未被拒绝
    panic("BUG: double free was accepted");
}

#define PMEM_CONC_CNT 64

static uint64 conc_pages[2][PMEM_CONC_CNT];

static int conc_ready[2] = {0, 0};
static int conc_alloc_done[2] = {0, 0};
static int conc_verified = 0;
static int conc_free_done[2] = {0, 0};

void test_pmem_concurrent(int hartid)
{
    assert(hartid == 0 || hartid == 1,
           "test_pmem_concurrent: invalid hartid");

    int initial_allocable = 0;

    // 只有 Hart 0 记录初始空闲页数
    if (hartid == 0)
    {
        spinlock_acquire(&user_region.lk);
        initial_allocable = user_region.allocable;
        spinlock_release(&user_region.lk);

        assert(initial_allocable >= 2 * PMEM_CONC_CNT,
               "test_pmem_concurrent: not enough free pages");
    }

    // 第一阶段：等待两个 hart 都进入测试
    __atomic_store_n(&conc_ready[hartid], 1, __ATOMIC_RELEASE);

    while (__atomic_load_n(&conc_ready[1 - hartid],
                           __ATOMIC_ACQUIRE) == 0)
        ;

    // 第二阶段：各自申请 64 个物理页
    for (int i = 0; i < PMEM_CONC_CNT; i++)
    {
        conc_pages[hartid][i] = (uint64)pmem_alloc(false);
    }

    __atomic_store_n(&conc_alloc_done[hartid],
                     1, __ATOMIC_RELEASE);

    // 等待另一个 hart 完成分配
    while (__atomic_load_n(&conc_alloc_done[1 - hartid],
                           __ATOMIC_ACQUIRE) == 0)
        ;

    // 第三阶段：Hart 0 统一检查
    if (hartid == 0)
    {
        for (int h = 0; h < 2; h++)
        {
            for (int i = 0; i < PMEM_CONC_CNT; i++)
            {
                uint64 pa = conc_pages[h][i];

                // 页对齐检查
                assert(pa % PGSIZE == 0,
                       "concurrent: unaligned page");

                // 地址范围检查
                assert(pa >= user_region.begin &&
                       pa < user_region.end,
                       "concurrent: page out of range");

                // 检查所有页面是否重复
                for (int h2 = 0; h2 <= h; h2++)
                {
                    int count = (h2 == h) ? i : PMEM_CONC_CNT;

                    for (int j = 0; j < count; j++)
                    {
                        assert(pa != conc_pages[h2][j],
                               "concurrent: duplicate page");
                    }
                }
            }
        }

        spinlock_acquire(&user_region.lk);
        int actual = user_region.allocable;
        spinlock_release(&user_region.lk);

        assert(actual == initial_allocable - 2 * PMEM_CONC_CNT,
               "concurrent: allocable incorrect after alloc");

        printf("Concurrent allocation verification passed!\n");

        __atomic_store_n(&conc_verified, 1, __ATOMIC_RELEASE);
    }

    // Hart 1 等待 Hart 0 完成检查
    while (__atomic_load_n(&conc_verified,
                           __ATOMIC_ACQUIRE) == 0)
        ;

    // 第四阶段：两个 hart 分别释放自己的页面
    for (int i = 0; i < PMEM_CONC_CNT; i++)
    {
        pmem_free(conc_pages[hartid][i], false);
    }

    __atomic_store_n(&conc_free_done[hartid],
                     1, __ATOMIC_RELEASE);

    // 第五阶段：Hart 0 检查计数是否恢复
    if (hartid == 0)
    {
        while (__atomic_load_n(&conc_free_done[1],
                               __ATOMIC_ACQUIRE) == 0)
            ;

        spinlock_acquire(&user_region.lk);
        int actual = user_region.allocable;
        spinlock_release(&user_region.lk);

        assert(actual == initial_allocable,
               "concurrent: allocable not restored");

        printf("test_pmem_concurrent passed!\n");
    }
}