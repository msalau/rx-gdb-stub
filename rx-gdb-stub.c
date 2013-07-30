/***********************************************************************
 * GDB stub for bare-metal Renesas RX target             .             *
 *                                                                     *
 * Created by Maxim Salov                                              *
 *                                                                     *
 * This source code is offered for use in the public domain. You may   *
 * use, modify or distribute it freely.                                *
 *                                                                     *
 * This code is distributed in the hope that it will be useful but     *
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY *
 * DISCLAIMED. This includes but is not limited to warranties of       *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                *
 ***********************************************************************/

#include "rx-gdb-stub.h"
#include <iodefine.h>
#include "isr_vectors.h"
#include <string.h>
#include <limits.h>
#include <stdint.h>

static void stub_rsp_handler (unsigned int signal);
static char stub_getchar (void);
static void stub_putchar (char c);
/*@unused@*/
static void stub_puts (const char *str);

#define SIGBREAK '\x03'

#define TARGET_SIGNAL_INT  2
#define TARGET_SIGNAL_TRAP 5

enum regnames
{
    R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15,
    USP, ISP, PSW, PC, INTB, BPSW, BPC, FINTV, FPSW, ACC,
    NUM_REGS
};

/* Add some space to hold ACC high word */
static unsigned int registers[NUM_REGS + 1];

#define PSW_C_BIT (1U << 0)
#define PSW_Z_BIT (1U << 1)
#define PSW_S_BIT (1U << 2)
#define PSW_O_BIT (1U << 3)

#define PSW_C ((unsigned int)(0 != (registers[PSW] & PSW_C_BIT)))
#define PSW_Z ((unsigned int)(0 != (registers[PSW] & PSW_Z_BIT)))
#define PSW_S ((unsigned int)(0 != (registers[PSW] & PSW_S_BIT)))
#define PSW_O ((unsigned int)(0 != (registers[PSW] & PSW_O_BIT)))

/* Stack pointer provided by linker script.
   It is used to get end of RAM area */
/*@external@*/
extern unsigned int _stack;

#define RAM_END ((void*)&_stack)

#define OPCODE_NOP 0x03
#define OPCODE_BRK 0x00

static const char hexchars[] = "0123456789abcdef";

#define BUFFER_SIZE 512

static char trx_buffer[BUFFER_SIZE + 1];

static unsigned char   stepping = 0;
/*@null@*/
static unsigned char * stepping_brk_address = NULL;
static unsigned char   stepping_brk_opcode = OPCODE_BRK;

__attribute__((naked))
static void save_context (void)
{
    __asm__ __volatile__ (
        "push  r15 \n"
        "mov.l %0, r15 \n"
        ";; Skip R0 \n"
        "add   %1, r15 \n"
        ";; Save registers R1-R14 \n"
        "mov.l r1, [r15+] \n"
        "mov.l r2, [r15+] \n"
        "mov.l r3, [r15+] \n"
        "mov.l r4, [r15+] \n"
        "mov.l r5, [r15+] \n"
        "mov.l r6, [r15+] \n"
        "mov.l r7, [r15+] \n"
        "mov.l r8, [r15+] \n"
        "mov.l r9, [r15+] \n"
        "mov.l r10, [r15+] \n"
        "mov.l r11, [r15+] \n"
        "mov.l r12, [r15+] \n"
        "mov.l r13, [r15+] \n"
        "mov.l r14, [r15+] \n"
        ";; Save R15 \n"
        "pop   r1 \n"
        "mov.l r1, [r15+] \n"
        ";; Pop return address \n"
        "pop    r14 \n"
        ";; Pop PC (r2) & PSW (r3) \n"
        "popm   r2-r3 \n"
        ";; Save USP \n"
        "mvfc  usp, r4 \n"
        "mov.l r4, [r15+] \n"
        ";; Save ISP \n"
        "mvfc  isp, r5 \n"
        "mov.l r5, [r15+] \n"
        ";; Save PSW \n"
        "mov.l r3, [r15+] \n"
        ";; Save PC \n"
        "mov.l r2, [r15+] \n"
        ";; Save INTB \n"
        "mvfc  intb, r1 \n"
        "mov.l r1, [r15+] \n"
        ";; Save BPSW \n"
        "mvfc  bpsw, r1 \n"
        "mov.l r1, [r15+] \n"
        ";; Save BPC \n"
        "mvfc  bpc, r1 \n"
        "mov.l r1, [r15+] \n"
        ";; Save FINTV \n"
        "mvfc  fintv, r1 \n"
        "mov.l r1, [r15+] \n"
        ";; Save FPSW \n"
        "mvfc  fpsw, r1 \n"
        "mov.l r1, [r15+] \n"
        ";; Save ACC low word \n"
        "mvfacmi  r1 \n"
        "shll  #16, r1 \n"
        "mov.l r1, [r15+] \n"
        ";; Save ACC high word \n"
        "mvfachi  r1 \n"
        "mov.l r1, [r15+] \n"
        ";; Set R0 according to U bit value \n"
        "mov.l %0, r15 \n"
        ";; Test U bit in PSW \n"
        "btst  #17, r3 \n"
        "bnz   1f \n"
        "mov.l r5, 0[r15] \n"
        "bra   2f \n"
        "1: \n"
        "mov.l r4, 0[r15] \n"
        "2: \n"
        ";; Return from function \n"
        "jmp   r14 \n"
        :: "i" (&registers), "i" (sizeof registers[0]));
}

