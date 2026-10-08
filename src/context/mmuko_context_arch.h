#ifndef MMUKO_CONTEXT_ARCH_H
#define MMUKO_CONTEXT_ARCH_H

/* Private boundary between the portable context core and one ABI binding.
 * Implemented in arch/<arch>/<abi>/mmuko_context_arch.c and
 * arch/<arch>/<abi>/mmuko_context_switch.S. Never included by kernel or
 * module code (separation-test). */

#include <mmuko/context.h>

/* Seed a READY context so that its first switch-in enters
 * mmuko_context_start on a fresh, ABI-aligned stack ending at stack_top. */
void mmuko_context_arch_prepare(mmuko_context *context, uintptr_t stack_top);

uintptr_t mmuko_context_arch_saved_sp(const mmuko_context *context);
uintptr_t mmuko_context_arch_saved_pc(const mmuko_context *context);

/* Assembly: first instruction of every new context. Calls entry(argument)
 * and falls through into the context-exit trampoline. Never called from C. */
void mmuko_context_start(void);

#endif
