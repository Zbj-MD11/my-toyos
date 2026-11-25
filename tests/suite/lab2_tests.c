/*
 * lab-2 测试用例集
 *
 * 本文件不会出现在 src/ 里：tests/run_tests.sh 会把工程复制到
 * tests/.work/<用例>，再把本文件拷成 src/kernel/lab2_tests.c，
 * 并按用例重写 main.c（见 tests/templates/）。
 *
 * 判定约定：
 *   - 正常用例：最后打印 "RESULT: PASS <名字>"
 *   - 期望 panic 的用例：触发被测代码里的 assert/panic，输出里必须出现
 *     对应函数的 panic 原文；若错误输入被接受，会打印 "LAB2: ..." 自曝
 *
 * 覆盖设计（对应组员的三个要求）：
 *   路径覆盖：每个 if / 每个链表分支 / 页表每一级 alloc 分支都至少走一次
 *   等价类：  {内核区, 用户区} x {已分配, 空闲} x {对齐, 未对齐} x {合法, 越界}
 *   边界值：  len = 0 / 1 / PGSIZE-1 / PGSIZE / PGSIZE+1；va = 0 / VA_MAX-PGSIZE / VA_MAX；
 *            ALLOC_BEGIN 前一页 / ALLOC_END；2MB 与 1GB 页表边界
 */
#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/type.h"   // CLINT_BASE / PLIC_BASE（用例 38 用）

/* 可分配区间 [ALLOC_BEGIN, ALLOC_END)，前 KERN_PAGES 页归内核空间 */
#define KERN_END ((uint64)ALLOC_BEGIN + (uint64)KERN_PAGES * PGSIZE)

/* 测试使用的虚拟地址，避开 UART/CLINT/PLIC/内核，且都 < VA_MAX */
#define VA_A 0x60000000UL
#define VA_B 0x61000000UL
#define VA_C 0x62000000UL
#define VA_D 0x63000000UL

static void pass(const char *name)
{
    printf("RESULT: PASS %s\n", name);
}

/* va 是否已经建立了有效映射 */
static int pte_valid(pgtbl_t pgtbl, uint64 va)
{
    pte_t *pte = vm_getpte(pgtbl, va, false);
    return (pte != NULL) && ((*pte & PTE_V) != 0);
}

/* 申请 n 个物理连续页，返回最低地址（测试夹具） */
static uint64 alloc_run(int n, bool in_kernel)
{
    uint64 base = (uint64)-1;
    uint64 top = 0;
    int i;

    for (i = 0; i < n; i++)
    {
        uint64 pa = (uint64)pmem_alloc(in_kernel);
        if (pa < base)
            base = pa;
        if (pa > top)
            top = pa;
    }
    assert(top - base == (uint64)(n - 1) * PGSIZE,
           "LAB2: allocator did not return contiguous pages");
    return base;
}

/* ============================================================
 * A. 基础：启动 + Sv39
 * ============================================================ */

/* 用例 02：satp 进入 Sv39，且内核页表是恒等映射 */
void lab2_case_sv39_identity(int hartid)
{
    uint64 satp;
    uint64 pa;
    int mode;

    if (hartid)
        return;

    satp = r_satp();
    mode = (int)(satp >> 60);
    printf("satp mode = %d (expect 8 = Sv39)\n", mode);
    assert(mode == 8, "LAB2: Sv39 not enabled (satp MODE != 8)");

    /* 恒等映射下，物理页的 pa 可以直接当 va 用 */
    pa = (uint64)pmem_alloc(false);
    *(volatile uint64 *)pa = 0x1234abcdUL;
    assert(*(volatile uint64 *)pa == 0x1234abcdUL,
           "LAB2: identity mapping broken");

    printf("identity ok at pa %p\n", (void *)pa);
    pass("sv39_identity");
}

/* ============================================================
 * B. 物理内存：pmem_init / pmem_alloc / pmem_free
 * ============================================================ */