__attribute__((naked))
static void restore_context_and_exit (void)
{
    __asm__ __volatile__ (
        ";; Remove return address from stack \n"
        "pop   r15 \n"
        ";; Prepare to restore context \n"
        "mov.l %0, r15 \n"
        "mov.l [r15], r4 \n"
        "add   %1, r15 \n"
        ";; Restore ACC high word \n"
        "mov.l [-r15], r1 \n"
        "mvtachi r1\n"
        ";; Restore ACC high low \n"
        "mov.l [-r15], r1 \n"
        "mvtaclo r1\n"
        ";; Restore FPSW \n"
        "mov.l [-r15], r1 \n"
        "mvtc  r1, fpsw \n"
        ";; Restore FINTV \n"
        "mov.l [-r15], r1 \n"
        "mvtc  r1, fintv \n"
        ";; Restore BPC \n"
        "mov.l [-r15], r1 \n"
        "mvtc  r1, bpc \n"
        ";; Restore BPSW \n"
        "mov.l [-r15], r1 \n"
        "mvtc  r1, bpsw \n"
        ";; Restore INTB \n"
        "mov.l [-r15], r1 \n"
        "mvtc  r1, intb \n"
        ";; Restore PC \n"
        "mov.l [-r15], r2 \n"
        ";; Restore PSW \n"
        "mov.l [-r15], r3 \n"
        ";; Read ISP \n"
        "mov.l [-r15], r5 \n"
        ";; Read USP \n"
        "mov.l [-r15], r6 \n"
        ";; Set ISP/USP equal to R0 \n"
        ";; Test U bit in PSW \n"
        "btst  #17, r3 \n"
        "bnz   1f \n"
        "mov.l r4, r5 \n"
        "bra   2f \n"
        "1: \n"
        "mov.l r4, r6 \n"
        "2: \n"
        ";; Restore ISP & USP \n"
        "mvtc  r5, isp \n"
        "mvtc  r6, usp \n"
        ";; Push PC (r2) & PSW (r3) \n"
        "pushm r2-r3 \n"
        ";; Read r15 and save it on stack \n"
        "mov.l [-r15], r1 \n"
        "push  r1 \n"
        ";; Restore registers r14-r1 \n"
        "mov.l [-r15], r14 \n"
        "mov.l [-r15], r13 \n"
        "mov.l [-r15], r12 \n"
        "mov.l [-r15], r11 \n"
        "mov.l [-r15], r10 \n"
        "mov.l [-r15], r9 \n"
        "mov.l [-r15], r8 \n"
        "mov.l [-r15], r7 \n"
        "mov.l [-r15], r6 \n"
        "mov.l [-r15], r5 \n"
        "mov.l [-r15], r4 \n"
        "mov.l [-r15], r3 \n"
        "mov.l [-r15], r2 \n"
        "mov.l [-r15], r1 \n"
        ";; Restore r15 from stack \n"
        "pop   r15 \n"
        ";; Return from exception \n"
        "rte \n"
        :: "i" (&registers), "i" (sizeof registers));
}

