
#include "arch/mod.h"
#include "lib/mod.h"

// 两个 CPU 共享的状态变量
    volatile static int started = 0;

    volatile static int sum = 0;

    static spinlock_t sum_lk;

    void main()
    {
        int cpuid = r_tp();
        if(cpuid == 0) {
            print_init();
            spinlock_init(&sum_lk,"yj");
            printf("cpu %d is booting!\n", cpuid);        
            __sync_synchronize();
            started = 1;
            spinlock_acquire(&sum_lk);
            for(int i = 0; i < 1000000; i++){
                

                sum++;
                
            }
            spinlock_release(&sum_lk);
            spinlock_acquire(&sum_lk);

            int result = sum;

            spinlock_release(&sum_lk);

            printf("cpu %d report: sum = %d\n", cpuid, result);
        } else {
            while(started == 0);
            __sync_synchronize();
            printf("cpu %d is booting!\n", cpuid);
            spinlock_acquire(&sum_lk);
            for(int i = 0; i < 1000000; i++){
                
                sum++;
                
            }
            spinlock_release(&sum_lk);
            spinlock_acquire(&sum_lk);

            int result = sum;

            spinlock_release(&sum_lk);

            printf("cpu %d report: sum = %d\n", cpuid, result);
        }   
        while (1);    
    }  