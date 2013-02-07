#ifndef RX_GDB_STUB_H__
#define RX_GDB_STUB_H__

void debug_puts (const char *str);

#define BREAKPOINT() asm volatile ("brk")
#define NOP()        asm volatile ("nop")

#endif /* RX_GDB_STUB_H__ */