static unsigned int get_next_pc (void)
/*@modifies nothing@*/
/*@globals registers@*/
{
    const unsigned char * const pc = (unsigned char*)registers[PC];
    const unsigned char * next_pc = pc;
    unsigned int opcode = *pc;
    /* Parse first byte of instruction */
    if (0x02 == opcode)                                /* 00000010  1   RTS  */
    {
        unsigned int *sp = (unsigned int*)registers[R0];
        next_pc = (unsigned char*)*sp;
    }
    else if (0x03 == opcode)                           /* 00000011  1   NOP */
    {
        next_pc = pc + 1;
    }
    else if (0x04 == opcode ||                         /* 00000100  4   BRA 4 */
             0x05 == opcode)                           /* 00000101  4   BSR 2 */
    {
        unsigned int dsp = ((unsigned int)pc[1] << 0) |
            ((unsigned int)pc[2] << 8) |
            ((unsigned int)pc[3] << 16);
        if (0 != (dsp & 0x00800000U))   /* If displacement is negative - extend the sign */
        {
            dsp |= 0xFF000000U;
        }
        next_pc = pc + (signed int)dsp;
    }
    else if (0x06 == opcode)                           /* 00000110 some kind of memory extended instruction
                                                          00000110 xx0000xx 3+  SUB 2x
                                                          00000110 xx0001xx 3+  CMP 4x
                                                          00000110 xx0010xx 3+  ADD 2x
                                                          00000110 xx0011xx 3+  MUL 3x
                                                          00000110 xx0100xx 3+  AND 3x
                                                          00000110 xx0101xx 3+  OR 3x
                                                          00000110 101000xx 4+  SBB 2
                                                          00000110 xx1000xx 4+  ADC 3
                                                          00000110 xx1000xx 4+  DIV 2x
                                                          00000110 xx1000xx 4+  DIVU 2x
                                                          00000110 xx1000xx 4+  EMUL 2x
                                                          00000110 xx1000xx 4+  EMULU 2x
                                                          00000110 xx1000xx 4+  ITOF 1x
                                                          00000110 xx1000xx 4+  MAX 2x
                                                          00000110 xx1000xx 4+  MIN 2x
                                                          00000110 xx1000xx 4+  TST 2x
                                                          00000110 xx1000xx 4+  XCHG 1x
                                                          00000110 xx1000xx 4+  XOR 2x
                                                        */
    {
        unsigned int byte1 = pc[1];
        unsigned int ld = byte1 & 0x03U;
        if (3U == ld)
        {
            ld = 0;
        }
        if (0 != (byte1 & 0x20U))
        {
            ++ld;
        }
        next_pc = pc + 3 + ld;
    }
    else if ((0xF8 & opcode) == 0x08)                  /* 00001xxx  1   BRA 1 */
    {
        unsigned int dsp = opcode & 0x07;
        if (dsp < 3)
        {
            dsp += 8;
        }
        next_pc = pc + dsp;
    }
    else if ((0xF0 & opcode) == 0x10)                  /* 0001xxxx  1   BCnd 1 */
    {
        /* Condition:
           0: BEQ, BZ
           1: BNE, BNZ */
        unsigned int cnd = ((opcode & 0x80) != 0);
        unsigned int dsp = 1;
        if (cnd != PSW_Z)
        {
            dsp = opcode & 0x03;
            if (dsp < 3)
            {
                dsp += 8;
            }
        }
        next_pc = pc + dsp;
    }
    else if (0x2E == opcode)                           /* 00101110  2   BRA 2 */
    {
        int dsp = (int)(signed char)pc[1];
        next_pc = pc + dsp;
    }
    else if ((0xF0 & opcode) == 0x20)                  /* 0010xxxx  2   BCnd 2 */
    {
        unsigned int branch = 0;
        unsigned int cnd = opcode & 0x0F;
        switch (cnd)
        {
        case 0x00:                 /* BEQ, BZ */
        case 0x01:                 /* BNE, BNZ */
        {
            if ((0 == cnd) == PSW_Z)
            {
                branch = 1;
            }
            break;
        }
        case 0x02:                 /* BGEU, BC */
        case 0x03:                 /* BLTU, BNC */
        {
            if ((0x02 == cnd) == PSW_C)
            {
                branch = 1;
            }
            break;
        }
        case 0x04:                 /* BGTU */
        case 0x05:                 /* BLEU */
        {
            unsigned int gtu = PSW_C && !PSW_Z;
            if ((0x04 == cnd) == gtu)
            {
                branch = 1;
            }
            break;
        }
        case 0x06:                 /* BPZ */
        case 0x07:                 /* BN */
        {
            if ((0x07 == cnd) == PSW_S)
            {
                branch = 1;
            }
            break;
        }
        case 0x08:                 /* BGE */
        case 0x09:                 /* BLT */
        {
            unsigned int lt = PSW_S ^ PSW_O;
            if ((0x09 == cnd) == lt)
            {
                branch = 1;
            }
            break;
        }
        case 0x0A:                /* BGT */
        case 0x0B:                /* BLE */
        {
            unsigned int le = (PSW_S ^ PSW_O) || PSW_Z;
            if ((0x0B == cnd) == le)
            {
                branch = 1;
            }
            break;
        }
        case 0x0C:                /* BO */
        case 0x0D:                /* BNO */
        {
            if ((0x0C == cnd) == PSW_O)
            {
                branch = 1;
            }
            break;
        }
        case 0x0E:                /* BRA 2 (00101110) */
        case 0x0F:                /* Reserved */
        default:
            /* Wrong opcode */
            break;
        }
        {
            int dsp = 2;
            if (branch)
            {
                dsp = (int)(signed char)pc[1];
            }
            next_pc = pc + dsp;
        }
    }
    else if (0x38 == opcode ||                         /* 00111000  3   BRA 3 */
             0x39 == opcode)                           /* 00111001  3   BSR 1 */
    {
        int dsp = (int)(short int)(((unsigned int)pc[1] << 0) |
                                   ((unsigned int)pc[2] << 8));
        next_pc = pc + dsp;
    }
    else if ((0xFE & opcode) == 0x3A)                  /* 0011101x  3   BCnd 3 */
    {
        /* Condition:
           0: BEQ, BZ
           1: BNE, BNZ */
        unsigned int cnd = ((opcode & 0x01) != 0);
        signed int dsp = 3;
        if (cnd != PSW_Z)
        {
            dsp = (int)(short int)(((unsigned int)pc[1] << 0) |
                                   ((unsigned int)pc[2] << 8));
        }
        next_pc = pc + dsp;
    }
    else if (0x3F == opcode)                           /* 00111111  3   RTSD 2 */
    {
        unsigned int *sp = (unsigned int*)registers[R0];
        unsigned int offset = pc[2];
        next_pc = (unsigned char*)sp[offset];
    }
    else if ((0xFC & opcode) == 0x3C)                  /* 001111xx  3   MOV 4 */
    {
        next_pc = pc + 3;
    }
    else if ((0xE0 & opcode) == 0x40)                  /* 010xxxxx group of commands:
                                                          010000xx  2+  SUB 2
                                                          010001xx  2+  CMP 4
                                                          010010xx  2+  ADD 2
                                                          010011xx  2+  MUL 3
                                                          010100xx  2+  AND 3
                                                          010101xx  2+  OR 3
                                                          01011xxx  2+  MOVU 2
                                                       */
    {
        unsigned int ld = opcode & 0x03;
        if (0x03 == ld)
        {
            ld = 0;
        }
        next_pc = pc + 2 + ld;
    }
    else if (0x67 == opcode)                           /* 01100111  2   RTSD 1 */
    {
        unsigned int *sp = (unsigned int*)registers[R0];
        unsigned int offset = pc[1];
        next_pc = (unsigned char*)sp[offset];
    }
    else if ((0xF0 & opcode) == 0x60)                  /* 0110xxxx group of commands:
                                                          01100000  2   SUB 1
                                                          01100001  2   CMP 1
                                                          01100010  2   ADD 1
                                                          01100011  2   MUL 1
                                                          01100100  2   AND 1
                                                          01100101  2   OR 1
                                                          01100110  2   MOV 3
                                                          01100111  2   RTSD 1 - already processed
                                                          0110100x  2   SHLR 1
                                                          0110101x  2   SHAR 1
                                                          0110110x  2   SHLL 1
                                                          01101110  2   PUSHM
                                                          01101111  2   POPM
                                                       */
    {
        next_pc = pc + 2;
    }
    else if (0x75 == opcode)                           /* 01110101  3   INT */
    {
        unsigned int *intb = (unsigned int*)registers[INTB];
        unsigned int n = pc[2];
        next_pc = (unsigned char*)intb[n];
    }
    else if ((0xF8 & opcode) == 0x70)                  /* 01110xxx group of commands:
                                                          011100xx  3+  ADD 3
                                                          01110101  3   CMP 2
                                                          01110101  3   INT - already processed
                                                          01110101  3   MOV 5
                                                          01110101  3   MVTIPL
                                                          011101xx  3+  AND 2
                                                          011101xx  3+  CMP 3
                                                          011101xx  3+  MUL 2
                                                          011101xx  3+  OR 2
                                                       */
    {
        unsigned int li = (opcode & 0x03);
        if (0x03 == li)
        {
            li = 4;
        }
        next_pc = pc + 2 + li;
    }
    else if ((0xFC & opcode) == 0x78 ||                /* 011110xx
                                                          0111100x  2   BSET 3
                                                          0111101x  2   BCLR 3
                                                       */
             (0xFE & opcode) == 0x7C ||                /* 0111110x  2   BTST 3 */
             0x7E == opcode)                           /* 01111110  2   ABS 1
                                                          01111110  2   NEG 1
                                                          01111110  2   NOT 1
                                                          01111110  2   POP
                                                          01111110  2   POPC
                                                          01111110  2   PUSH 1
                                                          01111110  2   PUSHC
                                                          01111110  2   ROLC
                                                          01111110  2   RORC
                                                          01111110  2   SAT
                                                       */
    {
        next_pc = pc + 2;
    }
    else if (0x7F == opcode)                           /* 01111111
                                                          01111111 0000xxxx 2   JMP
                                                          01111111 0001xxxx 2   JSR
                                                          01111111 0101xxxx 2   BSR 3
                                                          01111111 10000011 2   SCMPU
                                                          01111111 100000xx 2   SUNTIL
                                                          01111111 10000111 2   SMOVU
                                                          01111111 100001xx 2   SWHILE
                                                          01111111 10001011 2   SMOVB
                                                          01111111 100010xx 2   SSTR
                                                          01111111 10001111 2   SMOVF
                                                          01111111 100011xx 2   RMPA
                                                          01111111 10010011 2   SATR
                                                          01111111 10010100 2   RTFI
                                                          01111111 10010101 2   RTE
                                                          01111111 10010110 2   WAIT
                                                          01111111 1010xxxx 2   SETPSW
                                                          01111111 1011xxxx 2   CLRPSW
                                                       */
    {
        unsigned int byte1 = pc[1];
        if ((0xE0 & byte1) == 0x00)                    /* 01111111 000xxxxx
                                                          01111111 0000xxxx 2   JMP
                                                          01111111 0001xxxx 2   JSR
                                                       */
        {
            unsigned int rs = byte1 & 0x0F;
            next_pc = (unsigned char*)registers[rs];
        }
        else if ((0xF0 & byte1) == 0x50)               /* 01111111 0101xxxx 2   BSR 3 */
        {
            unsigned int rs = byte1 & 0x0F;
            next_pc = pc + (signed int)registers[rs];
        }
        else if (0x94  == byte1)                       /* 01111111 10010100 2   RTFI */
        {
            unsigned char *bpc = (unsigned char*)registers[BPC];
            next_pc = bpc;
        }
        else if (0x95 ==  byte1)                       /* 01111111 10010101 2   RTE */
        {
            unsigned int *sp = (unsigned int*)registers[ISP];
            next_pc = (unsigned char*)sp[0];
        }
        else                                           /* All other instructions from this group */
        {
            next_pc = pc + 2;
        }
    }
    else if ((0xC0 & opcode) == 0x80)                  /* 10xxxxxx
                                                          1011xxxx  2   MOVU 1
                                                          10xx0xxx  2   MOV 1
                                                          10xx1xxx  2   MOV 2
                                                       */
    {
        next_pc = pc + 2;
    }
    else if (0xFC == opcode)                           /* 11111100
                                                          11111100 00000011 3   SBB 1
                                                          11111100 00000111 3   NEG 2
                                                          11111100 00001011 3   ADC 2
                                                          11111100 00001111 3   ABS 2
                                                          11111100 00111011 3   NOT 2
                                                          11111100 01100011 3   BSET 4
                                                          11111100 01100111 3   BCLR 4
                                                          11111100 01101011 3   BTST 4
                                                          11111100 01101111 3   BNOT 4
                                                          11111100 000100xx 3+  MAX 2
                                                          11111100 000101xx 3+  MIN 2
                                                          11111100 000110xx 3+  EMUL 2
                                                          11111100 000111xx 3+  EMULU 2
                                                          11111100 001000xx 3+  DIV 2
                                                          11111100 001001xx 3+  DIVU 2
                                                          11111100 001100xx 3+  TST 2
                                                          11111100 001101xx 3+  XOR 2
                                                          11111100 010000xx 3+  XCHG 1
                                                          11111100 010001xx 3+  ITOF 1
                                                          11111100 011000xx 3+  BSET 2
                                                          11111100 011001xx 3+  BCLR 2
                                                          11111100 011010xx 3+  BTST 2
                                                          11111100 011011xx 3+  BNOT 2
                                                          11111100 100000xx 3+  FSUB 2
                                                          11111100 100001xx 3+  FCMP 2
                                                          11111100 100010xx 3+  FADD 2
                                                          11111100 100011xx 3+  FMUL 2
                                                          11111100 100100xx 3+  FDIV 2
                                                          11111100 100101xx 3+  FTOI
                                                          11111100 100110xx 3+  ROUND
                                                          11111100 1101xxxx 3+  SCCnd
                                                          11111100 111xxxxx 3+  BMCnd 1
                                                          11111100 111xxxxx 3+  BNOT 1
                                                       */
    {
        unsigned int ld = pc[1] & 0x03;
        if (3 == ld)
        {
            ld = 0;
        }
        next_pc = pc + 3 + ld;
    }
    else if (0xFD == opcode)                           /* 11111101 */
    {
        unsigned int byte1 = pc[1];
        if (0x72 == byte1)                             /* 11111101 01110010 7   FADD 1
                                                          11111101 01110010 7   FCMP 1
                                                          11111101 01110010 7   FDIV 1
                                                          11111101 01110010 7   FMUL 1
                                                          11111101 01110010 7   FSUB 1
                                                       */
        {
            next_pc = pc + 7;
        }
        else if ((0xF3 & byte1) == 0x70 ||             /* 11111101 0111xx00 4+  ADC 1
                                                          11111101 0111xx00 4+  DIV 1
                                                          11111101 0111xx00 4+  DIVU 1
                                                          11111101 0111xx00 4+  EMUL 1
                                                          11111101 0111xx00 4+  EMULU 1
                                                          11111101 0111xx00 4+  MAX 1
                                                          11111101 0111xx00 4+  MIN 1
                                                          11111101 0111xx00 4+  STNZ
                                                          11111101 0111xx00 4+  STZ
                                                          11111101 0111xx00 4+  TST 1
                                                          11111101 0111xx00 4+  XOR 1
                                                       */
                 (0xF3 & byte1) == 0x73)               /* 11111101 0111xx11 4+  MVTC 1 */
        {
            unsigned int li = byte1 & 0x03;
            if (0 == li)
            {
                li = 4;
            }
            next_pc = pc + 3 + li;
        }
        else                                           /* 11111101 100xxxxx 3   SHLR 3
                                                          11111101 101xxxxx 3   SHAR 3
                                                          11111101 110xxxxx 3   SHLL 3
                                                          11111101 111xxxxx 3   BMCnd 2
                                                          11111101 111xxxxx 3   BNOT 3
                                                          11111101 00000000 3   MULHI
                                                          11111101 00000001 3   MULLO
                                                          11111101 00000100 3   MACHI
                                                          11111101 00000101 3   MACLO
                                                          11111101 00010111 3   MVTACHI
                                                          11111101 00010111 3   MVTACLO
                                                          11111101 00011000 3   RACW
                                                          11111101 00011111 3   MVFACHI
                                                          11111101 00011111 3   MVFACMI
                                                          11111101 0010xxxx 3   MOV 14
                                                          11111101 0010xxxx 3   MOV 15
                                                          11111101 0011xx0x 3   MOVU 4
                                                          11111101 01100000 3   SHLR 2
                                                          11111101 01100001 3   SHAR 2
                                                          11111101 01100010 3   SHLL 2
                                                          11111101 01100100 3   ROTR 2
                                                          11111101 01100101 3   REVW
                                                          11111101 01100110 3   ROTL 2
                                                          11111101 01100111 3   REVL
                                                          11111101 01101000 3   MVTC 2
                                                          11111101 01101010 3   MVFC
                                                          11111101 0110110x 3   ROTR 1
                                                          11111101 0110111x 3   ROTL 1
                                                       */
        {
            next_pc = pc + 3;
        }
    }
    else if ((0xFE & opcode) == 0xFE)                  /* 1111111x
                                                          11111110  3   MOV 10
                                                          11111110  3   MOV 12
                                                          11111110  3   MOVU 3
                                                          11111111  3   ADD 4
                                                          11111111  3   ADD 4
                                                          11111111  3   MUL 4
                                                          11111111  3   OR 4
                                                          11111111  3   SUB 3
                                                       */
    {
        next_pc = pc + 3;
    }
    else if ((0xF8 & opcode) == 0xF0)                  /* 11110xxx
                                                          111100xx  2+  BCLR 1
                                                          111100xx  2+  BSET 1
                                                          111101xx  2+  BTST 1
                                                          111101xx  2+  PUSH 2
                                                       */
    {
        unsigned int ld = opcode & 0x03;
        next_pc = pc + 2 + ld;
    }
    else if (0xFB == opcode)                           /* 11111011  3+  MOV 6 */
    {
        unsigned int li = (pc[1] >> 2) & 0x03;
        if (0 == li)
        {
            li = 4;
        }
        next_pc = pc + 2 + li;
    }
    else if ((0xFE & opcode) == 0xF8)                  /* 111110xx  3+  MOV 8 */
    {
        unsigned int ld = opcode & 0x03;
        unsigned int li = (pc[1] >> 2) & 0x03;
        if (0 == li)
        {
            li = 4;
        }
        next_pc = pc + 2 + ld + li;
    }
    else if ((0xC0 & opcode) == 0xC0)                  /* 11xxxxxx
                                                          11xx1111  2   MOV 7
                                                          11xx11xx  2+  MOV 9
                                                          11xxxx11  2+  MOV 11
                                                          11xxxxxx  2+  MOV 13
                                                        */
    {
        unsigned int lds = opcode & 0x03;
        unsigned int ldd = (opcode >> 2) & 0x03;
        if (3 == lds)
        {
            lds = 0;
        }
        if (3 == ldd)
        {
            ldd = 0;
        }
        next_pc = pc + 2 + lds + ldd;
    }
    else
    {
        /* Unknown opcode */
    }
    return (unsigned int)next_pc;
}

