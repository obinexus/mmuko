#ifndef MMUKO_STACK_H
#define MMUKO_STACK_H

/* Platform service (POSIX): execution-context stacks.
 *
 * Each stack is its own anonymous mapping with a PROT_NONE guard page below
 * it, so an overflow faults immediately instead of silently running into
 * another context's stack. Used by the control plane only. */

#include <stddef.h>

typedef struct mmuko_stack_region {
    void *mapping;
    size_t mapping_size;
    void *base;     /* lowest usable byte, directly above the guard page */
    size_t size;    /* usable bytes */
} mmuko_stack_region;

int mmuko_stack_allocate(mmuko_stack_region *region, size_t size);
void mmuko_stack_release(mmuko_stack_region *region);

#endif