/* 用例 03：内核区恰好 1024 页，耗尽后 panic（链表空分支） */
void lab2_case_pmem_kernel_exhaust(int hartid)
{
    int i;
    uint64 pa = 0;

    if (hartid)
        return;

    for (i = 0; i < KERN_PAGES; i++)
    {
        pa = (uint64)pmem_alloc(true);
        assert(pa % PGSIZE == 0, "LAB2: kernel page not aligned");
        assert(pa >= (uint64)ALLOC_BEGIN && pa < KERN_END,
               "LAB2: kernel page outside kernel region");
    }

    printf("kernel pages = %d (expected %d)\n", i, (int)KERN_PAGES);
    printf("last alloc = %p, region = [%p, %p)\n",
           (void *)pa, ALLOC_BEGIN, (void *)KERN_END);

    (void)pmem_alloc(true);
    panic("LAB2: kernel exhaustion not detected");
}

/* 用例 04：用户区页数 = 总页数 - 1024，耗尽后 panic */
void lab2_case_pmem_user_exhaust(int hartid)
{
    uint64 total;
    int expect;
    int i;
    uint64 pa = 0;

    if (hartid)
        return;

    total = ((uint64)ALLOC_END - (uint64)ALLOC_BEGIN) / PGSIZE;
    assert(((uint64)ALLOC_END - (uint64)ALLOC_BEGIN) % PGSIZE == 0,
           "LAB2: alloc region is not page aligned");
    expect = (int)(total - (uint64)KERN_PAGES);

    for (i = 0; i < expect; i++)
    {
        pa = (uint64)pmem_alloc(false);
        assert(pa % PGSIZE == 0, "LAB2: user page not aligned");
        assert(pa >= KERN_END && pa < (uint64)ALLOC_END,
               "LAB2: user page outside user region");
    }

    printf("user pages = %d (expected %d)\n", i, expect);
    printf("last alloc = %p, region = [%p, %p)\n",
           (void *)pa, (void *)KERN_END, ALLOC_END);
    printf("count ok\n");

    (void)pmem_alloc(false);
    panic("LAB2: user exhaustion not detected");
}

/* 用例 05：两区域不重叠、页对齐、分配不重复（等价类：区域 x 页） */
#define TINY 16
static uint64 kmem[TINY];
static uint64 umem[TINY];

void lab2_case_pmem_alloc_unique(int hartid)
{
    int i, j;

    if (hartid)
        return;

    for (i = 0; i < TINY; i++)
    {
        kmem[i] = (uint64)pmem_alloc(true);
        assert(kmem[i] % PGSIZE == 0, "LAB2: kernel page unaligned");
        assert(kmem[i] >= (uint64)ALLOC_BEGIN && kmem[i] < KERN_END,
               "LAB2: kernel page out of kernel region");
    }

    for (i = 0; i < TINY; i++)
    {
        umem[i] = (uint64)pmem_alloc(false);
        assert(umem[i] % PGSIZE == 0, "LAB2: user page unaligned");
        assert(umem[i] >= KERN_END && umem[i] < (uint64)ALLOC_END,
               "LAB2: user page out of user region");
    }

    for (i = 0; i < TINY; i++)
    {
        for (j = i + 1; j < TINY; j++)
        {
            assert(kmem[i] != kmem[j], "LAB2: duplicate kernel page");
            assert(umem[i] != umem[j], "LAB2: duplicate user page");
        }
        for (j = 0; j < TINY; j++)
            assert(kmem[i] != umem[j], "LAB2: kernel/user page overlap");
    }

    for (i = 0; i < TINY; i++)
    {
        pmem_free(kmem[i], true);
        pmem_free(umem[i], false);
    }

    printf("unique ok: %d kernel + %d user pages\n", TINY, TINY);
    pass("pmem_alloc_unique");
}

/* 用例 06：释放的页必须被重新分配（回收路径） */
#define LIFO_N 8
static uint64 lifo_old[LIFO_N];
static uint64 lifo_new[LIFO_N];