static unsigned int char2int (char c)
{
    if ('0' <= c &&
        '9' >= c)
    {
        return c - '0';
    }
    else if ('a' <= c &&
             'f' >= c)
    {
        return c - 'a' + 10U;
    }
    else if ('A' <= c &&
             'F' >= c)
    {
        return c - 'A' + 10U;
    }
    else
    {
        return UINT_MAX;
    }
}

static unsigned int hex2int (const char *src, /*@null@*/ const char **p)
/*@globals nothing@*/
/*@modifies *p@*/
{
    unsigned int val = 0U;
    unsigned int nibble = 0U;
    unsigned int count = 0U;
    const char *s = src;
    while (UINT_MAX != (nibble = char2int(*s)) &&
           count < (sizeof(val) * 2))
    {
        val <<= 4U;
        val += nibble;
        ++s;
        ++count;
    }
    if (NULL != p)
    {
        *p = s;
    }
    return val;
}

static void mem2hex_1 (char *dst, const void *src, size_t size)
{
    size_t i;
    const uint8_t *s = (const uint8_t*)src;
    char *d = dst;
    for (i = size; i; --i)
    {
        *d++ = hexchars[(*s >> 4) & 0x0F];
        *d++ = hexchars[(*s >> 0) & 0x0F];
        ++s;
    }
    *d = '\0';
}

