#include "rx-gdb-stub.h"
#include "intrinsics.h"

__attribute__((section(".ramfunc.foo")))
void foo (void)
{
    for (int i = 100000; i; --i)
    {
        __no_operation();
    }
}

__attribute__((section(".ramfunc.main")))
void main (void)
{
    volatile unsigned int lval1 = 0;
    volatile unsigned int lval2 = -1;
    __enable_interrupt();
    debug_puts("Hello, buggy world!");
    for (;;)
    {
        ++lval1;
        --lval2;
        foo();
        __no_operation();
    }
}