void lab2_case_pmem_lifo_reuse(int hartid)
{
    int i, j, found;

    if (hartid)
        return;

    for (i = 0; i < LIFO_N; i++)
        lifo_old[i] = (uint64)pmem_alloc(false);

    for (i = 0; i < LIFO_N; i++)
        pmem_free(lifo_old[i], false);

    for (i = 0; i < LIFO_N; i++)
    {
        lifo_new[i] = (uint64)pmem_alloc(false);
        found = 0;
        for (j = 0; j < LIFO_N; j++)
            if (lifo_new[i] == lifo_old[j])
                found = 1;
        assert(found, "LAB2: freed page was not reused");
    }

    for (i = 0; i < LIFO_N; i++)
        pmem_free(lifo_new[i], false);

    printf("reuse ok: %d pages recycled\n", LIFO_N);
    pass("pmem_lifo_reuse");
}

/* 用例 07：重新分配到的页必须是清零的 */
void lab2_case_pmem_zero_after_free(int hartid)
{
    uint8 *page;
    uint8 *again;
    int i;

    if (hartid)
        return;

    page = (uint8 *)pmem_alloc(false);
    memset(page, 0xAA, PGSIZE);
    pmem_free((uint64)page, false);

    again = (uint8 *)pmem_alloc(false);
    assert((uint64)again == (uint64)page,
           "LAB2: allocator did not return the just-freed page");
    for (i = 0; i < PGSIZE; i++)
        if (again[i] != 0)
            panic("LAB2: page not zeroed after reallocation");

    pmem_free((uint64)again, false);
    printf("zero on alloc ok\n");
    pass("pmem_zero_after_free");
}

/* 用例 08：未对齐地址释放 -> panic */
void lab2_case_pmem_free_unaligned(int hartid)
{
    uint64 pa;

    if (hartid)
        return;

    pa = (uint64)pmem_alloc(false);
    printf("free unaligned %p, expect panic\n", (void *)(pa + PGSIZE / 2));
    pmem_free(pa + PGSIZE / 2, false);
    panic("LAB2: unaligned free was accepted");
}

/* 用例 09：ALLOC_BEGIN 之前一页 -> panic（左边界外） */
void lab2_case_pmem_free_below_range(int hartid)
{
    if (hartid)
        return;

    printf("free %p (ALLOC_BEGIN - PGSIZE), expect panic\n",
           (void *)((uint64)ALLOC_BEGIN - PGSIZE));
    pmem_free((uint64)ALLOC_BEGIN - PGSIZE, false);
    panic("LAB2: free below ALLOC_BEGIN was accepted");
}

/* 用例 10：ALLOC_END 本身 -> panic（右边界，半开区间端点） */
void lab2_case_pmem_free_at_end(int hartid)
{
    if (hartid)
        return;

    printf("free %p (ALLOC_END), expect panic\n", (void *)(uint64)ALLOC_END);
    pmem_free((uint64)ALLOC_END, false);
    panic("LAB2: free at ALLOC_END was accepted");
}

/* 用例 11：用户页当内核页释放 -> panic（区域标签错误） */
void lab2_case_pmem_free_user_as_kernel(int hartid)
{
    uint64 pa;

    if (hartid)
        return;

    pa = (uint64)pmem_alloc(false);
    printf("free user page %p as kernel page, expect panic\n", (void *)pa);
    pmem_free(pa, true);
    panic("LAB2: user page freed as kernel page was accepted");
}

/* 用例 12：内核页当用户页释放 -> panic */
void lab2_case_pmem_free_kernel_as_user(int hartid)
{
    uint64 pa;

    if (hartid)
        return;

    pa = (uint64)pmem_alloc(true);
    printf("free kernel page %p as user page, expect panic\n", (void *)pa);
    pmem_free(pa, false);
    panic("LAB2: kernel page freed as user page was accepted");
}