static void mem2hex_2 (char *dst, const void *src, size_t size)
{
    size_t i;
    const uint16_t *s = (const uint16_t*)src;
    char *d = dst;
    for (i = size; i; --i)
    {
        uint16_t tmp = *s;
        mem2hex_1(d, &tmp, sizeof tmp);
        d += sizeof(tmp) * 2;
        ++s;
    }
}

static void mem2hex_4 (char *dst, const void *src, size_t size)
{
    size_t i;
    const uint32_t *s = (const uint32_t*)src;
    char *d = dst;
    for (i = size; i; --i)
    {
        uint32_t tmp = *s;
        mem2hex_1(d, &tmp, sizeof tmp);
        d += sizeof(tmp) * 2;
        ++s;
    }
}

static void mem2hex (char *dst, const void *src, size_t size)
{
    if (0 == ((uint32_t)src % 4)
        && 0 == (size % 4))
    {
        mem2hex_4(dst, src, size / 4);
    }
    else if (0 == ((uint32_t)src % 2)
             && 0 == (size % 2))
    {
        mem2hex_2(dst, src, size / 2);
    }
    else
    {
        mem2hex_1(dst, src, size);
    }
}

static void hex2mem_1 (/*@out@*/ uint8_t *dst, const char *src, size_t size)
{
    size_t i;
    const char *s = src;
    uint8_t *d = dst;
    for (i = size; i; --i)
    {
        unsigned int tmp;
        tmp = char2int(*s++) << 4;
        tmp += char2int(*s++);
        *d = tmp;
        ++d;
    }
}

