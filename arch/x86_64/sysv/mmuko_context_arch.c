#include <mmuko/context.h>

#include "mmuko_context_arch.h"
#include "mmuko_context_layout.h"

#include <stddef.h>

/* Compile-time ABI contract between this file, the C structure and
 * mmuko_context_switch.S. If any of these drift, the build fails. */
_Static_assert(sizeof(void *) == 8 && sizeof(uintptr_t) == 8, "x86-64 System V binding requires LP64");
_Static_assert(offsetof(mmuko_context, machine) == 0, "assembly addresses the machine block at offset 0");
_Static_assert(sizeof(mmuko_context_machine) == MMUKO_CONTEXT_MACHINE_BYTES, "opaque machine block size");
_Static_assert(MMUKO_CTX_SIZE <= MMUKO_CONTEXT_MACHINE_BYTES, "x86-64 layout exceeds the machine block");
_Static_assert(_Alignof(mmuko_context) >= 16, "context must be 16-byte aligned");
_Static_assert(MMUKO_CTX_RSP % 8 == 0 && MMUKO_CTX_RIP % 8 == 0 && MMUKO_CTX_RBX % 8 == 0 &&
               MMUKO_CTX_RBP % 8 == 0 && MMUKO_CTX_R12 % 8 == 0 && MMUKO_CTX_R13 % 8 == 0 &&
               MMUKO_CTX_R14 % 8 == 0 && MMUKO_CTX_R15 % 8 == 0 && MMUKO_CTX_MXCSR % 8 == 0,
               "64-bit slots must be 8-byte aligned");
_Static_assert(MMUKO_CTX_FPUCW == MMUKO_CTX_MXCSR + 4, "x87 control word sits in the upper half of the MXCSR slot");
_Static_assert(MMUKO_CTX_MXCSR >= MMUKO_CTX_R15 + 8, "FP control follows the integer slots");

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
    /* stack_top is 16-byte aligned. Reserve two zero words: rsp stays
     * 16-byte aligned at mmuko_context_start, so its `call *%r12` enters
     * entry() with rsp == 8 (mod 16) exactly as the ABI requires, and a
     * frame walk that reaches the top finds a null return address. */
    uintptr_t rsp = stack_top - 16u;
    uint64_t *frame = (uint64_t *)rsp;

    frame[0] = 0;
    frame[1] = 0;

    put_slot(context, MMUKO_CTX_RSP, rsp);
    put_slot(context, MMUKO_CTX_RIP, (uintptr_t)&mmuko_context_start);
    put_slot(context, MMUKO_CTX_RBP, 0);
    put_slot(context, MMUKO_CTX_SEED_CONTEXT, (uintptr_t)context);
    put_slot(context, MMUKO_CTX_SEED_ENTRY, (uintptr_t)context->entry);
    put_slot(context, MMUKO_CTX_SEED_ARGUMENT, (uintptr_t)context->argument);
    put_slot(context, MMUKO_CTX_R14, 0);
    put_slot(context, MMUKO_CTX_R15, 0);
    /* Little-endian: MXCSR in bytes 64..67, x87 control word in 68..69. */
    put_slot(context, MMUKO_CTX_MXCSR,
             (uint64_t)MMUKO_CTX_DEFAULT_MXCSR | ((uint64_t)MMUKO_CTX_DEFAULT_FPUCW << 32));
}

uintptr_t mmuko_context_arch_saved_sp(const mmuko_context *context)
{
    return (uintptr_t)get_slot(context, MMUKO_CTX_RSP);
}

uintptr_t mmuko_context_arch_saved_pc(const mmuko_context *context)
{
    return (uintptr_t)get_slot(context, MMUKO_CTX_RIP);
}
