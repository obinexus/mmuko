#include <mmuko/context.h>
#include <mmuko/kernel>

/* Execution-plane trampoline for Cluster A:
 *   mmuko_context_start -> mmuko_kernel_context_entry
 *     -> mmuko_kernel_entry_asm (ABI binding) -> mmuko_kernel_main
 * The result goes to the owner through mmuko_context_exit, so this frame never
 * returns into the start stub's exit path with a lost exit code. */
void mmuko_kernel_context_entry(void *argument)
{
    const mmuko_entry_context *context = (const mmuko_entry_context *)argument;
    int rc = mmuko_kernel_entry_asm(context);
    mmuko_context_exit(context->self, rc);
}