static void hex2mem_2 (/*@out@*/ uint16_t *dst, const char *src, size_t size)
{
    size_t i;
    const char *s = src;
    uint16_t *d = dst;
    for (i = size; i; --i)
    {
        uint16_t tmp;
        hex2mem_1((uint8_t*)&tmp, s, sizeof tmp);
        *d = tmp;
        s += sizeof(tmp) * 2;
        ++d;
    }
}

static void hex2mem_4 (/*@out@*/ uint32_t *dst, const char *src, size_t size)
{
    size_t i;
    const char *s = src;
    uint32_t *d = dst;
    for (i = size; i; --i)
    {
        uint32_t tmp;
        hex2mem_1((uint8_t*)&tmp, s, sizeof tmp);
        *d = tmp;
        s += sizeof(tmp) * 2;
        ++d;
    }
}

static void hex2mem (/*@out@*/ void *dst, const char *src, size_t size)
{
    if (0 == ((uint32_t)dst % 4)
        && 0 == (size % 4))
    {
        hex2mem_4(dst, src, size / 4);
    }
    else if (0 == ((uint32_t)dst % 2)
             && 0 == (size % 2))
    {
        hex2mem_2(dst, src, size / 2);
    }
    else
    {
        hex2mem_1(dst, src, size);
    }
}

static void get_packet(void)
{
    char c = '\0';
    /* Retry until correct packet is received */
    for (;;)
    {
        unsigned int checksum = 0;
        unsigned int count = 0;
        char *rxp = trx_buffer;

        /* Wait for start byte */
        while ('$' != c)
        {
            c = stub_getchar();
        }

        /* Receive packet payload */
        while (BUFFER_SIZE > count)
        {
            c = stub_getchar();
            if ('$' == c ||
                '#' == c)
            {
                /*@innerbreak@*/
                break;
            }
            checksum += c;
            count += 1;
            *rxp++ = c;
        }
        *rxp = '\0';
        /* Receive and verify checksum */
        if ('#' == c)
        {
            unsigned int received_checksum = 0;
            received_checksum += char2int(stub_getchar()) << 4;
            received_checksum += char2int(stub_getchar());
            checksum &= 0x00FF;
            if (checksum == received_checksum)
            {
                stub_putchar('+');
                break;
            }
            else
            {
                stub_putchar('-');
            }
        }
    }
}

static void put_packet (const char *buffer)
{
    do
    {
        const char *p = buffer;
        unsigned int checksum = 0;
        stub_putchar('$');
        while ('\0' != *p)
        {
            stub_putchar(*p);
            checksum += *p;
            ++p;
        }
        stub_putchar('#');
        stub_putchar(hexchars[(checksum >> 4) & 0x0F]);
        stub_putchar(hexchars[(checksum >> 0) & 0x0F]);
    }
    while ('+' != stub_getchar());
}

static void start_step (void)
{
    stepping_brk_address = (unsigned char*)get_next_pc();
    stepping_brk_opcode = *stepping_brk_address;
    *stepping_brk_address = OPCODE_BRK;
#ifdef DEBUG_STEPPING
    char report[] = "stepi from xxxxxxxx to xxxxxxxx\n";
    unsigned int v = registers[PC];
    report[11] = hexchars[(v >> 28) & 0x0F];
    report[12] = hexchars[(v >> 24) & 0x0F];
    report[13] = hexchars[(v >> 20) & 0x0F];
    report[14] = hexchars[(v >> 16) & 0x0F];
    report[15] = hexchars[(v >> 12) & 0x0F];
    report[16] = hexchars[(v >> 8) & 0x0F];
    report[17] = hexchars[(v >> 4) & 0x0F];
    report[18] = hexchars[(v >> 0) & 0x0F];
    v = (unsigned int)stepping_brk_address;
    report[23] = hexchars[(v >> 28) & 0x0F];
    report[24] = hexchars[(v >> 24) & 0x0F];
    report[25] = hexchars[(v >> 20) & 0x0F];
    report[26] = hexchars[(v >> 16) & 0x0F];
    report[27] = hexchars[(v >> 12) & 0x0F];
    report[28] = hexchars[(v >> 8) & 0x0F];
    report[29] = hexchars[(v >> 4) & 0x0F];
    report[30] = hexchars[(v >> 0) & 0x0F];
    trx_buffer[0] = 'O';
    mem2hex(trx_buffer + 1, report, sizeof(report) - 1);
    put_packet(trx_buffer);
#endif /* DEBUG_STEPPING */
}

