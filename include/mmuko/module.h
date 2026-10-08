#ifndef MMUKO_MODULE_H
#define MMUKO_MODULE_H

#include <stdint.h>
#include <mmuko/abi.h>

typedef struct mmuko_graphics_module {
    uint32_t abi_version;
    const char *name;
    const char *ontology;
    int (*start)(const mmuko_entry_context *context);
    int (*render)(const mmuko_entry_context *context, uint32_t frame);
} mmuko_graphics_module;

/* Cluster B: module/GUI concern. */
int mmuko_module_main(const mmuko_entry_context *context);
int mmuko_module_entry_asm(const mmuko_entry_context *context);
const mmuko_graphics_module *mmuko_module_resolve_graphics(const char *name);

/* Execution-context trampoline for Cluster B. argument is the
 * const mmuko_entry_context * owned by the scheduler. Runs on the module
 * context's own stack, enters mmuko_module_main once, and reports its result
 * through mmuko_context_exit. Never returns. */
void mmuko_module_context_entry(void *argument);

#endif
