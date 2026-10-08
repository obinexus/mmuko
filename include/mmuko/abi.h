#ifndef MMUKO_ABI_H
#define MMUKO_ABI_H

#include <stdint.h>

/* v2: the entry context carries an execution-context handle and a data-plane
 * endpoint instead of a POSIX file descriptor. */
#define MMUKO_ABI_VERSION 2u

typedef enum mmuko_cluster_id {
    MMUKO_CLUSTER_PROGRAM = 0,
    MMUKO_CLUSTER_A_KERNEL = 1,
    MMUKO_CLUSTER_B_MODULE = 2
} mmuko_cluster_id;

typedef enum mmuko_bipartite_color {
    MMUKO_COLOR_ZERO = 0,
    MMUKO_COLOR_ONE = 1
} mmuko_bipartite_color;

struct mmuko_context;
struct mmuko_channel_endpoint;

typedef struct mmuko_entry_context {
    uint32_t abi_version;
    mmuko_cluster_id cluster_id;
    uint32_t generation;
    /* Execution plane: the context this subsystem runs in and yields from.
     * NULL means run-to-completion (no scheduler, e.g. direct ABI tests). */
    struct mmuko_context *self;
    /* Data plane: adapter endpoint for wire records to the peer cluster.
     * NULL means no peer. */
    struct mmuko_channel_endpoint *peer;
} mmuko_entry_context;

typedef int (*mmuko_entry_fn)(const mmuko_entry_context *context);

const char *mmuko_cluster_name(mmuko_cluster_id cluster_id);

#endif