/* 用例 13：重复释放 -> 必须被拒绝（当前实现未检测，预期 FAIL） */
void lab2_case_pmem_double_free(int hartid)
{
    uint64 pa;

    if (hartid)
        return;

    pa = (uint64)pmem_alloc(false);
    printf("first free %p\n", (void *)pa);
    pmem_free(pa, false);

    printf("second free %p, expect rejection\n", (void *)pa);
    pmem_free(pa, false);

    panic("LAB2: double free accepted");
}

/* ============================================================
 * C. 内核态虚拟内存：vm_getpte / vm_mappages / vm_unmappages
 * ============================================================ */

/* 用例 14：getpte 的 alloc=false / alloc=true 两条路径 */
void lab2_case_kvm_getpte_paths(int hartid)
{
    pgtbl_t tbl;
    uint64 va = 0x40000000UL;
    pte_t *p0;
    pte_t *p1;
    pte_t *p2;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);

    p0 = vm_getpte(tbl, va, false);
    assert(p0 == NULL, "LAB2: getpte(alloc=false) must be NULL on empty pgtbl");
    printf("getpte(false) on empty table -> NULL\n");

    p1 = vm_getpte(tbl, va, true);
    assert(p1 != NULL, "LAB2: getpte(alloc=true) returned NULL");
    assert((*p1 & PTE_V) == 0, "LAB2: freshly allocated leaf must be invalid");
    printf("getpte(true) -> leaf %p, pte = %x (V=0)\n", (void *)p1, (uint64)*p1);

    p2 = vm_getpte(tbl, va, false);
    assert(p2 == p1, "LAB2: leaf pte address changed between calls");

    pass("kvm_getpte_paths");
}

/* 用例 15：单页映射，检查 V / PA / 权限位 */
void lab2_case_kvm_map_single(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;
    pte_t *pte;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    vm_mappages(tbl, VA_A, pa, PGSIZE, PTE_R | PTE_W);

    pte = vm_getpte(tbl, VA_A, false);
    assert(pte != NULL && (*pte & PTE_V) != 0, "LAB2: pte not valid after map");
    assert(PTE_TO_PA(*pte) == pa, "LAB2: mapped pa mismatch");
    assert((*pte & (PTE_R | PTE_W | PTE_X | PTE_U)) == (PTE_R | PTE_W),
           "LAB2: permission bits mismatch");
    assert(!pte_valid(tbl, VA_A + PGSIZE), "LAB2: neighbour page should be unmapped");

    printf("map ok: va %p -> pa %p, flags %d\n",
           (void *)VA_A, (void *)pa, (int)PTE_FLAGS(*pte));
    pass("kvm_map_single");
}

/* 用例 16：多页映射，每页 PA 递增 */
void lab2_case_kvm_map_multi(int hartid)
{
    pgtbl_t tbl;
    uint64 base;
    pte_t *pte;
    int i;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    base = alloc_run(3, false);
    vm_mappages(tbl, VA_D, base, 3 * PGSIZE, PTE_R);

    for (i = 0; i < 3; i++)
    {
        pte = vm_getpte(tbl, VA_D + (uint64)i * PGSIZE, false);
        assert(pte != NULL && (*pte & PTE_V) != 0, "LAB2: multi-page pte invalid");
        assert(PTE_TO_PA(*pte) == base + (uint64)i * PGSIZE,
               "LAB2: multi-page pa mismatch");
    }
    assert(!pte_valid(tbl, VA_D + 3 * PGSIZE), "LAB2: mapped beyond len");

    printf("multi map ok: 3 pages from %p\n", (void *)base);
    pass("kvm_map_multi");
}

