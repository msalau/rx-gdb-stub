#include "rx-gdb-stub.h"
#include "intrinsics.h"

__attribute__((section(".ramfunc.foo"),noinline))
static void foo (void)
{
    int i;
    for (i = 100000; i > 0; --i)
    {
        __no_operation();
    }
}

__attribute__((section(".ramfunc.main")))
int main (void)
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
