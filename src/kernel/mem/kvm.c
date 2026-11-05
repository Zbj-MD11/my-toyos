#include "mod.h"

// 内核页表
static pgtbl_t kernel_pgtbl;

// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN + PTE_TO_PA + PA_TO_PTE
pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{

    if (va >= VA_MAX ) {
    assert(false, "vm_getpte:Va out of range");
    }

    pte_t *pte=NULL;
    pte=&pgtbl[VA_TO_VPN(va, 2)];
    if(!(*pte & PTE_V)){
        if(alloc){

            *pte=(PA_TO_PTE(pmem_alloc(true))) | PTE_V;

        }else{
            return NULL;
        }
    }else{
        if(PTE_CHECK(*pte)){

        }else{
            assert(false,"vm_getpte:This PTE is not a pgtbl");
        }
    }
    pgtbl=(pgtbl_t)PTE_TO_PA(*pte);
    
    pte=&pgtbl[VA_TO_VPN(va,1)];
    if(!(*pte & PTE_V)){
        if(alloc){
            
            *pte=(PA_TO_PTE(pmem_alloc(true))) | PTE_V;

        }else{

            return NULL;

        }

    }else{
     
        assert(PTE_CHECK(*pte),"vm_getpte:This PTE is not a pgtbl");
        
    }
    pgtbl=(pgtbl_t)PTE_TO_PA(*pte);
    
    pte=&pgtbl[VA_TO_VPN(va, 0)];
    

    return pte;

    
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    if(va%PGSIZE!=0||pa%PGSIZE!=0){
        assert(false,"vm_mappages:Va or Pa is not page-aligned");
    }
    if(len==0){
        assert(false,"vm_mappages:Invalid len");
    }
    //注意，如果直接用VA+len,可能导致溢出
    if (va >= VA_MAX || len > VA_MAX - va) {
    assert(false, "vm_mappages:Va out of range");
    }

    //所需要的页数向上取整
    int pages =(len-1)/PGSIZE + 1;

    while(pages--){

        pte_t *pte=vm_getpte(pgtbl,va,true);
        if(pte==NULL){
            assert(false,"vm_mappages:leaf pte dosen't exist");
        }
        if((*pte & PTE_V)==1){
            assert(false,"vm_mappages:Try to change existing mapping");
        }
        *pte=(pte_t)PA_TO_PTE(pa);
        *pte|=(pte_t)PTE_V;
        *pte |=(pte_t)perm;

        pa+=PGSIZE;
        va+=PGSIZE;

    }

}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    if(va%PGSIZE!=0){
        assert(false,"vm_unmappages:Va is not page-aligned");
    }
    if(len==0){
        assert(false,"vm_unmappages:Invalid len");
    }
    //注意，如果直接用VA+len,可能导致溢出
    if (va >= VA_MAX || len > VA_MAX - va) {
    assert(false, "vm_unmappages:Va out of range");
    }

    int pages =(len-1)/PGSIZE + 1;

    while(pages--){

        pte_t *pte=vm_getpte(pgtbl,va,false);
        if(pte==NULL){
            assert(false,"vm_unmappages:leaf pte dosen't exist");
        }
        if((*pte & PTE_V)==0){
            assert(false,"vm_unmappages:leaf pte dosen't valid");
        }
        if(PTE_CHECK(*pte)){
            assert(false,"vm_unmappages:Pte isn't a leaf");
        }
        if(freeit){
            pmem_free((uint64)PTE_TO_PA(*pte),false);
        }
        *pte &=0;
        
        

        va+=PGSIZE;

    }


}

