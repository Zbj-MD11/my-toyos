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