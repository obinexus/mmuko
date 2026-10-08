/* x86-64 System V test probes for the context binding.
 *
 * uintptr_t mmuko_probe_caller_sp(void)
 *     The caller's rsp at the call site. The ABI requires it to be 16-byte
 *     aligned, so sampling it inside a context checks the context stack.
 *
 * uint64_t mmuko_probe_switch_preserves(mmuko_context *from, mmuko_context *to)
 *     Loads pattern A into every callee-saved register plus MXCSR / x87
 *     control, calls mmuko_context_switch directly (no C wrapper whose own
 *     prologue/epilogue could mask a broken switch), and on resumption
 *     returns a bitmask of what changed:
 *       bit0 rbx  bit1 rbp  bit2 r12  bit3 r13  bit4 r14  bit5 r15
 *       bit6 mxcsr  bit7 x87 control word
 *
 * void mmuko_probe_scrambler_entry(void *pair)    (context entry; never returns)
 *     pair = { self, peer, scrambler_mask, rounds }. Loads pattern B into the
 *     same state, switches to peer, and each time it is resumed verifies
 *     pattern B survived (OR-ing failures into scrambler_mask) and repeats.
 */
        .text

        .globl mmuko_probe_caller_sp
        .type mmuko_probe_caller_sp, @function
mmuko_probe_caller_sp:
        leaq    8(%rsp), %rax
        ret
        .size mmuko_probe_caller_sp, .-mmuko_probe_caller_sp

#define PATTERN_A_MXCSR 0x7F80          /* RC = toward zero */
#define PATTERN_A_FPUCW 0x0F7F          /* RC = toward zero */
#define PATTERN_B_MXCSR 0x5F80          /* RC = toward +inf */
#define PATTERN_B_FPUCW 0x0B7F          /* RC = toward +inf */

        .globl mmuko_probe_switch_preserves
        .type mmuko_probe_switch_preserves, @function
mmuko_probe_switch_preserves:
        pushq   %rbx
        pushq   %rbp
        pushq   %r12
        pushq   %r13
        pushq   %r14
        pushq   %r15
        subq    $40, %rsp               /* 8 + 48 + 40 = 96: rsp is 16-byte aligned */
        stmxcsr 0(%rsp)                 /* caller's FP control, restored on exit */
        fnstcw  4(%rsp)

        movabsq $0xA1A1A1A1A1A1A1A1, %rbx
        movabsq $0xA2A2A2A2A2A2A2A2, %rbp
        movabsq $0xA3A3A3A3A3A3A3A3, %r12
        movabsq $0xA4A4A4A4A4A4A4A4, %r13
        movabsq $0xA5A5A5A5A5A5A5A5, %r14
        movabsq $0xA6A6A6A6A6A6A6A6, %r15
        movl    $PATTERN_A_MXCSR, 8(%rsp)
        ldmxcsr 8(%rsp)
        movw    $PATTERN_A_FPUCW, 12(%rsp)
        fldcw   12(%rsp)

        callq   mmuko_context_switch@PLT        /* rdi = from, rsi = to (unchanged) */

        xorl    %eax, %eax
        movabsq $0xA1A1A1A1A1A1A1A1, %rcx
        cmpq    %rcx, %rbx
        je      1f
        orl     $0x01, %eax
1:      movabsq $0xA2A2A2A2A2A2A2A2, %rcx
        cmpq    %rcx, %rbp
        je      2f
        orl     $0x02, %eax
2:      movabsq $0xA3A3A3A3A3A3A3A3, %rcx
        cmpq    %rcx, %r12
        je      3f
        orl     $0x04, %eax
3:      movabsq $0xA4A4A4A4A4A4A4A4, %rcx
        cmpq    %rcx, %r13
        je      4f
        orl     $0x08, %eax
4:      movabsq $0xA5A5A5A5A5A5A5A5, %rcx
        cmpq    %rcx, %r14
        je      5f
        orl     $0x10, %eax
