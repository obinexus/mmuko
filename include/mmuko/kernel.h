#ifndef MMUKO_KERNEL_H
#define MMUKO_KERNEL_H

#include <mmuko/abi.h>
#include <mmuko/system.h>

/* Cluster A: kernel/TUI concern. */
int mmuko_kernel_main(const mmuko_entry_context *context);
int mmuko_kernel_entry_asm(const mmuko_entry_context *context);

/* Execution-context trampoline for Cluster A. argument is the
 * const mmuko_entry_context * owned by the scheduler. Runs on the kernel
 * context's own stack, enters mmuko_kernel_main once, and reports its result
 * through mmuko_context_exit. Never returns. */
void mmuko_kernel_context_entry(void *argument);

#endif
