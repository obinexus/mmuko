#ifndef MMUKO_SCHEDULER_H
#define MMUKO_SCHEDULER_H

/* Control plane: the neutral context scheduler owned by mmuko_program_main.
 *
 * It owns the three execution contexts (program, kernel, module), decides who
 * runs next, and applies the context-level fault policy. It contains no
 * kernel or module semantics. Kernel and module code never include this
 * header (enforced by `make separation-test`).
 *
 * Freestanding: progress is reported as structured events through a trace
 * callback; the scheduler itself performs no I/O. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <mmuko/abi.h>
#include <mmuko/context.h>

#define MMUKO_SCHEDULER_EXECUTION_SLOTS 2u

typedef enum mmuko_scheduler_status {
    MMUKO_SCHEDULER_OK = 0,
    MMUKO_SCHEDULER_EINVAL = -1,
    MMUKO_SCHEDULER_ESTATE = -2,
    MMUKO_SCHEDULER_ETOPOLOGY = -3,
    MMUKO_SCHEDULER_ELIMIT = -4
} mmuko_scheduler_status;

typedef enum mmuko_scheduler_event_kind {
    MMUKO_SCHEDULER_EVENT_REGISTERED = 0, /* context READY */
    MMUKO_SCHEDULER_EVENT_TOPOLOGY,       /* code = mmuko_topology_status */
    MMUKO_SCHEDULER_EVENT_DISPATCH,       /* from -> role */
    MMUKO_SCHEDULER_EVENT_TERMINATED,
    MMUKO_SCHEDULER_EVENT_FAULTED,        /* code = exit code */
    MMUKO_SCHEDULER_EVENT_RECREATED,      /* new generation READY */
    MMUKO_SCHEDULER_EVENT_ABANDONED,      /* restart budget exhausted */
    MMUKO_SCHEDULER_EVENT_REJECTED,       /* resume refused; code = mmuko_context_status */
    MMUKO_SCHEDULER_EVENT_IDLE            /* no resumable context remains */
} mmuko_scheduler_event_kind;

typedef struct mmuko_scheduler_event {
    mmuko_scheduler_event_kind kind;
    mmuko_context_role role;
    mmuko_context_role from;
    mmuko_cluster_id cluster;
    uint32_t generation;
    uint32_t restarts;
    uint32_t restart_budget;
    int code;
} mmuko_scheduler_event;

typedef void (*mmuko_scheduler_trace_fn)(void *sink, const mmuko_scheduler_event *event);

/* Everything the control plane needs to create (and re-create) one
 * execution context. The stack memory stays owned by the caller. */
typedef struct mmuko_context_registration {
    mmuko_context_role role;
    mmuko_cluster_id cluster;
    mmuko_context_entry_fn entry;       /* e.g. mmuko_kernel_context_entry */
    void *stack;
    size_t stack_size;
    struct mmuko_channel_endpoint *peer;
    uint32_t restart_budget;
    /* Optional control-plane fault injection: for generation fault_generation
     * the context starts in fault_entry instead of entry. */
    mmuko_context_entry_fn fault_entry;
    uint32_t fault_generation;
} mmuko_context_registration;

typedef struct mmuko_scheduler_slot {
    mmuko_context *context;
    mmuko_context_role role;
    mmuko_cluster_id cluster;
    mmuko_context_entry_fn entry;
    mmuko_context_entry_fn fault_entry;
    uint32_t fault_generation;
    void *stack;
    size_t stack_size;
    mmuko_entry_context entry_context;  /* what the subsystem sees */
    uint32_t restart_budget;
    uint32_t restarts;
    bool registered;
    bool abandoned;
} mmuko_scheduler_slot;

typedef struct mmuko_scheduler {
    mmuko_context program;
    mmuko_context kernel;
    mmuko_context module;
    mmuko_context *current;
    mmuko_scheduler_slot slots[MMUKO_SCHEDULER_EXECUTION_SLOTS];
    mmuko_context_role last;
    bool topology_valid;
    uint32_t dispatches;
    mmuko_scheduler_trace_fn trace;
    void *trace_sink;
} mmuko_scheduler;

/* Adopt the calling thread of control as the program context. */
int mmuko_scheduler_init(mmuko_scheduler *scheduler, mmuko_scheduler_trace_fn trace, void *trace_sink);

/* Create the kernel or module context on its own stack (state READY). */
int mmuko_scheduler_register(mmuko_scheduler *scheduler, const mmuko_context_registration *registration);

/* Build the context topology from the registered state and validate it. */
int mmuko_scheduler_validate(mmuko_scheduler *scheduler);

/* Round-robin dispatch until no execution context is resumable.
 * Returns 0 when every context TERMINATED, the exit code of the first
 * context that stayed FAULTED, or a negative mmuko_scheduler_status. */
int mmuko_scheduler_run(mmuko_scheduler *scheduler, uint32_t max_dispatches);

const mmuko_scheduler_slot *mmuko_scheduler_slot_of(const mmuko_scheduler *scheduler, mmuko_context_role role);

#endif
