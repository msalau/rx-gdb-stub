#include "rx-gdb-stub.h"

__attribute__((section(".ramfunc.foo")))
void foo (void)
{
    for (int i = 100000; i; --i)
    {
        NOP();
    }
}

__attribute__((section(".ramfunc.main")))
void main (void)
{
    volatile unsigned int lval1 = 0;
    volatile unsigned int lval2 = -1;
    asm volatile ("setpsw I");
    debug_puts("Hello, buggy world!");
    for (;;)
    {
        ++lval1;
        --lval2;
        foo();
        NOP();
    }
}