/* 用例 17：len 边界 1 / PGSIZE-1 / PGSIZE+1（向上取整） */
void lab2_case_kvm_map_len_boundary(int hartid)
{
    pgtbl_t tbl;
    uint64 pa1, pa2, pa3;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);

    pa1 = alloc_run(1, false);
    vm_mappages(tbl, VA_A, pa1, 1, PTE_R);
    assert(pte_valid(tbl, VA_A), "LAB2: len=1 should map one page");
    assert(!pte_valid(tbl, VA_A + PGSIZE), "LAB2: len=1 mapped a second page");

    pa2 = alloc_run(1, false);
    vm_mappages(tbl, VA_B, pa2, PGSIZE - 1, PTE_R);
    assert(pte_valid(tbl, VA_B), "LAB2: len=PGSIZE-1 should map one page");
    assert(!pte_valid(tbl, VA_B + PGSIZE), "LAB2: len=PGSIZE-1 mapped two pages");

    pa3 = alloc_run(2, false);
    vm_mappages(tbl, VA_C, pa3, PGSIZE + 1, PTE_R);
    assert(pte_valid(tbl, VA_C), "LAB2: len=PGSIZE+1 page0 missing");
    assert(pte_valid(tbl, VA_C + PGSIZE), "LAB2: len=PGSIZE+1 page1 missing");
    assert(!pte_valid(tbl, VA_C + 2 * PGSIZE), "LAB2: len=PGSIZE+1 mapped 3 pages");

    printf("len boundary ok: 1/%d/%d\n", PGSIZE - 1, PGSIZE + 1);
    pass("kvm_map_len_boundary");
}

/* 用例 18：跨 2MB 边界（需要新建二级页表） */
void lab2_case_kvm_map_cross_2mb(int hartid)
{
    pgtbl_t tbl;
    uint64 va = 0x200000UL - PGSIZE;
    uint64 base;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    base = alloc_run(2, false);
    vm_mappages(tbl, va, base, 2 * PGSIZE, PTE_R);

    assert(pte_valid(tbl, va), "LAB2: cross-2MB lower page missing");
    assert(pte_valid(tbl, va + PGSIZE), "LAB2: cross-2MB upper page missing");
    printf("cross 2MB ok: %p, %p\n", (void *)va, (void *)(va + PGSIZE));
    pass("kvm_map_cross_2mb");
}

/* 用例 19：跨 1GB 边界（需要新建一级页表） */
void lab2_case_kvm_map_cross_1gb(int hartid)
{
    pgtbl_t tbl;
    uint64 va = 0x40000000UL - PGSIZE;
    uint64 base;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    base = alloc_run(2, false);
    vm_mappages(tbl, va, base, 2 * PGSIZE, PTE_R);

    assert(pte_valid(tbl, va), "LAB2: cross-1GB lower page missing");
    assert(pte_valid(tbl, va + PGSIZE), "LAB2: cross-1GB upper page missing");
    printf("cross 1GB ok: %p, %p\n", (void *)va, (void *)(va + PGSIZE));
    pass("kvm_map_cross_1gb");
}

/* 用例 20：最高可用虚地址 VA_MAX-PGSIZE */
void lab2_case_kvm_map_top_va(int hartid)
{
    pgtbl_t tbl;
    uint64 va = (uint64)VA_MAX - PGSIZE;
    uint64 pa;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    vm_mappages(tbl, va, pa, PGSIZE, PTE_R);
    assert(pte_valid(tbl, va), "LAB2: top VA not mapped");
    printf("top va ok: %p\n", (void *)va);
    pass("kvm_map_top_va");
}

/* 用例 21：va == VA_MAX -> panic（右边界外） */
void lab2_case_kvm_map_va_max(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    printf("map va = VA_MAX, expect panic\n");
    vm_mappages(tbl, (uint64)VA_MAX, pa, PGSIZE, PTE_R);
    panic("LAB2: va == VA_MAX was accepted");
}

/* 用例 22：va 未对齐 -> panic */
void lab2_case_kvm_map_unaligned_va(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    printf("map unaligned va %p, expect panic\n", (void *)(VA_A + 1));
    vm_mappages(tbl, VA_A + 1, pa, PGSIZE, PTE_R);
    panic("LAB2: unaligned va was accepted");
}

