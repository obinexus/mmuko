#ifndef MMUKO_CONTEXT_LAYOUT_AARCH64_AAPCS64_H
#define MMUKO_CONTEXT_LAYOUT_AARCH64_AAPCS64_H

/* AArch64 AAPCS64 layout of mmuko_context_machine.
 * Shared by mmuko_context_switch.S, mmuko_context_arch.c and the ABI tests;
 * the C side checks it with _Static_assert, the assembly with .if/.error.
 *
 *   off  size  slot      ABI role
 *   ---  ----  --------  ---------------------------------------------
 *     0    80  x19-x28   callee-saved general registers (stp pairs)
 *    80     8  x29       frame pointer (callee-saved)
 *    88     8  x30       link register at the switch
 *    96     8  sp        stack pointer (16-byte aligned at all times)
 *   104     8  pc        resume position
 *   112    64  d8-d15    low 64 bits of v8-v15 (callee-saved)
 *   176     8  fpcr      floating-point control (rounding/flush modes)
 */
#define MMUKO_CTX_X19   0
#define MMUKO_CTX_X20   8
#define MMUKO_CTX_X21  16
#define MMUKO_CTX_X22  24
#define MMUKO_CTX_X23  32
#define MMUKO_CTX_X24  40
#define MMUKO_CTX_X25  48
#define MMUKO_CTX_X26  56
#define MMUKO_CTX_X27  64
#define MMUKO_CTX_X28  72
#define MMUKO_CTX_X29  80
#define MMUKO_CTX_X30  88
#define MMUKO_CTX_SP   96
#define MMUKO_CTX_PC  104
#define MMUKO_CTX_D8  112
#define MMUKO_CTX_D10 128
#define MMUKO_CTX_D12 144
#define MMUKO_CTX_D14 160
#define MMUKO_CTX_FPCR 176
#define MMUKO_CTX_SIZE 184

/* Registers that carry the start arguments into mmuko_context_start. They
 * are callee-saved, so the context pointer in x19 survives entry() and is
 * still there when entry returns into the exit trampoline. */
#define MMUKO_CTX_SEED_CONTEXT  MMUKO_CTX_X19
#define MMUKO_CTX_SEED_ENTRY    MMUKO_CTX_X20
#define MMUKO_CTX_SEED_ARGUMENT MMUKO_CTX_X21

#endif
