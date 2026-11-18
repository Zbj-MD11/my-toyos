#include "mod.h"

/*--------------------part-1: 关于内核空间<->用户空间的数据传递--------------------*/

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    //不应该传入len==0
    assert(len>0,"uvm_copyin:Invalid len");
    //这里提前判断一下会不会超出界限
    assert(
    src < VA_MAX && len <= VA_MAX - src,
    "uvm_copyin: Invalid src range"
    );

    assert(
    dst < VA_MAX && len <= VA_MAX - dst,
    "uvm_copyin: Invalid dst range"
    );
    while(len>0){
        //判断地址是否合法
        assert(dst<VA_MAX,"uvm_copyin:Invalid dst VA");
        assert(src<VA_MAX,"uvm_copyin:Invalid src VA");
        uint64 va_page = src - (src % PGSIZE);
        uint64 offset = src % PGSIZE;
        pte_t *pte = vm_getpte(pgtbl,va_page,false);
        //检查页表项指针是否为空以及是否有对应权限
        assert(pte != NULL, "uvm_copyin: PTE is NULL");
        assert(
        (*pte & (PTE_V | PTE_U | PTE_R)) ==
        (PTE_V | PTE_U | PTE_R),
        "uvm_copyin: invalid PTE permissions"
        );
        uint64 pa_page = PTE_TO_PA(*pte);
        //计算此次复制的大小
        uint32 copy_Byte_Sum;
        if( PGSIZE - offset > len){
            copy_Byte_Sum = len;
        }else{
            copy_Byte_Sum = PGSIZE - offset;
        }
        //复制对应字节
        memmove((void *)dst,
            (const void *)(pa_page+offset),
            copy_Byte_Sum   
        );
        //更新对应地址
        src += copy_Byte_Sum;
        dst += copy_Byte_Sum;
        len -= copy_Byte_Sum;
    }
    

}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    //不应该传入len==0
    assert(len>0,"uvm_copyout:Invalid len");
    //这里提前判断一下会不会超出界限
    assert(
    src < VA_MAX && len <= VA_MAX - src,
    "uvm_copyout: Invalid src range"
    );

    assert(
    dst < VA_MAX && len <= VA_MAX - dst,
    "uvm_copyout: Invalid dst range"
    );
    while(len>0){
        //判断地址是否合法
        assert(dst<VA_MAX,"uvm_copyout:Invalid dst VA");
        assert(src<VA_MAX,"uvm_copyout:Invalid src VA");
        uint64 va_page = dst - (dst % PGSIZE);
        uint64 offset = dst % PGSIZE;
        
        pte_t *pte = vm_getpte(pgtbl,va_page,false);
        //检查页表项指针是否为空以及是否有对应权限
        assert(pte != NULL, "uvm_copyout: PTE is NULL");
        assert(
        (*pte & (PTE_V | PTE_U | PTE_W)) ==
        (PTE_V | PTE_U | PTE_W),
        "uvm_copyout: invalid PTE permissions"
        );
        uint64 pa_page = PTE_TO_PA(*pte);
        //计算此次复制的大小
        uint32 copy_Byte_Sum;
        if( PGSIZE - offset > len){
            copy_Byte_Sum = len;
        }else{
            copy_Byte_Sum = PGSIZE - offset;
        }
        //复制对应字节
        memmove((void *)(pa_page+offset),
            (const void *)(src),
            copy_Byte_Sum   
        );
        //更新对应地址
        src += copy_Byte_Sum;
        dst += copy_Byte_Sum;
        len -= copy_Byte_Sum;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{

    uint32 i=0;
    while(i<maxlen){
        uvm_copyin(pgtbl,dst,src,1);
        if(*(char*)dst=='\0'){
            //遇到‘/0’直接return
            return;
        }
        i++;
        dst++;
        src++;
    }
    
    assert(false,"uvm_copyin_str:String too long");
    
}

/*--------------------part-2: mmap_region相关--------------------*/

// 打印以mmap为首的mmap链
// for debug
void uvm_show_mmaplist(mmap_region_t *mmap)
{
    mmap_region_t *tmp = mmap;
    printf("\nalloced mmap_space:\n");
    if (tmp == NULL)
        printf("empty\n");
    while (tmp != NULL)
    {
        printf("alloced mmap_region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 两个 mmap_region 区域合并
// 注意: 保留一个 释放一个 不操作 next 指针
// 由uvm_mmap调用
static void mmap_merge(mmap_region_t *mmap_1, mmap_region_t *mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");

    // merge
    if (keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 寻找一块足够大的区域(len), 作为 mmap_region
// 由uvm_mmap调用(处理begin==0的情况)
// 成功返回begin, 失败返回0

//First Fit!
static uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len, mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap)
{

    // 检查申请长度是否合法
    assert(len!=0&&len<=MMAP_END-MMAP_BEGIN,"uvm_mmap_find:Invalid len");

    // 如果当前进程的 mmap 链表为空
    // 说明整个 mmap 地址区域都尚未分配
    if(head_mmap==NULL){
        
        // 新区域没有前驱节点和后继节点
        *p_last_mmap = NULL;
        *p_tmp_mmap = NULL;

        // 从 mmap 区域起始地址分配
        return MMAP_BEGIN;
    }

    // 当前尝试分配的起始虚拟地址
    // 初始时从 mmap 区域的最低地址开始查找
    uint64 candidate = MMAP_BEGIN;

    // last 表示当前节点的前驱节点
    // tmp 表示当前正在检查的已分配区域
    mmap_region_t *last = NULL;
    mmap_region_t *tmp = head_mmap;

    // 遍历按 begin 递增排列的 mmap 链表
    // 查找第一块足够大的空闲虚拟地址区域
    while(tmp!=NULL){

        // 当前可用的空隙为 [candidate, tmp->begin)
        // 如果空隙足够容纳 len 字节，则查找成功
        if(tmp->begin-candidate>=len){
            // 记录新区域应该插入的位置
            // 新节点将被插入 last 和 tmp 之间
            *p_last_mmap = last;
            *p_tmp_mmap = tmp;

            return candidate;
        }
        // 当前空隙不够大
        // 将候选地址移动到当前已分配区域的结束位置
        candidate = tmp->begin + (uint64)tmp->npages * PGSIZE;
        last=tmp;
        tmp = tmp->next;
        
    }
     // 遍历结束后，还需要检查最后一个节点之后的空闲区域
    if (MMAP_END - candidate >= len){
        // last 指向原来的最后一个节点
        // tmp 为 NULL，表示不存在后继节点
        *p_last_mmap = last;
        *p_tmp_mmap = NULL;

        return candidate;
    }else{
        return 0;
    }
    
}

// 在用户页表和进程mmap链里新增mmap区域 [begin, begin + npages * PGSIZE)
// 调用者保证begin是page-aligned的, 页面权限为perm
// 注意: 如果start==0, 意味着需要内核自主找一块足够大的空间
// 失败则panic卡死
uint64 uvm_mmap(uint64 begin, uint32 npages, int perm)
{   
    // 检查申请页数是否合法
    assert(npages>0&&npages <= (MMAP_END - MMAP_BEGIN) / PGSIZE,"uvm_mmap:Invalid npages");
    
     // 计算申请的字节数
    uint64 len = (uint64)npages * PGSIZE;

     // 获取当前进程
    proc_t *p = myproc();
    // 记录新区域的插入位置
    mmap_region_t *last = NULL;        
    mmap_region_t *tmp = NULL;

    if(begin==0){

        // 用户未指定地址，由内核自动寻找可用区域
        begin = uvm_mmap_find(
        p->mmap,
        len,
        &last,
        &tmp
        );

        assert(
        begin != 0,
        "uvm_mmap: no available mmap region"
        );

        
    }else{
        // 检查指定地址是否页对齐 
       
        assert(begin%PGSIZE==0,"uvm_mmap:The begin addr is not page-aligned");
        // 检查申请范围是否位于 mmap 区域内
        assert(
        begin >= MMAP_BEGIN &&
        begin < MMAP_END &&
        len <= MMAP_END - begin,
        "uvm_mmap: invalid mmap range"
        );
        // 从当前进程 mmap 链表的第一个节点开始查找
        // 寻找新区域的插入位置
        tmp = p->mmap;

        while (tmp != NULL && tmp->begin < begin)
        {
            last = tmp;
            tmp = tmp->next;
        }
        // 检查是否与前驱节点重叠
        if (last != NULL)
        {
            uint64 last_end =
            last->begin + (uint64)last->npages * PGSIZE;

            assert(
                last_end <= begin,
                "uvm_mmap: overlap with previous region"
            );
        }

        // 检查是否与后继节点重叠
        if (tmp != NULL)
        {
            assert(
                len <= tmp->begin - begin,
                "uvm_mmap: overlap with next region"
            );
        }

    }

        // 从全局节点仓库申请一个 mmap_region_t
    mmap_region_t *region = mmap_region_alloc();

    // 填写新区域的信息
    region->begin = begin;
    region->npages = npages;
    region->next = tmp;

    // 将新区域插入进程 mmap 链表
    if (last == NULL)
    {
        // 新区域成为链表第一个节点
        p->mmap = region;
    }
    else
    {
        // 新区域插入 last 和 tmp 之间
        last->next = region;
    }

    // 检查新区域是否与前驱节点相邻
    if (last != NULL &&
        last->begin + (uint64)last->npages * PGSIZE == region->begin)
    {
        // 保留前驱节点，移除新申请的节点
        // 先维护链表连接，再调用 mmap_merge()
        last->next = region->next;

        mmap_merge(last, region, true);

        // 合并后，last 成为合并区域的描述节点
        region = last;
    }

    // 检查合并后的区域是否与后继节点相邻
    if (tmp != NULL &&
        region->begin + (uint64)region->npages * PGSIZE == tmp->begin)
    {
        // 保留 region，移除后继节点
        // 先维护链表连接，再调用 mmap_merge()
        region->next = tmp->next;

        mmap_merge(region, tmp, true);
    }

    // 为本次新增的虚拟地址逐页分配物理内存
    for (uint64 va = begin; va < begin + len; va += PGSIZE)
    {
        // 从用户物理页分配器申请一个页面
        void *page = pmem_alloc(false);

        // 在当前进程的用户页表中建立映射
        vm_mappages(
            p->pgtbl,
            va,
            (uint64)page,
            PGSIZE,
            perm | PTE_U
        );
    }

    return begin;
    
}


// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
// 失败则panic卡死
void uvm_munmap(uint64 begin, uint32 npages)
{
    // 检查释放页数是否合法
    assert(
        npages > 0 &&
        npages <= (MMAP_END - MMAP_BEGIN) / PGSIZE,
        "uvm_munmap: invalid npages"
    );

    // 检查起始地址是否页对齐
    assert(
        begin % PGSIZE == 0,
        "uvm_munmap: begin is not page-aligned"
    );

    // 计算待释放区域的长度
    uint64 len = (uint64)npages * PGSIZE;

    // 检查待释放范围是否合法
    assert(
        begin >= MMAP_BEGIN &&
        begin < MMAP_END &&
        len <= MMAP_END - begin,
        "uvm_munmap: invalid mmap range"
    );

    uint64 end = begin + len;

    // 获取当前进程
    proc_t *p = myproc();

    // 查找包含待释放区域的 mmap 节点
    mmap_region_t *last = NULL;
    mmap_region_t *tmp = p->mmap;

    while (tmp != NULL)
    {
        uint64 tmp_end =
            tmp->begin + (uint64)tmp->npages * PGSIZE;

        // 当前节点完全位于待释放范围之前
        if (tmp_end <= begin)
        {
            last = tmp;
            tmp = tmp->next;
            continue;
        }

        break;
    }

    // 必须找到包含待释放范围的节点
    assert(
        tmp != NULL,
        "uvm_munmap: mmap region not found"
    );

    uint64 tmp_begin = tmp->begin;
    uint64 tmp_end =
        tmp->begin + (uint64)tmp->npages * PGSIZE;

    // 要求整个释放范围都已经分配
    assert(
        begin >= tmp_begin &&
        end <= tmp_end,
        "uvm_munmap: unmapped or cross-region range"
    );

    // 如果释放的是节点中间部分，需要额外申请一个节点
    mmap_region_t *right = NULL;

    if (begin > tmp_begin && end < tmp_end)
    {
        right = mmap_region_alloc();
    }

    // 解除用户页表映射，并释放对应物理页
    vm_unmappages(
        p->pgtbl,
        begin,
        len,
        true
    );

    // 整个节点都被释放
    if (begin == tmp_begin && end == tmp_end)
    {
        // 从进程 mmap 链表中移除节点
        if (last == NULL)
        {
            p->mmap = tmp->next;
        }
        else
        {
            last->next = tmp->next;
        }

        // 将节点归还全局仓库
        mmap_region_free(tmp);
    }
    // 释放节点的前半部分
    else if (begin == tmp_begin)
    {
        // 保留右侧未释放的区域
        tmp->begin = end;
        tmp->npages = (uint32)((tmp_end - end) / PGSIZE);
    }
    // 释放节点的后半部分
    else if (end == tmp_end)
    {
        // 保留左侧未释放的区域
        tmp->npages = (uint32)((begin - tmp_begin) / PGSIZE);
    }
    // 释放节点的中间部分
    else
    {
        // 原节点保留左侧区域
        tmp->npages = (uint32)((begin - tmp_begin) / PGSIZE);

        // 新节点记录右侧区域
        right->begin = end;
        right->npages = (uint32)((tmp_end - end) / PGSIZE);

        // 将右侧节点插入原节点之后
        right->next = tmp->next;
        tmp->next = right;
    }
}

/*------------------part-3: 用户空间heap和stack管理相关------------------*/

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len) 
{
    //先来检查是否越界
    assert(
    cur_heap_top >= USER_BASE + PGSIZE &&
    cur_heap_top <= MMAP_BEGIN &&
    len <= MMAP_BEGIN - cur_heap_top,
    "uvm_heap_grow: heap exceeds MMAP_BEGIN"
    );
    uint64 new_heap_top = cur_heap_top + len;

    //统一使用向上对齐
    uint64 begin =
    (cur_heap_top + PGSIZE - 1) / PGSIZE * PGSIZE;

    uint64 end =
    (new_heap_top + PGSIZE - 1) / PGSIZE * PGSIZE;

    for (uint64 va = begin; va < end; va += PGSIZE)
    {
        // 申请物理页
    
        void *page=pmem_alloc(false);
        // 建立映射
        vm_mappages(pgtbl,
                    va,
                    (uint64)page,
                    PGSIZE,
                    (PTE_R | PTE_W | PTE_U)
                );
    
    }

    return new_heap_top; 

}

// 用户堆空间减少, 返回新的堆顶地址
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
    //先来检查是否越界
    assert(
    cur_heap_top >= USER_BASE + PGSIZE &&
    cur_heap_top <= MMAP_BEGIN &&
    len <= cur_heap_top - (USER_BASE + PGSIZE),
    "uvm_heap_ungrow: invalid heap range"
    );

    uint64 new_heap_top = cur_heap_top - len;

    // 从新堆顶向上对齐，得到第一张需要释放的页
    uint64 begin =
    (new_heap_top + PGSIZE - 1) / PGSIZE * PGSIZE;

    // 从旧堆顶向上对齐，得到释放范围的结束地址
    uint64 end =
    (cur_heap_top + PGSIZE - 1) / PGSIZE * PGSIZE;

    for (uint64 va = begin; va < end; va += PGSIZE)
    {
        
        //解除映射
        vm_unmappages(pgtbl,
                    va,
                    PGSIZE,
                    true
                );
    
    }

    return new_heap_top; 
}

// 处理函数栈增长导致的page fault事件
// 成功返回new_ustack_npage，失败返回-1
uint64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage, uint64 fault_addr)
{
    // 检查当前栈页数是否合法
    if (old_ustack_npage == 0 ||
        old_ustack_npage > (TRAPFRAME - MMAP_END) / PGSIZE)
    {
        return (uint64)-1;
    }
    //计算当前用户栈最低地址
    uint64 old_stack_bottom=USTACK + PGSIZE - PGSIZE * old_ustack_npage;
    //将发生异常的地址向下对齐
    uint64 new_stack_bottom=fault_addr / PGSIZE * PGSIZE;
    //检查是否属于合法的栈增长地址
    if(new_stack_bottom<MMAP_END||new_stack_bottom >= old_stack_bottom){
        return (uint64)-1;
    }
    for(uint64 va=new_stack_bottom;va<old_stack_bottom;va+=PGSIZE){
        void *page=pmem_alloc(false);
        // 建立映射
        vm_mappages(pgtbl,
                    va,
                    (uint64)page,
                    PGSIZE,
                    (PTE_R | PTE_W | PTE_U)
                );
    }
    // 计算本次增加的页数
    uint64 added_pages =
    (old_stack_bottom - new_stack_bottom) / PGSIZE;
    return old_ustack_npage + added_pages;
}

/*----------------------part-4: 用户页表管理相关----------------------*/

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    assert(
        pgtbl != NULL && level >= 1 && level <= 3,
        "destroy_pgtbl: invalid arguments"
    );

    // 遍历当前页表中的所有 PTE
    for (uint32 i = 0; i < PGSIZE / sizeof(pte_t); i++)
    {
        pte_t pte = pgtbl[i];

        // 无效页表项不需要处理
        if ((pte & PTE_V) == 0)
        {
            continue;
        }

        // 从 PTE 中提取物理地址
        uint64 pa = PTE_TO_PA(pte);

        if (PTE_CHECK(pte))
        {
            // 非叶子页表项指向下一级页表
            assert(
                level > 1,
                "destroy_pgtbl: invalid non-leaf PTE"
            );

            // 递归释放下一级页表
            destroy_pgtbl((pgtbl_t)pa, level - 1);
        }
        else
        {
            // 当前 PTE 指向真正的用户数据页
            assert(
                level == 1,
                "destroy_pgtbl: unexpected leaf PTE"
            );

            // 释放用户物理页
            pmem_free(pa, false);
        }

        // 清除当前页表项
        pgtbl[i] = 0;
    }

    // 当前页表中的所有有效 PTE 都已经处理完毕
    // 释放存放当前页表本身的物理页
    pmem_free((uint64)pgtbl, true);
}

// 页表销毁
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    // 获取 Trapframe 对应的物理地址
    pte_t *pte = vm_getpte(pgtbl, TRAPFRAME, false);

    assert(
        pte != NULL && (*pte & PTE_V),
        "uvm_destroy_pgtbl: invalid trapframe PTE"
    );

    uint64 trapframe_pa = PTE_TO_PA(*pte);

    // 解除 Trapframe 映射，但不通过 vm_unmappages 释放物理页
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, false);

    // Trampoline 为所有进程共享，只解除映射
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false);

    // Trapframe 从内核物理页池申请，因此归还给内核物理页池
    pmem_free(trapframe_pa, true);

    // 递归释放用户物理页及各级页表
    destroy_pgtbl(pgtbl, 3);
}

