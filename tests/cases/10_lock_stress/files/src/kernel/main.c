#include "arch/mod.h"
#include "lib/mod.h"

volatile static int started = 0;
volatile static int sum = 0;
static spinlock_t lk;

void main()
{
    int cpuid = r_tp();

    if (cpuid == 0) {
        print_init();
        spinlock_init(&lk, "stress");
        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0);
        __sync_synchronize();
    }

    for (int i = 0; i < 200000; i++) {
        spinlock_acquire(&lk);
        sum++;
        spinlock_release(&lk);
    }

    spinlock_acquire(&lk);
    int v = sum;
    spinlock_release(&lk);

    printf("stress: cpu %d sum = %d\n", cpuid, v);
    while (1);
}
