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
    

}uvm_copyin

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
            copy_Byuvm_copyinte_Sum = len;
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
static uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len, mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap)
{

}

// 在用户页表和进程mmap链里新增mmap区域 [begin, begin + npages * PGSIZE)
// 调用者保证begin是page-aligned的, 页面权限为perm
// 注意: 如果start==0, 意味着需要内核自主找一块足够大的空间
// 失败则panic卡死
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{

}


// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
// 失败则panic卡死
void uvm_munmap(uint64 begin, uint32 npages)
{

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

}

// 页表销毁
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, true);   // 可以释放，因为trapframe是每个进程独有的
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false); // 不能释放，因为所有进程共用区域
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
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint64 ustack_npage, mmap_region_t *mmap)
{

}
