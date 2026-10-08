#include <mmuko/context.h>

#include "mmuko_context_arch.h"
#include "mmuko_context_layout.h"

#include <stddef.h>

/* Compile-time ABI contract between this file, the C structure and
 * mmuko_context_switch.S. If any of these drift, the build fails. */
_Static_assert(sizeof(void *) == 8 && sizeof(uintptr_t) == 8, "AAPCS64 binding requires LP64");
_Static_assert(offsetof(mmuko_context, machine) == 0, "assembly addresses the machine block at offset 0");
_Static_assert(sizeof(mmuko_context_machine) == MMUKO_CONTEXT_MACHINE_BYTES, "opaque machine block size");
_Static_assert(MMUKO_CTX_SIZE <= MMUKO_CONTEXT_MACHINE_BYTES, "AArch64 layout exceeds the machine block");
_Static_assert(_Alignof(mmuko_context) >= 16, "context must be 16-byte aligned");
/* stp/ldp pairs in the assembly require adjacent 8-byte slots. */
_Static_assert(MMUKO_CTX_X20 == MMUKO_CTX_X19 + 8 && MMUKO_CTX_X21 == MMUKO_CTX_X19 + 16 &&
               MMUKO_CTX_X23 == MMUKO_CTX_X21 + 16 && MMUKO_CTX_X25 == MMUKO_CTX_X23 + 16 &&
               MMUKO_CTX_X27 == MMUKO_CTX_X25 + 16 && MMUKO_CTX_X28 == MMUKO_CTX_X27 + 8 &&
               MMUKO_CTX_X29 == MMUKO_CTX_X27 + 16 && MMUKO_CTX_X30 == MMUKO_CTX_X29 + 8,
               "general-register pairs must be contiguous");
_Static_assert(MMUKO_CTX_D10 == MMUKO_CTX_D8 + 16 && MMUKO_CTX_D12 == MMUKO_CTX_D10 + 16 &&
               MMUKO_CTX_D14 == MMUKO_CTX_D12 + 16 && MMUKO_CTX_FPCR >= MMUKO_CTX_D14 + 16,
               "d8-d15 pairs must be contiguous");
_Static_assert(MMUKO_CTX_SP % 8 == 0 && MMUKO_CTX_PC % 8 == 0 && MMUKO_CTX_FPCR % 8 == 0,
               "64-bit slots must be 8-byte aligned");
/* stp/ldp signed offsets are limited to [-512, 504]. */
_Static_assert(MMUKO_CTX_D14 + 8 <= 504, "pair offsets must fit the stp/ldp immediate range");

static void put_slot(mmuko_context *context, unsigned offset, uint64_t value)
{
    context->machine.words[offset / 8u] = value;
}

static uint64_t get_slot(const mmuko_context *context, unsigned offset)
{
    return context->machine.words[offset / 8u];
}

void mmuko_context_arch_prepare(mmuko_context *context, uintptr_t stack_top)
{
    /* sp must stay 16-byte aligned at all times under AAPCS64; stack_top
     * already is. x29 = 0 terminates the frame-record chain. The machine
     * block was cleared by mmuko_context_reset, so d8-d15 and FPCR start at
     * zero (round-to-nearest, no flush-to-zero), matching process start. */
    put_slot(context, MMUKO_CTX_SP, stack_top);
    put_slot(context, MMUKO_CTX_PC, (uintptr_t)&mmuko_context_start);
    put_slot(context, MMUKO_CTX_X29, 0);
    put_slot(context, MMUKO_CTX_X30, 0);
    put_slot(context, MMUKO_CTX_SEED_CONTEXT, (uintptr_t)context);
    put_slot(context, MMUKO_CTX_SEED_ENTRY, (uintptr_t)context->entry);
    put_slot(context, MMUKO_CTX_SEED_ARGUMENT, (uintptr_t)context->argument);
    put_slot(context, MMUKO_CTX_FPCR, 0);
}

uintptr_t mmuko_context_arch_saved_sp(const mmuko_context *context)
{
    return (uintptr_t)get_slot(context, MMUKO_CTX_SP);
}

uintptr_t mmuko_context_arch_saved_pc(const mmuko_context *context)
{
    return (uintptr_t)get_slot(context, MMUKO_CTX_PC);
}