5:      movabsq $0xA6A6A6A6A6A6A6A6, %rcx
        cmpq    %rcx, %r15
        je      6f
        orl     $0x20, %eax
6:      stmxcsr 16(%rsp)
        cmpl    $PATTERN_A_MXCSR, 16(%rsp)
        je      7f
        orl     $0x40, %eax
7:      fnstcw  20(%rsp)
        cmpw    $PATTERN_A_FPUCW, 20(%rsp)
        je      8f
        orl     $0x80, %eax
8:
        ldmxcsr 0(%rsp)
        fldcw   4(%rsp)
        addq    $40, %rsp
        popq    %r15
        popq    %r14
        popq    %r13
        popq    %r12
        popq    %rbp
        popq    %rbx
        ret
        .size mmuko_probe_switch_preserves, .-mmuko_probe_switch_preserves

        .globl mmuko_probe_scrambler_entry
        .type mmuko_probe_scrambler_entry, @function
mmuko_probe_scrambler_entry:
        pushq   %rbx
        pushq   %rbp
        pushq   %r12
        pushq   %r13
        pushq   %r14
        pushq   %r15
        subq    $24, %rsp               /* 8 + 48 + 24 = 80: rsp is 16-byte aligned */
        movq    %rdi, 0(%rsp)           /* pair lives on this context's own stack */
.Lscramble:
        movabsq $0xB1B1B1B1B1B1B1B1, %rbx
        movabsq $0xB2B2B2B2B2B2B2B2, %rbp
        movabsq $0xB3B3B3B3B3B3B3B3, %r12
        movabsq $0xB4B4B4B4B4B4B4B4, %r13
        movabsq $0xB5B5B5B5B5B5B5B5, %r14
        movabsq $0xB6B6B6B6B6B6B6B6, %r15
        movl    $PATTERN_B_MXCSR, 8(%rsp)
        ldmxcsr 8(%rsp)
        movw    $PATTERN_B_FPUCW, 12(%rsp)
        fldcw   12(%rsp)
        movq    0(%rsp), %rax
        movq    0(%rax), %rdi           /* self */
        movq    8(%rax), %rsi           /* peer */
        callq   mmuko_context_switch@PLT

        xorl    %edx, %edx
        movabsq $0xB1B1B1B1B1B1B1B1, %rcx
        cmpq    %rcx, %rbx
        je      1f
        orl     $0x01, %edx
1:      movabsq $0xB2B2B2B2B2B2B2B2, %rcx
        cmpq    %rcx, %rbp
        je      2f
        orl     $0x02, %edx
2:      movabsq $0xB3B3B3B3B3B3B3B3, %rcx
        cmpq    %rcx, %r12
        je      3f
        orl     $0x04, %edx
3:      movabsq $0xB4B4B4B4B4B4B4B4, %rcx
        cmpq    %rcx, %r13
        je      4f
        orl     $0x08, %edx
4:      movabsq $0xB5B5B5B5B5B5B5B5, %rcx
        cmpq    %rcx, %r14
        je      5f
        orl     $0x10, %edx
5:      movabsq $0xB6B6B6B6B6B6B6B6, %rcx
        cmpq    %rcx, %r15
        je      6f
        orl     $0x20, %edx
6:      stmxcsr 16(%rsp)
        cmpl    $PATTERN_B_MXCSR, 16(%rsp)
        je      7f
        orl     $0x40, %edx
7:      fnstcw  20(%rsp)
        cmpw    $PATTERN_B_FPUCW, 20(%rsp)
        je      8f
        orl     $0x80, %edx
8:      movq    0(%rsp), %rax
        orq     %rdx, 16(%rax)          /* scrambler_mask */
        incq    24(%rax)                /* rounds */
        jmp     .Lscramble
        .size mmuko_probe_scrambler_entry, .-mmuko_probe_scrambler_entry

        .section .note.GNU-stack,"",@progbits