static void finish_step (void)
{
    if (NULL != stepping_brk_address)
    {
        /* Step back on breakpoint instruction if it is hit */
        if ((stepping_brk_address + 1) == (unsigned char*)registers[PC])
        {
            --registers[PC];
        }
        /* Clear breakpoint */
        if (OPCODE_BRK != stepping_brk_opcode)
        {
            *stepping_brk_address = stepping_brk_opcode;
            stepping_brk_address = NULL;
            stepping_brk_opcode = OPCODE_BRK;
        }
    }
}

static void prepare_state_report (void *dst, unsigned int signal)
{
    char *p = (char*)dst;
    /* Report current state
       Use extended reply format: report PC value */
    *p++ = 'T';
    *p++ = hexchars[(signal >> 4) & 0x0F];
    *p++ = hexchars[(signal >> 0) & 0x0F];
    /* Report PC register value */
    *p++ = hexchars[(PC >> 4) & 0x0F];
    *p++ = hexchars[(PC >> 0) & 0x0F];
    *p++ = ':';
    mem2hex(p, &registers[PC], sizeof registers[0]);
    p += sizeof(registers[0]) * 2;
    *p++ = ';';
    /* Report PSW register value */
    *p++ = hexchars[(PSW >> 4) & 0x0F];
    *p++ = hexchars[(PSW >> 0) & 0x0F];
    *p++ = ':';
    mem2hex(p, &registers[PSW], sizeof registers[0]);
    p += sizeof(registers[0]) * 2;
    *p++ = ';';
    *p = '\0';
}

static void stub_rsp_handler (unsigned int signal)
{
    if (stepping)
    {
        stepping = 0;
        finish_step();
    }

    /* Report current state */
    prepare_state_report(trx_buffer, signal);
    put_packet(trx_buffer);

    /* Communicate with GDB */
    for (;;)
    {
        const char *p = trx_buffer;
        trx_buffer[0] = '\0';
        get_packet();
        /*@-loopswitchbreak@*/
        switch (*p++)
        {
        case '?':                                           /* Report current state */
            prepare_state_report(trx_buffer, signal);
            break;
        case 'g':                                           /* Read registers */
            mem2hex(trx_buffer, registers, sizeof registers);
            break;
        case 'G':                                           /* Write registers */
            hex2mem(registers, p, sizeof registers);
            strcpy(trx_buffer, "OK");
            break;
        case 'p':                                           /* Read specific register */
        {
            unsigned int register_size = sizeof registers[0];
            unsigned int n = hex2int(p, NULL);
            if (NUM_REGS <= n)
            {
                strcpy(trx_buffer, "E02");
                break;
            }
            /* If ACC value is requested,
               double register size, since ACC size is 8 bytes */
            if (ACC == n)
            {
                register_size *= 2;
            }
            mem2hex(trx_buffer, &registers[n], register_size);
            break;
        }
        case 'P':                                           /* Write specific register */
        {
            unsigned int register_size = sizeof registers[0];
            unsigned int n = hex2int(p, &p);
            if ('=' != *p++)
            {
                strcpy(trx_buffer, "E01");
                break;
            }
            if (NUM_REGS <= n)
            {
                strcpy(trx_buffer, "E02");
                break;
            }
            /* If ACC value is requested,
               double register size, since ACC size is 8 bytes */
            if (ACC == n)
            {
                register_size *= 2;
            }
            hex2mem(&registers[n], p, register_size);
            strcpy(trx_buffer, "OK");
            break;
        }
        case 'm':                                           /* Read memory */
        {
            unsigned int length;
            unsigned int address = hex2int(p, &p);
            if (',' != *p++)
            {
                strcpy(trx_buffer, "E01");
                break;
            }
            length = hex2int(p, NULL);
            mem2hex(trx_buffer, (const void*)address, length);
            break;
        }
        case 'M':                                           /* Write memory */
        {
            unsigned int length;
            void * address = (void*)hex2int(p, &p);
            if (',' != *p++)
            {
                strcpy(trx_buffer, "E01");
                break;
            }
            length = hex2int(p, &p);
            if (':' != *p++)
            {
                strcpy(trx_buffer, "E01");
                break;
            }
            /* Check if destination area is RAM */
            if (RAM_END < (void*)((char*)address + length))
            {
                strcpy(trx_buffer, "E02");
                break;
            }
            hex2mem((void*)address, p, length);
            strcpy(trx_buffer, "OK");
            break;
        }
        case 'c':                                           /* Continue */
            /* If 'continue from address' is requested */
            if ('\0' != *p)
            {
                registers[PC] = hex2int(p, NULL);
            }
            return;
        case 's':                                           /* Step */
        {
            /* If 'step from address' is requested */
            if ('\0' != *p)
            {
                registers[PC] = hex2int(p, NULL);
            }
            if (OPCODE_BRK == *(unsigned char*)registers[PC])
            {
                ++registers[PC];
                prepare_state_report(trx_buffer, TARGET_SIGNAL_TRAP);
                break;
            }
            else
            {
                stepping = 1;
                start_step();
                return;
            }
        }
        case 'q':                                           /* Query */
            if (0 == strncmp(p, "Supported", strlen("Supported")))
            {
                strcpy(trx_buffer, "PacketSize=200");
            }
            else if (0 == strcmp(p, "Offsets"))
            {
                strcpy(trx_buffer, "Text=0;Data=0;Bss=0");
            }
            else
            {
                trx_buffer[0] = '\0';
            }
            break;
        case 'd':                                           /* Toggle debug */
        case 'z':                                           /* Remove breakpoint */
        case 'Z':                                           /* Set breakpoint */
        default:
            trx_buffer[0] = '\0';
            break;
        }
        /*@=loopswitchbreak@*/
        put_packet(trx_buffer);
    }
}