/* 用例 23：pa 未对齐 -> panic */
void lab2_case_kvm_map_unaligned_pa(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    printf("map unaligned pa %p, expect panic\n", (void *)(pa + 1));
    vm_mappages(tbl, VA_A, pa + 1, PGSIZE, PTE_R);
    panic("LAB2: unaligned pa was accepted");
}

/* 用例 24：len == 0 -> panic（左边界） */
void lab2_case_kvm_map_len_zero(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    printf("map len = 0, expect panic\n");
    vm_mappages(tbl, VA_A, pa, 0, PTE_R);
    panic("LAB2: len == 0 was accepted");
}

/* 用例 25：va+len 溢出 -> panic */
void lab2_case_kvm_map_overflow(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;
    uint64 va = (uint64)VA_MAX - PGSIZE;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    printf("map [VA_MAX-PGSIZE, +PGSIZE+1), expect panic\n");
    vm_mappages(tbl, va, pa, PGSIZE + 1, PTE_R);
    panic("LAB2: out-of-range mapping was accepted");
}

/* 用例 26：重复映射同一 VA -> panic（防御性检查） */
void lab2_case_kvm_map_duplicate(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    vm_mappages(tbl, VA_A, pa, PGSIZE, PTE_R);
    printf("map same va twice, expect panic\n");
    vm_mappages(tbl, VA_A, pa, PGSIZE, PTE_R);
    panic("LAB2: duplicate mapping was accepted");
}

/* 用例 27：权限位原样写入，不夹带其它位 */
void lab2_case_kvm_perm_flags(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;
    pte_t *pte;
    pte_t want = (pte_t)(PTE_V | PTE_R | PTE_X);

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    vm_mappages(tbl, VA_A, pa, PGSIZE, PTE_R | PTE_X);

    pte = vm_getpte(tbl, VA_A, false);
    assert(pte != NULL, "LAB2: pte missing");
    assert((*pte & 0x3FF) == want, "LAB2: pte flags must be exactly V|R|X");
    printf("flags ok: %d\n", (int)PTE_FLAGS(*pte));
    pass("kvm_perm_flags");
}

/* 用例 28：部分解映射（freeit=false），其余映射不受影响 */
void lab2_case_kvm_unmap_partial(int hartid)
{
    pgtbl_t tbl;
    uint64 base;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    base = alloc_run(4, false);
    vm_mappages(tbl, VA_A, base, 4 * PGSIZE, PTE_R);

    vm_unmappages(tbl, VA_A + PGSIZE, 2 * PGSIZE, false);

    assert(pte_valid(tbl, VA_A), "LAB2: first page lost");
    assert(!pte_valid(tbl, VA_A + PGSIZE), "LAB2: middle page still mapped");
    assert(!pte_valid(tbl, VA_A + 2 * PGSIZE), "LAB2: middle page still mapped");
    assert(pte_valid(tbl, VA_A + 3 * PGSIZE), "LAB2: last page lost");

    /* 解映射后 PTE 必须回到"无效但存在"，可以再次映射 */
    vm_mappages(tbl, VA_A + PGSIZE, base + PGSIZE, 2 * PGSIZE, PTE_R);
    assert(pte_valid(tbl, VA_A + PGSIZE), "LAB2: remap after unmap failed");

    vm_unmappages(tbl, VA_A, 4 * PGSIZE, false);
    assert(!pte_valid(tbl, VA_A), "LAB2: full unmap failed");

    printf("partial unmap ok\n");
    pass("kvm_unmap_partial");
}

/* 用例 29：freeit=true 必须把物理页还给用户区（回到空闲链表） */
void lab2_case_kvm_unmap_freeit(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;
    uint64 again;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    vm_mappages(tbl, VA_A, pa, PGSIZE, PTE_R);
    vm_unmappages(tbl, VA_A, PGSIZE, true);

    assert(!pte_valid(tbl, VA_A), "LAB2: pte still valid after unmap");

    again = (uint64)pmem_alloc(false);
    assert(again == pa, "LAB2: freed page not returned to the free list");
    pmem_free(again, false);

    printf("freeit ok: %p recycled\n", (void *)pa);
    pass("kvm_unmap_freeit");
}