// 连续虚拟空间的复制
// 在uvm_copy_pgtbl中使用
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t *pte;

    for (va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");

        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char *)page, (const char *)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 拷贝页表 (拷贝并不包括 trapframe 和 trampoline)
// 拷贝的页表管理的物理页是原来页表的复制品
void uvm_copy_pgtbl(
    pgtbl_t old,
    pgtbl_t new,
    uint64 heap_top,
    uint64 ustack_npage,
    mmap_region_t *mmap
)
{
    // 检查页表指针是否合法
    assert(
        old != NULL && new != NULL,
        "uvm_copy_pgtbl: invalid page table"
    );

    // 复制用户代码及初始数据区域
    // 用户程序最初占用 USER_BASE 开始的一页
    copy_range(
        old,
        new,
        USER_BASE,
        USER_BASE + PGSIZE
    );

    // 检查堆顶是否合法
    assert(
        heap_top >= USER_BASE + PGSIZE &&
        heap_top <= MMAP_BEGIN,
        "uvm_copy_pgtbl: invalid heap_top"
    );

    // 计算堆区域的起始地址
    uint64 heap_begin = USER_BASE + PGSIZE;

    // 将堆顶向上对齐到页面边界
    uint64 heap_end =
        (heap_top + PGSIZE - 1) / PGSIZE * PGSIZE;

    // 只有堆实际占用物理页时才复制
    if (heap_end > heap_begin)
    {
        copy_range(
            old,
            new,
            heap_begin,
            heap_end
        );
    }

    // 遍历进程 mmap 链表
    mmap_region_t *tmp = mmap;

    while (tmp != NULL)
    {
        uint64 begin = tmp->begin;
        uint64 len = (uint64)tmp->npages * PGSIZE;

        // 检查 mmap 区域是否合法
        assert(
            tmp->npages > 0 &&
            begin % PGSIZE == 0 &&
            begin >= MMAP_BEGIN &&
            begin < MMAP_END &&
            len <= MMAP_END - begin,
            "uvm_copy_pgtbl: invalid mmap region"
        );

        // 复制当前 mmap 节点管理的所有页面
        copy_range(
            old,
            new,
            begin,
            begin + len
        );

        // 处理下一个 mmap 节点
        tmp = tmp->next;
    }

    // 用户栈至少需要有一页
    // 同时不能向下越过 MMAP_END
    uint64 stack_top = USTACK + PGSIZE;

    assert(
        ustack_npage > 0 &&
        ustack_npage <= (stack_top - MMAP_END) / PGSIZE,
        "uvm_copy_pgtbl: invalid ustack_npage"
    );

    // 根据已分配的页面数量计算栈底
    uint64 stack_bottom =
        stack_top - ustack_npage * PGSIZE;

    // 复制整个已分配的用户栈
    copy_range(
        old,
        new,
        stack_bottom,
        stack_top
    );
}