void debug_puts (/*@unused@*/ const char *str)
{
    __asm__ __volatile__ ("int #1");
}

static void stub_puts (const char *str)
{
    unsigned int length = strlen(str);
    /* Truncate string to match buffer size */
    if ((length * 2) > (BUFFER_SIZE - 4))
    {
        length = (BUFFER_SIZE - 4) / 2;
    }
    trx_buffer[0] = 'O';
    mem2hex(trx_buffer + 1, str, length);
    strcat(trx_buffer, "0A");
    put_packet(trx_buffer);
}

__attribute__((interrupt,naked))
static void stub_puts_handler (void)
{
    __asm__ __volatile__ (
        "pushm  r1-r15      \n"
        "mov.l  %0, r15     \n"
        "jsr    r15         \n"
        "popm   r1-r15      \n"
        "rte                \n"
        :: "i" (stub_puts)
        );
}

__attribute__((interrupt,naked))
static void stub_rx_handler (void)
{
    save_context();
    IR(SCI1, RXI1) = 0;
    {
        char c = SCI1.RDR;
        if (SIGBREAK == c)
        {
            stub_rsp_handler(TARGET_SIGNAL_INT);
        }
    }
    restore_context_and_exit();
}

__attribute__((interrupt,naked))
static void stub_erx_handler (void)
{
    save_context();
    /* Wait till the end of BREAK signal */
    while (0 == PORT3.PORT.BIT.B0);
    /* Reset interrupt flags */
    IR(SCI1, ERI1) = 0;
    IR(SCI1, RXI1) = 0;
    /* Read any data present */
    /*@-noeffect@*/
    (void)SCI1.RDR;
    /*@=noeffect@*/
    /* Reset error flags in status register */
    SCI1.SSR.BYTE = 0x84;
    stub_rsp_handler(TARGET_SIGNAL_INT);
    restore_context_and_exit();
}

__attribute__((interrupt,naked))
static void stub_brk_handler (void)
{
    save_context();
    stub_rsp_handler(TARGET_SIGNAL_TRAP);
    restore_context_and_exit();
}

static void stub_putchar (char c)
{
    while (0 == IR(SCI1,TXI1));
    IR(SCI1,TXI1) = 0;
    SCI1.TDR = c;
}

static char stub_getchar (void)
{
    char c;
    while (0 == IR(SCI1,RXI1));
    IR(SCI1,RXI1) = 0;
    c = SCI1.RDR;
    return c;
}

#ifndef PCLK_FREQUENCY
#define PCLK_FREQUENCY 48000000UL
#endif

#ifndef SCI1_BAUDRATE
#define SCI1_BAUDRATE  115200U
#endif

void stub_init (void)
{
    /* Configure system clocks as following:
       EXTAL = 12 MHz
       ICLK = 8 * EXTAL = 96 MHz
       BCLK = 4 * EXTAL = 48 MHz
       PCLK = 4 * EXTAL = 48 MHz
       BCLK and SDCLK outputs are disabled (fixed high)
    */
    SYSTEM.SCKCR.LONG = 0x00C10100UL;

    _isr_vectors[0] = stub_brk_handler;
    _isr_vectors[1] = stub_puts_handler;
    _isr_vectors[VECT(SCI1, RXI1)] = stub_rx_handler;
    _isr_vectors[VECT(SCI1, ERI1)] = stub_erx_handler;

    /* Configure SCI1 */
    MSTP(SCI1) = 0;                                         /* Enable module */
    SCI1.SCR.BYTE = 0;                                      /* Reset module */
    /* Setup RXD1 pin */
    PORT3.DDR.BIT.B0 = 0;
    PORT3.ICR.BIT.B0 = 1;
    /* Setup TXD1 pin */
    PORT2.DDR.BIT.B6 = 1;

    SCI1.SCR.BIT.CKE = 0;                                   /* Use internal baudrate generator, SCK pin functions as IO port */
    SCI1.SMR.BYTE = 0;                                      /* PCLK/1, 8N1, asynchronous mode, multiprocessor mode is disabled */
    SCI1.SCMR.BIT.SMIF = 0;                                 /* Not smart card mode */
    SCI1.SCMR.BIT.SINV = 0;                                 /* No TDR inversion */
    SCI1.SCMR.BIT.SDIR = 0;                                 /* LSB first */
    SCI1.SEMR.BIT.ACS0 = 0;                                 /* Use external clock */
    SCI1.SEMR.BIT.ABCS = 1;                                 /* 8 base clock cycles for 1 bit period */
    /* Set baudrate */
    SCI1.BRR = PCLK_FREQUENCY / (16 * SCI1_BAUDRATE) - 1;
    /* Reset interrupt flags */
    IR(SCI1, RXI1) = 0;
    IR(SCI1, TXI1) = 0;
    IR(SCI1, ERI1) = 0;
    IR(SCI1, TEI1) = 0;
    /* Set priorities - highest */
    IPR(SCI1, RXI1) = 15;
    IPR(SCI1, TXI1) = 15;
    IPR(SCI1, ERI1) = 15;
    IPR(SCI1, TEI1) = 15;
    /* Disable interrupts */
    IEN(SCI1, RXI1) = 1;
    IEN(SCI1, TXI1) = 0;
    IEN(SCI1, ERI1) = 1;
    IEN(SCI1, TEI1) = 0;
    /* Enable interrupt requests */
    SCI1.SCR.BIT.RIE = 1;
    SCI1.SCR.BIT.TIE = 1;
    /* Wait at least one bit interval */
    {
        int i;
        for (i = 20000ul; i > 0; --i)
        {
            __asm__ __volatile__ ("");
        }
    }
    /* Enable receiver and transmitter. This MUST be done simultaneously. */
    SCI1.SCR.BYTE |= 0x30;
}
