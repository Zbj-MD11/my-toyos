zho#include "../arch/mod.h"
#include "../lib/mod.h"
#include "../mem/mod.h"
#include "../trap/mod.h"
#include "../proc/mod.h"
extern char trampoline[];
extern void swtch(context_t *, context_t *);
extern int lab4_context_roundtrip(context_t *, context_t *);
static context_t test_old, test_new;
static uint8 test_stack[PGSIZE] __attribute__((aligned(16)));
static void context_target(void) {
    swtch(&test_new, &test_old);
    panic("TEST: context resumed unexpectedly");
}
void lab4_context_test(void) {
    test_new.sp = (uint64)test_stack + PGSIZE;
    test_new.ra = (uint64)context_target;
    assert(lab4_context_roundtrip(&test_old, &test_new) == 0, "TEST: swtch registers");
    printf("[probe] swtch ra, sp, s0-s11 roundtrip OK\n");
}
static uint64 calls, timers, externals, initial_ticks;
static uint64 saved_pc;
static int saved_kind;
static void mapping(pgtbl_t pt, uint64 va, int flags) {
    pte_t *p = vm_getpte(pt, va, false);
    assert(p && (*p & PTE_V), "TEST: missing mapping");
    assert((*p & (PTE_R|PTE_W|PTE_X|PTE_U)) == flags, "TEST: mapping permission");
}
void lab4_created(void) {
    proc_t *p = myproc();
    assert(p && p->pid == 0 && mycpuid() == 0, "TEST: process identity");
    assert(p->tf && p->pgtbl && p->ustack_npage == 1, "TEST: process resources");
    assert(p->heap_top == USER_BASE+PGSIZE, "TEST: heap top");
    assert(p->tf->sp == USTACK+PGSIZE && p->tf->user_to_kern_epc == USER_BASE, "TEST: initial context");
    assert(p->ctx.sp == KSTACK(0)+PGSIZE, "TEST: kernel stack");
    mapping(p->pgtbl, USER_BASE, PTE_R|PTE_X|PTE_U);
    mapping(p->pgtbl, USTACK, PTE_R|PTE_W|PTE_U);
    mapping(p->pgtbl, TRAMPOLINE, PTE_R|PTE_X);
    mapping(p->pgtbl, TRAPFRAME, PTE_R|PTE_W);
    pte_t *z = vm_getpte(p->pgtbl, 0, false);
    assert(!z || !(*z & PTE_V), "TEST: zero page mapped");
    assert(PTE_TO_PA(*vm_getpte(p->pgtbl, TRAMPOLINE, false)) == (uint64)trampoline, "TEST: trampoline PA");
    assert(PTE_TO_PA(*vm_getpte(p->pgtbl, TRAPFRAME, false)) == (uint64)p->tf, "TEST: trapframe PA");
    uint64 satp = r_satp();
    pgtbl_t kernel = (pgtbl_t)((satp & ((1ULL<<44)-1))<<12);
    mapping(kernel, TRAMPOLINE, PTE_R|PTE_X);
    mapping(kernel, KSTACK(0), PTE_R|PTE_W);
    assert(PTE_TO_PA(*vm_getpte(kernel, TRAMPOLINE, false)) == (uint64)trampoline, "TEST: shared trampoline");
    initial_ticks = timer_get_ticks();
    printf("[probe] process, initial context and mappings OK\n");
}
void lab4_trap(void) {
    proc_t *p = myproc();
    uint64 sp; asm volatile("mv %0, sp" : "=r"(sp));
    assert(sp >= KSTACK(0) && sp < KSTACK(0)+PGSIZE, "TEST: active kernel stack");
    assert((r_sstatus() & SSTATUS_SPP) == 0, "TEST: U-mode origin");
    assert(r_satp() == p->tf->user_to_kern_satp, "TEST: kernel satp");
    saved_pc = r_sepc();
    saved_kind = r_scause() == 8 ? 1 : 0;
    if (r_scause() == 8) {
        calls++;
        if (calls == 1) printf("[probe] U-mode syscall entry OK\n");
    } else if (r_scause() == (1ULL<<63|1)) {
        timers++;
    } else if (r_scause() == (1ULL<<63|9)) {
        externals++;
    }
}
void lab4_return(void) {
    proc_t *p = myproc();
    assert(p->tf->user_to_kern_epc == saved_pc + (saved_kind ? 4 : 0), "TEST: return PC");
    if (saved_kind) {
        assert(p->tf->a0 == 0, "TEST: syscall return value");
        if (calls == 1) printf("[probe] syscall return a0=0, PC+4 OK\n");
        if (calls == EXPECT_CALLS) printf("[probe] expected syscall count reached\n");
    }
    if (timers == 3 && timer_get_ticks() >= initial_ticks+3) {
        printf("[probe] user timer, ticks and unchanged PC OK\n");
        timers++; /* bounded logging */
    }
    if (externals && !saved_kind) {
        static int printed;
        if (!printed) { printf("[probe] user external interrupt and unchanged PC OK\n"); printed=1; }
    }
}