/* 用例 30：解映射从未映射的 VA -> panic（页表路径缺失） */
void lab2_case_kvm_unmap_unmapped(int hartid)
{
    pgtbl_t tbl;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    printf("unmap never-mapped va, expect panic\n");
    vm_unmappages(tbl, VA_A, PGSIZE, false);
    panic("LAB2: unmapping an unmapped va was accepted");
}

/* 用例 31：解映射已无效的叶子（V=0）-> panic */
void lab2_case_kvm_unmap_invalid_leaf(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    vm_mappages(tbl, VA_A, pa, PGSIZE, PTE_R);
    vm_unmappages(tbl, VA_A, PGSIZE, false);

    printf("unmap same va twice, expect panic\n");
    vm_unmappages(tbl, VA_A, PGSIZE, false);
    panic("LAB2: double unmap was accepted");
}

/* 用例 32：解映射 len == 0 -> panic */
void lab2_case_kvm_unmap_len_zero(int hartid)
{
    pgtbl_t tbl;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    printf("unmap len = 0, expect panic\n");
    vm_unmappages(tbl, VA_A, 0, false);
    panic("LAB2: unmap len == 0 was accepted");
}

/* 用例 33：解映射区间溢出 -> panic */
void lab2_case_kvm_unmap_overflow(int hartid)
{
    pgtbl_t tbl;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    printf("unmap [VA_MAX-PGSIZE, +PGSIZE+1), expect panic\n");
    vm_unmappages(tbl, (uint64)VA_MAX - PGSIZE, PGSIZE + 1, false);
    panic("LAB2: out-of-range unmap was accepted");
}

/* 用例 34：解映射 va 未对齐 -> panic */
void lab2_case_kvm_unmap_unaligned(int hartid)
{
    pgtbl_t tbl;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    printf("unmap unaligned va, expect panic\n");
    vm_unmappages(tbl, VA_A + 1, PGSIZE, false);
    panic("LAB2: unaligned unmap was accepted");
}

/* 用例 35：getpte(va == VA_MAX) -> panic */
void lab2_case_kvm_getpte_va_max(int hartid)
{
    pgtbl_t tbl;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    printf("getpte va = VA_MAX, expect panic\n");
    (void)vm_getpte(tbl, (uint64)VA_MAX, false);
    panic("LAB2: getpte accepted va == VA_MAX");
}

/* 用例 37：vm_print 输出三级页表结构（debug 接口） */
void lab2_case_kvm_vm_print(int hartid)
{
    pgtbl_t tbl;
    uint64 pa;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    pa = (uint64)pmem_alloc(false);
    vm_mappages(tbl, VA_A, pa, PGSIZE, PTE_R | PTE_W);

    printf("vm_print start\n");
    vm_print(tbl);
    printf("vm_print end\n");
    pass("kvm_vm_print");
}

