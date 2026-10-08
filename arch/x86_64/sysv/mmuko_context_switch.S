/* MMUKO x86-64 System V execution-context binding.
 *
 * void mmuko_context_switch(mmuko_context *from, mmuko_context *to)
 *   arg0 from: %rdi    arg1 to: %rsi    no return value
 *
 * Saves exactly the state the ABI obliges a callee to preserve - rbx, rbp,
 * r12-r15, rsp, the MXCSR and x87 control words - plus the resume position
 * (the return address). Every other register is caller-saved, so the C
 * caller already treats it as clobbered by this call.
 *
 * Resuming a context never executes `ret`: it reloads rsp to the value the
 * caller would see after returning and jumps to the saved return address.
 * Layout offsets come from mmuko_context_layout.h, shared with the C side.
 */
#include <mmuko/context.h>
#include "mmuko_context_layout.h"

        .if MMUKO_CTX_SIZE > MMUKO_CONTEXT_MACHINE_BYTES
        .error "x86-64 context layout exceeds the opaque machine block"
        .endif
        .if MMUKO_CTX_FPUCW != MMUKO_CTX_MXCSR + 4
        .error "x87 control word must follow MXCSR"
        .endif

        .text
        .p2align 4
        .globl mmuko_context_switch
        .type mmuko_context_switch, @function
mmuko_context_switch:
        /* 1-4: save the running context into *from. */
        movq    (%rsp), %rax                    /* resume position = our return address */
        leaq    8(%rsp), %rdx                   /* rsp as the caller sees it after return */
        movq    %rax, MMUKO_CTX_RIP(%rdi)
        movq    %rdx, MMUKO_CTX_RSP(%rdi)
        movq    %rbx, MMUKO_CTX_RBX(%rdi)
        movq    %rbp, MMUKO_CTX_RBP(%rdi)
        movq    %r12, MMUKO_CTX_R12(%rdi)
        movq    %r13, MMUKO_CTX_R13(%rdi)
        movq    %r14, MMUKO_CTX_R14(%rdi)
        movq    %r15, MMUKO_CTX_R15(%rdi)
        stmxcsr MMUKO_CTX_MXCSR(%rdi)
        fnstcw  MMUKO_CTX_FPUCW(%rdi)

        /* 5-7: load *to - callee-saved registers, FP control, stack. */
        movq    MMUKO_CTX_RBX(%rsi), %rbx
        movq    MMUKO_CTX_RBP(%rsi), %rbp
        movq    MMUKO_CTX_R12(%rsi), %r12
        movq    MMUKO_CTX_R13(%rsi), %r13
        movq    MMUKO_CTX_R14(%rsi), %r14
        movq    MMUKO_CTX_R15(%rsi), %r15
        ldmxcsr MMUKO_CTX_MXCSR(%rsi)
        fldcw   MMUKO_CTX_FPUCW(%rsi)
        movq    MMUKO_CTX_RSP(%rsi), %rsp

        /* 8: continue where *to last yielded (or at mmuko_context_start). */
        jmpq    *MMUKO_CTX_RIP(%rsi)
        .size mmuko_context_switch, .-mmuko_context_switch

/* First instruction of every new context (seeded by mmuko_context_arch.c):
 *   rbx = mmuko_context *    r12 = entry    r13 = argument
 *   rsp = 16-byte aligned, so `call` enters entry with rsp == 8 (mod 16).
 */
        .p2align 4
        .globl mmuko_context_start
        .type mmuko_context_start, @function
mmuko_context_start:
        movq    %r13, %rdi
        callq   *%r12
        /* entry() returned: fall into the context-exit trampoline. */
        .size mmuko_context_start, .-mmuko_context_start

/* Context-exit trampoline. entry() preserved rbx (callee-saved), so it still
 * holds the context; rsp is back at its 16-byte aligned start value. Marks
 * the context TERMINATED and switches to its owner; never returns. */
        .globl mmuko_context_exit_trampoline
        .type mmuko_context_exit_trampoline, @function
mmuko_context_exit_trampoline:
        movq    %rbx, %rdi
        xorl    %esi, %esi
        callq   mmuko_context_exit@PLT
        ud2                                     /* unreachable: exit never returns */
        .size mmuko_context_exit_trampoline, .-mmuko_context_exit_trampoline

        .section .note.GNU-stack,"",@progbits
