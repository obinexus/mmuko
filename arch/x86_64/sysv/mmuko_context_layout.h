#ifndef MMUKO_CONTEXT_LAYOUT_X86_64_SYSV_H
#define MMUKO_CONTEXT_LAYOUT_X86_64_SYSV_H

/* x86-64 System V layout of mmuko_context_machine.
 * Shared by mmuko_context_switch.S, mmuko_context_arch.c and the ABI tests;
 * the C side checks it with _Static_assert, the assembly with .if/.error.
 *
 *   off  size  slot   ABI role
 *   ---  ----  -----  ------------------------------------------------
 *     0     8  rsp    stack pointer as seen after the switch "returns"
 *     8     8  rip    resume position (return address of the switch)
 *    16     8  rbx    callee-saved
 *    24     8  rbp    callee-saved (frame pointer)
 *    32     8  r12    callee-saved
 *    40     8  r13    callee-saved
 *    48     8  r14    callee-saved
 *    56     8  r15    callee-saved
 *    64     4  mxcsr  SSE control bits are callee-saved
 *    68     2  fpucw  x87 control word is callee-saved
 *    70     2  -      padding
 */
#define MMUKO_CTX_RSP    0
#define MMUKO_CTX_RIP    8
#define MMUKO_CTX_RBX   16
#define MMUKO_CTX_RBP   24
#define MMUKO_CTX_R12   32
#define MMUKO_CTX_R13   40
#define MMUKO_CTX_R14   48
#define MMUKO_CTX_R15   56
#define MMUKO_CTX_MXCSR 64
#define MMUKO_CTX_FPUCW 68
#define MMUKO_CTX_SIZE  72

/* Registers that carry the start arguments into mmuko_context_start. They
 * are callee-saved, so the context pointer in rbx survives entry() and is
 * still there when entry returns into the exit trampoline. */
#define MMUKO_CTX_SEED_CONTEXT  MMUKO_CTX_RBX
#define MMUKO_CTX_SEED_ENTRY    MMUKO_CTX_R12
#define MMUKO_CTX_SEED_ARGUMENT MMUKO_CTX_R13

/* Process-start values mandated by the ABI. */
#define MMUKO_CTX_DEFAULT_MXCSR 0x1F80
#define MMUKO_CTX_DEFAULT_FPUCW 0x037F

#endif