/* 用例 38：kvm_init 建立的设备 / 内核 / 可分配区映射 */
void lab2_case_kvm_init_mappings(int hartid)
{
    uint64 satp;
    pgtbl_t tbl;
    pte_t *pte;

    if (hartid)
        return;

    /* 从 satp 取根页表物理地址，顺便验证 satp 的 PPN 字段 */
    satp = r_satp();
    tbl = (pgtbl_t)((satp & ((1UL << 44) - 1)) << 12);
    printf("root pgtbl from satp = %p\n", (void *)tbl);
    assert((uint64)tbl % PGSIZE == 0, "LAB2: satp PPN is not page aligned");

    pte = vm_getpte(tbl, UART_BASE, false);
    assert(pte != NULL && (*pte & PTE_V) != 0, "LAB2: UART not mapped");
    assert(PTE_TO_PA(*pte) == (uint64)UART_BASE, "LAB2: UART mapping not identity");
    assert((*pte & (PTE_R | PTE_W)) == (PTE_R | PTE_W), "LAB2: UART needs R|W");

    pte = vm_getpte(tbl, CLINT_BASE, false);
    assert(pte != NULL && (*pte & PTE_V) != 0, "LAB2: CLINT not mapped");
    assert(PTE_TO_PA(*pte) == (uint64)CLINT_BASE, "LAB2: CLINT mapping not identity");

    pte = vm_getpte(tbl, PLIC_BASE, false);
    assert(pte != NULL && (*pte & PTE_V) != 0, "LAB2: PLIC not mapped");
    assert(PTE_TO_PA(*pte) == (uint64)PLIC_BASE, "LAB2: PLIC mapping not identity");

    pte = vm_getpte(tbl, (uint64)KERNEL_BASE, false);
    assert(pte != NULL && (*pte & PTE_V) != 0, "LAB2: kernel text not mapped");
    assert((*pte & PTE_X) != 0, "LAB2: kernel text needs PTE_X");

    pte = vm_getpte(tbl, (uint64)ALLOC_END - PGSIZE, false);
    assert(pte != NULL && (*pte & PTE_V) != 0, "LAB2: alloc region not mapped");
    assert((*pte & PTE_W) != 0, "LAB2: alloc region needs PTE_W");

    printf("kvm_init mappings ok\n");
    pass("kvm_init_mappings");
}

/* 用例 39：解映射 va == VA_MAX -> panic（`va >= VA_MAX` 这一侧） */
void lab2_case_kvm_unmap_va_max(int hartid)
{
    pgtbl_t tbl;

    if (hartid)
        return;

    tbl = (pgtbl_t)pmem_alloc(true);
    printf("unmap va = VA_MAX, expect panic\n");
    vm_unmappages(tbl, (uint64)VA_MAX, PGSIZE, false);
    panic("LAB2: unmap va == VA_MAX was accepted");
}

/* ============================================================
 * D. 并发：两个 hart 同时分配 / 释放
 * ============================================================ */

#define CONC 64
static uint64 conc[2][CONC];
static int conc_ready[2];
static int conc_verified;

void lab2_case_pmem_concurrent(int hartid)
{
    int i, j, h;

    __atomic_store_n(&conc_ready[hartid], 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&conc_ready[1 - hartid], __ATOMIC_ACQUIRE) == 0)
        ;

    for (i = 0; i < CONC; i++)
        conc[hartid][i] = (uint64)pmem_alloc(false);

    if (hartid == 0)
    {
        for (h = 0; h < 2; h++)
        {
            for (i = 0; i < CONC; i++)
            {
                uint64 pa = conc[h][i];
                assert(pa % PGSIZE == 0, "LAB2: concurrent page unaligned");
                assert(pa >= KERN_END && pa < (uint64)ALLOC_END,
                       "LAB2: concurrent page out of user region");
                for (j = 0; j < i; j++)
                    assert(pa != conc[h][j], "LAB2: duplicate page (same hart)");
                for (j = 0; j < CONC; j++)
                    assert(pa != conc[1 - h][j], "LAB2: duplicate page (cross hart)");
            }
        }
        printf("concurrent alloc ok: %d pages, no duplicate\n", 2 * CONC);
        __atomic_store_n(&conc_verified, 1, __ATOMIC_RELEASE);
    }

    while (__atomic_load_n(&conc_verified, __ATOMIC_ACQUIRE) == 0)
        ;

    for (i = 0; i < CONC; i++)
        pmem_free(conc[hartid][i], false);

    if (hartid != 0)
    {
        while (1)
            ;
    }

    /* Hart 0：全部释放后必须还能重新分配出 128 个不重复的页 */
    for (i = 0; i < CONC; i++)
    {
        conc[0][i] = (uint64)pmem_alloc(false);
        for (j = 0; j < i; j++)
            assert(conc[0][i] != conc[0][j], "LAB2: duplicate after concurrent free");
    }

    printf("concurrent free + realloc ok\n");
    pass("pmem_concurrent");
}
