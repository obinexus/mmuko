#ifndef MMUKO_SUPERVISOR_H
#define MMUKO_SUPERVISOR_H

/* Process-isolation layer (POSIX, private to the program layer).
 *
 * This is the OS-level fault boundary: fork, wait, restart. It never touches
 * execution contexts - all contexts live inside one execution-domain process
 * and are switched there by the context scheduler. The supervisor only sees
 * whether that process exited cleanly.
 *
 *   node 0  bootstrap   (mmuko_program_main; retires after hand-off)
 *     node 1  supervisor  (owns the domain restart budget)
 *       node 2  execution domain  (program + kernel + module contexts)
 */

#include <stdint.h>
#include <mmuko/abi.h>

/* Runs inside the execution-domain process. ready_fd is valid only for the
 * first generation; the domain signals each registered cluster on it. */
typedef int (*mmuko_domain_fn)(uint32_t generation, int ready_fd);

typedef struct mmuko_supervisor_spec {
    mmuko_domain_fn domain;
    uint32_t restart_budget;
} mmuko_supervisor_spec;

/* Fork the supervisor, which forks the domain. Returns 0 in the bootstrap
 * once the domain has signalled both clusters; non-zero on failure. */
int mmuko_supervisor_launch(const mmuko_supervisor_spec *spec);

/* Domain side of the hand-off. */
int mmuko_supervisor_signal_ready(int ready_fd, mmuko_cluster_id cluster);
void mmuko_supervisor_close_ready(int ready_fd);

#endif
