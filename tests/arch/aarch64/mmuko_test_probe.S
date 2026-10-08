/* AArch64 AAPCS64 test probes for the context binding.
 *
 * uintptr_t mmuko_probe_caller_sp(void)
 *     The caller's sp (bl does not move sp). AAPCS64 requires 16-byte
 *     alignment at all times.
 *
 * uint64_t mmuko_probe_switch_preserves(mmuko_context *from, mmuko_context *to)
 *     Loads pattern A into x19-x29, d8-d15 and FPCR, calls
 *     mmuko_context_switch directly, and on resumption returns a bitmask of
 *     what changed:
 *       bits 0-9 x19-x28   bit10 x29   bits 11-18 d8-d15   bit19 fpcr
 *
 * void mmuko_probe_scrambler_entry(void *pair)    (context entry; never returns)
 *     pair = { self, peer, scrambler_mask, rounds }. Loads pattern B into the
 *     same state, switches to peer, and each time it is resumed verifies
 *     pattern B survived (OR-ing failures into scrambler_mask) and repeats.
 */
        .text

        .globl mmuko_probe_caller_sp
        .type mmuko_probe_caller_sp, %function
mmuko_probe_caller_sp:
        mov     x0, sp
        ret
        .size mmuko_probe_caller_sp, .-mmuko_probe_caller_sp

#define FPCR_PATTERN_A 0xC00000         /* RMode = toward zero */
#define FPCR_PATTERN_B 0x400000         /* RMode = toward +inf */

/* Load a 64-bit byte-repeated pattern into the given registers. */
        .macro load_pattern prefix
        ldr     x19, =0x\prefix\()1\prefix\()1\prefix\()1\prefix\()1\prefix\()1\prefix\()1\prefix\()1\prefix\()1
        ldr     x20, =0x\prefix\()2\prefix\()2\prefix\()2\prefix\()2\prefix\()2\prefix\()2\prefix\()2\prefix\()2
        ldr     x21, =0x\prefix\()3\prefix\()3\prefix\()3\prefix\()3\prefix\()3\prefix\()3\prefix\()3\prefix\()3
        ldr     x22, =0x\prefix\()4\prefix\()4\prefix\()4\prefix\()4\prefix\()4\prefix\()4\prefix\()4\prefix\()4
        ldr     x23, =0x\prefix\()5\prefix\()5\prefix\()5\prefix\()5\prefix\()5\prefix\()5\prefix\()5\prefix\()5
        ldr     x24, =0x\prefix\()6\prefix\()6\prefix\()6\prefix\()6\prefix\()6\prefix\()6\prefix\()6\prefix\()6
        ldr     x25, =0x\prefix\()7\prefix\()7\prefix\()7\prefix\()7\prefix\()7\prefix\()7\prefix\()7\prefix\()7
        ldr     x26, =0x\prefix\()8\prefix\()8\prefix\()8\prefix\()8\prefix\()8\prefix\()8\prefix\()8\prefix\()8
        ldr     x27, =0x\prefix\()9\prefix\()9\prefix\()9\prefix\()9\prefix\()9\prefix\()9\prefix\()9\prefix\()9
        ldr     x28, =0x\prefix\()A\prefix\()A\prefix\()A\prefix\()A\prefix\()A\prefix\()A\prefix\()A\prefix\()A
        ldr     x29, =0x\prefix\()B\prefix\()B\prefix\()B\prefix\()B\prefix\()B\prefix\()B\prefix\()B\prefix\()B
        ldr     x9,  =0x\prefix\()C\prefix\()C\prefix\()C\prefix\()C\prefix\()C\prefix\()C\prefix\()C\prefix\()C
        fmov    d8, x9
        add     x9, x9, #1
        fmov    d9, x9
        add     x9, x9, #1
        fmov    d10, x9
        add     x9, x9, #1
        fmov    d11, x9
        add     x9, x9, #1
        fmov    d12, x9
        add     x9, x9, #1
        fmov    d13, x9
        add     x9, x9, #1
        fmov    d14, x9
        add     x9, x9, #1
        fmov    d15, x9
        .endm

/* Compare one general register against its pattern; set bit in x0 on mismatch. */
        .macro check_gpr reg, value, bit
        ldr     x9, =\value
        cmp     \reg, x9
        b.eq    1f
        orr     x0, x0, #(1 << \bit)
1:
        .endm

