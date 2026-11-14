#include "arch/mod.h"
#include "lib/mod.h"

volatile static int started = 0;

void main()
{
    int cpuid = r_tp();

    if (cpuid == 0) {
        print_init();
        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0);
        __sync_synchronize();
    }

    for (int i = 0; i < 300; i++) {
        if (cpuid == 0)
            printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
        else
            printf("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n");
    }
    while (1);
}
