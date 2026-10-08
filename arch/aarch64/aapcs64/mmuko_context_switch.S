/* MMUKO AArch64 AAPCS64 execution-context binding.
 *
 * void mmuko_context_switch(mmuko_context *from, mmuko_context *to)
 *   arg0 from: x0    arg1 to: x1    no return value
 *
 * Saves the AAPCS64 non-volatile state - x19-x28, x29 (fp), sp, the low
 * 64 bits of v8-v15 (d8-d15) - plus x30, the resume position, and FPCR.
 * x9-x17 are scratch here; the C caller already treats them as clobbered.
 *
 * Resume uses `ret x16` rather than `br`: RET is not subject to BTI
 * landing-pad checks, so resuming at an arbitrary post-call instruction stays
 * valid even in BTI-guarded processes. Offsets come from
 * mmuko_context_layout.h, shared with the C side.
 */
#include <mmuko/context.h>
#include "mmuko_context_layout.h"

        .if MMUKO_CTX_SIZE > MMUKO_CONTEXT_MACHINE_BYTES
        .error "AArch64 context layout exceeds the opaque machine block"
        .endif
        .if (MMUKO_CTX_D14 + 8) > 504
        .error "stp/ldp pair offsets must fit the signed 7-bit scaled immediate"
        .endif

        .text
        .p2align 4
        .globl mmuko_context_switch
        .type mmuko_context_switch, %function
mmuko_context_switch:
        /* 1-4: save the running context into *from. */
        mov     x9, sp
        stp     x19, x20, [x0, #MMUKO_CTX_X19]
        stp     x21, x22, [x0, #MMUKO_CTX_X21]
        stp     x23, x24, [x0, #MMUKO_CTX_X23]
        stp     x25, x26, [x0, #MMUKO_CTX_X25]
        stp     x27, x28, [x0, #MMUKO_CTX_X27]
        stp     x29, x30, [x0, #MMUKO_CTX_X29]
        str     x9,  [x0, #MMUKO_CTX_SP]
        str     x30, [x0, #MMUKO_CTX_PC]        /* resume position = return address */
        stp     d8,  d9,  [x0, #MMUKO_CTX_D8]
        stp     d10, d11, [x0, #MMUKO_CTX_D10]
        stp     d12, d13, [x0, #MMUKO_CTX_D12]
        stp     d14, d15, [x0, #MMUKO_CTX_D14]
        mrs     x10, fpcr
        str     x10, [x0, #MMUKO_CTX_FPCR]

        /* 5-7: load *to - callee-saved registers, FP state, stack. */
        ldp     x19, x20, [x1, #MMUKO_CTX_X19]
        ldp     x21, x22, [x1, #MMUKO_CTX_X21]
        ldp     x23, x24, [x1, #MMUKO_CTX_X23]
        ldp     x25, x26, [x1, #MMUKO_CTX_X25]
        ldp     x27, x28, [x1, #MMUKO_CTX_X27]
        ldp     x29, x30, [x1, #MMUKO_CTX_X29]
        ldp     d8,  d9,  [x1, #MMUKO_CTX_D8]
        ldp     d10, d11, [x1, #MMUKO_CTX_D10]
        ldp     d12, d13, [x1, #MMUKO_CTX_D12]
        ldp     d14, d15, [x1, #MMUKO_CTX_D14]
        ldr     x10, [x1, #MMUKO_CTX_FPCR]
        msr     fpcr, x10
        ldr     x9,  [x1, #MMUKO_CTX_SP]
        mov     sp, x9

        /* 8: continue where *to last yielded (or at mmuko_context_start). */
        ldr     x16, [x1, #MMUKO_CTX_PC]
        ret     x16
        .size mmuko_context_switch, .-mmuko_context_switch

/* First instruction of every new context (seeded by mmuko_context_arch.c):
 *   x19 = mmuko_context *    x20 = entry    x21 = argument
 *   sp  = 16-byte aligned top of the context's own stack
 */
        .p2align 4
        .globl mmuko_context_start
        .type mmuko_context_start, %function
mmuko_context_start:
        mov     x0, x21
        blr     x20
        /* entry() returned: fall into the context-exit trampoline. */
        .size mmuko_context_start, .-mmuko_context_start

/* Context-exit trampoline. entry() preserved x19 (callee-saved), so it still
 * holds the context; sp is back at its aligned start value. Marks the
 * context TERMINATED and switches to its owner; never returns. */
        .globl mmuko_context_exit_trampoline
        .type mmuko_context_exit_trampoline, %function
mmuko_context_exit_trampoline:
        mov     x0, x19
        mov     w1, #0
        bl      mmuko_context_exit
        brk     #0x3e8                          /* unreachable: exit never returns */
        .size mmuko_context_exit_trampoline, .-mmuko_context_exit_trampoline

        .section .note.GNU-stack,"",%progbits