// 完成UART、CLINT、PLIC、内核代码区、内核数据区、可分配区域的页表映射
// 相当于部分填充kernel_pgtbl
void kvm_init()
{
    kernel_pgtbl=(pgtbl_t)pmem_alloc(true);

    //为UART建立恒等映射
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);
    
    //为CLINT 建立恒等映射
    vm_mappages(kernel_pgtbl, CLINT_BASE, CLINT_BASE
                , CLINT_MTIME-CLINT_BASE+8, PTE_R | PTE_W);
    
    
    //为PLIC建立恒等映射
    
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE
                , PLIC_SCLAIM(NCPU-1)-PLIC_BASE+4, PTE_R | PTE_W);

    //内核部分
    vm_mappages(kernel_pgtbl, (uint64)KERNEL_BASE, (uint64)KERNEL_BASE,(uint64)KERNEL_DATA-(uint64)KERNEL_BASE , PTE_R | PTE_X);

    vm_mappages(kernel_pgtbl, (uint64)KERNEL_DATA, (uint64)KERNEL_DATA,(uint64)ALLOC_BEGIN-(uint64)KERNEL_DATA , PTE_R | PTE_W);

    vm_mappages(kernel_pgtbl, (uint64)ALLOC_BEGIN, (uint64)ALLOC_BEGIN,(uint64)ALLOC_END-(uint64)ALLOC_BEGIN , PTE_R | PTE_W );

    






}

// 每个CPU都需要调用, 从不使用页表切换到使用内核页表
// 切换后需要刷新TLB里面的缓存
void kvm_inithart()
{
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

// 输出页表内容(for debug)
void vm_print(pgtbl_t pgtbl)
{
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for (int i = 0; i < PGSIZE / sizeof(pte_t); i++)
    {
        pte = pgtbl_2[i];
        if (!((pte)&PTE_V))
            continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);

        for (int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if (!((pte)&PTE_V))
                continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_0);

            for (int k = 0; k < PGSIZE / sizeof(pte_t); k++)
            {
                pte = pgtbl_0[k];
                if (!((pte)&PTE_V))
                    continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));
            }
        }
    }
}





/*---------------------------------- 测试代码 --------------------------------*/

void test_mapping_and_unmapping()
{
    // 1. 初始化测试页表
    pte_t* pte;
    pgtbl_t pgtbl = (pgtbl_t)pmem_alloc(true);
    memset(pgtbl, 0, PGSIZE);

    // 2. 准备测试条件
    uint64 va_1 = 0x100000;
    uint64 va_2 = 0x8000;
    uint64 pa_1 = (uint64)pmem_alloc(false);
    uint64 pa_2 = (uint64)pmem_alloc(false);

    // 3. 建立映射
    vm_mappages(pgtbl, va_1, pa_1, PGSIZE, PTE_R | PTE_W);
    vm_mappages(pgtbl, va_2, pa_2, PGSIZE, PTE_R);

    // 4. 验证映射结果
    pte = vm_getpte(pgtbl, va_1, false);
    assert(pte != NULL, "test_mapping_and_unmapping: pte_1 not found");
    assert((*pte & PTE_V) != 0, "test_mapping_and_unmapping: pte_1 not valid");
    assert(PTE_TO_PA(*pte) == pa_1, "test_mapping_and_unmapping: pa_1 mismatch");
    assert((*pte & (PTE_R | PTE_W)) == (PTE_R | PTE_W), "test_mapping_and_unmapping: flag_1 mismatch");

    pte = vm_getpte(pgtbl, va_2, false);
    assert(pte != NULL, "test_mapping_and_unmapping: pte_2 not found");
    assert((*pte & PTE_V) != 0, "test_mapping_and_unmapping: pte_2 not valid");
    assert(PTE_TO_PA(*pte) == pa_2, "test_mapping_and_unmapping: pa_2 mismatch");
    assert(((*pte & PTE_R) == PTE_R), "test_mapping_and_unmapping: flag_2 mismatch");

    // 5. 解除映射
    vm_unmappages(pgtbl, va_1, PGSIZE, true);
    vm_unmappages(pgtbl, va_2, PGSIZE, true);

    // 6. 验证解除映射结果
    pte = vm_getpte(pgtbl, va_1, false);
    assert(pte != NULL, "test_mapping_and_unmapping: pte_1 not found");
    assert((*pte & PTE_V) == 0, "test_mapping_and_unmapping: pte_1 still valid");
    pte = vm_getpte(pgtbl, va_2, false);
    assert(pte != NULL, "test_mapping_and_unmapping: pte_2 not found");
    assert((*pte & PTE_V) == 0, "test_mapping_and_unmapping: pte_2 still valid");

    // 7. 由于页表的释放函数还没实现, 作为测试用例可以展示不释放页表空间

    printf("test_mapping_and_unmapping passed!\n");
}
