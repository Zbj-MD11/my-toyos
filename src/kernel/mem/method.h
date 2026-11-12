#pragma once

/* pmem.c: 物理内存管理逻辑 */

void pmem_init(void);
void *pmem_alloc(bool in_kernel);
void pmem_free(uint64 page, bool in_kernel);

/* kvm.c: 内核态虚拟内存管理 + 页表通用函数 */

pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc);
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm);
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit);
void vm_print(pgtbl_t pgtbl);
void kvm_init();
void kvm_inithart();


/* uvm.c: 用户态虚拟内存管理 */

void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len);
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len);
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen);
void uvm_show_mmaplist(mmap_region_t *mmap);
void uvm_mmap(uint64 begin, uint32 npages, int perm);
void uvm_munmap(uint64 begin, uint32 npages);
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len);
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len);
uint64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage, uint64 fault_addr);
void uvm_destroy_pgtbl(pgtbl_t pgtbl);
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint64 ustack_npage, mmap_region_t *mmap);

/* mmap.c: mmap_node仓库管理 */

void mmap_init();
mmap_region_t *mmap_region_alloc();
void mmap_region_free(mmap_region_t *mmap);
void mmap_show_nodelist();





// 物理内存测试
void test_case_1(void);
void test_case_2(void);
// 测试物理页地址对齐
void test_pmem_alignment(void);

// 测试物理页分配的唯一性
void test_pmem_unique(void);

// 测试非法释放（kind = 1、2、3）
void test_pmem_invalid_free(int kind);

// 测试重复释放
void test_pmem_double_free(void);

// 测试双 hart 并发分配和释放
void test_pmem_concurrent(int hartid);

//虚拟内存测试
void test_mapping_and_unmapping(void);