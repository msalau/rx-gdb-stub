#ifndef INTRINSICS_H__
#define INTRINSICS_H__

#define __enable_interrupt()  __asm__ __volatile__ ("setpsw I")
#define __disable_interrupt() __asm__ __volatile__ ("clrpsw I")
#define __no_operation()      __asm__ __volatile__ ("nop")
#define __breakpoint()        __asm__ __volatile__ ("brk")

#endif  /* INTRINSICS_H__ */
