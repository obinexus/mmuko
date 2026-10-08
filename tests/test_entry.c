#include <mmuko/kernel>
#include <mmuko/module>

#include <assert.h>
#include <stdio.h>

/* ABI-binding test: both assembly entry symbols reach their C mains. With no
 * execution context (self == NULL) and no peer, each main runs to
 * completion without yielding. */
int main(void)
{
    mmuko_entry_context kernel = {
        .abi_version = MMUKO_ABI_VERSION,
        .cluster_id = MMUKO_CLUSTER_A_KERNEL,
        .generation = 0,
        .self = NULL,
        .peer = NULL
    };
    mmuko_entry_context module = {
        .abi_version = MMUKO_ABI_VERSION,
        .cluster_id = MMUKO_CLUSTER_B_MODULE,
        .generation = 0,
        .self = NULL,
        .peer = NULL
    };
    mmuko_entry_context wrong = kernel;

    assert(mmuko_kernel_entry_asm(&kernel) == 0);
    assert(mmuko_module_entry_asm(&module) == 0);

    /* The structural contract is checked at the boundary. */
    wrong.abi_version = 1u;
    assert(mmuko_kernel_entry_asm(&wrong) == 64);
    wrong = module;
    wrong.cluster_id = MMUKO_CLUSTER_A_KERNEL;
    assert(mmuko_module_entry_asm(&wrong) == 64);
    puts("entry ABI bindings (<mmuko/kernel>, <mmuko/module>): HELD");
    return 0;
}