/* Compare d registers against base, base+1, ... ; bits start at 11. */
        .macro check_fpr base
        ldr     x10, =\base
        fmov    x9, d8
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 11)
1:      add     x10, x10, #1
        fmov    x9, d9
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 12)
1:      add     x10, x10, #1
        fmov    x9, d10
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 13)
1:      add     x10, x10, #1
        fmov    x9, d11
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 14)
1:      add     x10, x10, #1
        fmov    x9, d12
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 15)
1:      add     x10, x10, #1
        fmov    x9, d13
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 16)
1:      add     x10, x10, #1
        fmov    x9, d14
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 17)
1:      add     x10, x10, #1
        fmov    x9, d15
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 18)
1:
        .endm

        .macro check_all prefix, fpcr
        mov     x0, #0
        check_gpr x19, 0x\prefix\()1\prefix\()1\prefix\()1\prefix\()1\prefix\()1\prefix\()1\prefix\()1\prefix\()1, 0
        check_gpr x20, 0x\prefix\()2\prefix\()2\prefix\()2\prefix\()2\prefix\()2\prefix\()2\prefix\()2\prefix\()2, 1
        check_gpr x21, 0x\prefix\()3\prefix\()3\prefix\()3\prefix\()3\prefix\()3\prefix\()3\prefix\()3\prefix\()3, 2
        check_gpr x22, 0x\prefix\()4\prefix\()4\prefix\()4\prefix\()4\prefix\()4\prefix\()4\prefix\()4\prefix\()4, 3
        check_gpr x23, 0x\prefix\()5\prefix\()5\prefix\()5\prefix\()5\prefix\()5\prefix\()5\prefix\()5\prefix\()5, 4
        check_gpr x24, 0x\prefix\()6\prefix\()6\prefix\()6\prefix\()6\prefix\()6\prefix\()6\prefix\()6\prefix\()6, 5
        check_gpr x25, 0x\prefix\()7\prefix\()7\prefix\()7\prefix\()7\prefix\()7\prefix\()7\prefix\()7\prefix\()7, 6
        check_gpr x26, 0x\prefix\()8\prefix\()8\prefix\()8\prefix\()8\prefix\()8\prefix\()8\prefix\()8\prefix\()8, 7
        check_gpr x27, 0x\prefix\()9\prefix\()9\prefix\()9\prefix\()9\prefix\()9\prefix\()9\prefix\()9\prefix\()9, 8
        check_gpr x28, 0x\prefix\()A\prefix\()A\prefix\()A\prefix\()A\prefix\()A\prefix\()A\prefix\()A\prefix\()A, 9
        check_gpr x29, 0x\prefix\()B\prefix\()B\prefix\()B\prefix\()B\prefix\()B\prefix\()B\prefix\()B\prefix\()B, 10
        check_fpr 0x\prefix\()C\prefix\()C\prefix\()C\prefix\()C\prefix\()C\prefix\()C\prefix\()C\prefix\()C
        mrs     x9, fpcr
        mov     x10, #\fpcr
        cmp     x9, x10
        b.eq    1f
        orr     x0, x0, #(1 << 19)
1:
        .endm

/* Frame shared by both probes (176 bytes, 16-byte aligned):
 *   [sp,   0] x29, x30     [sp,  16] x19, x20   ...   [sp,  80] x27, x28
 *   [sp,  96] fpcr         [sp, 104] pair / scratch
 *   [sp, 112] d8, d9  ...  [sp, 160] d14, d15
 */
        .macro save_frame
        stp     x29, x30, [sp, #-176]!
        mov     x29, sp
        stp     x19, x20, [sp, #16]
        stp     x21, x22, [sp, #32]
        stp     x23, x24, [sp, #48]
        stp     x25, x26, [sp, #64]
        stp     x27, x28, [sp, #80]
        mrs     x9, fpcr
        str     x9, [sp, #96]
        stp     d8,  d9,  [sp, #112]
        stp     d10, d11, [sp, #128]
        stp     d12, d13, [sp, #144]
        stp     d14, d15, [sp, #160]
        .endm

        .globl mmuko_probe_switch_preserves
        .type mmuko_probe_switch_preserves, %function
mmuko_probe_switch_preserves:
        save_frame
        load_pattern A
        mov     x9, #FPCR_PATTERN_A
        msr     fpcr, x9

        bl      mmuko_context_switch    /* x0 = from, x1 = to (unchanged) */

        check_all A, FPCR_PATTERN_A

        ldr     x9, [sp, #96]
        msr     fpcr, x9
        ldp     d8,  d9,  [sp, #112]
        ldp     d10, d11, [sp, #128]
        ldp     d12, d13, [sp, #144]
        ldp     d14, d15, [sp, #160]
        ldp     x19, x20, [sp, #16]
        ldp     x21, x22, [sp, #32]
        ldp     x23, x24, [sp, #48]
        ldp     x25, x26, [sp, #64]
        ldp     x27, x28, [sp, #80]
        ldp     x29, x30, [sp], #176
        ret
        .size mmuko_probe_switch_preserves, .-mmuko_probe_switch_preserves
        .ltorg

        .globl mmuko_probe_scrambler_entry
        .type mmuko_probe_scrambler_entry, %function
mmuko_probe_scrambler_entry:
        save_frame
        str     x0, [sp, #104]          /* pair lives on this context's own stack */
.Lscramble:
        load_pattern B
        mov     x9, #FPCR_PATTERN_B
        msr     fpcr, x9
        ldr     x9, [sp, #104]
        ldr     x0, [x9, #0]            /* self */
        ldr     x1, [x9, #8]            /* peer */
        bl      mmuko_context_switch

        check_all B, FPCR_PATTERN_B
        ldr     x9, [sp, #104]
        ldr     x10, [x9, #16]
        orr     x10, x10, x0
        str     x10, [x9, #16]          /* scrambler_mask */
        ldr     x10, [x9, #24]
        add     x10, x10, #1
        str     x10, [x9, #24]          /* rounds */
        b       .Lscramble
        .size mmuko_probe_scrambler_entry, .-mmuko_probe_scrambler_entry
        .ltorg

        .section .note.GNU-stack,"",%progbits
