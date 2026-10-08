#define _POSIX_C_SOURCE 200809L
#include <mmuko/channel.h>
#include <mmuko/context.h>
#include <mmuko/kernel.h>
#include <mmuko/module.h>
#include <mmuko/program.h>
#include <mmuko/scheduler.h>
#include <mmuko/stack.h>
#include <mmuko/topology.h>

#include "mmuko_supervisor.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Control plane. mmuko_program_main owns the topology and the scheduling of
 * the kernel and module execution contexts; it contains no kernel (boot/TUI)
 * or module (GUI/plug-in) logic. The only subsystem knowledge here is which
 * trampoline enters each cluster - the same knowledge the bootstrap layer
 * held before contexts existed. */

#define MMUKO_CONTEXT_STACK_BYTES (128u * 1024u)
#define MMUKO_CONTEXT_RESTART_BUDGET 1u
#define MMUKO_DOMAIN_RESTART_BUDGET 1u
#define MMUKO_DISPATCH_LIMIT 64u
#define MMUKO_INJECTED_STARTUP_FAULT 75

typedef enum mmuko_fault_plan {
    MMUKO_FAULT_NONE = 0,
    MMUKO_FAULT_KERNEL_CONTEXT,  /* context level: kernel generation 0 fails at start */
    MMUKO_FAULT_MODULE_CONTEXT,  /* context level: module generation 0 fails at start */
    MMUKO_FAULT_DOMAIN_CRASH     /* process level: the domain dies inside the module context */
} mmuko_fault_plan;

static mmuko_fault_plan fault_plan_from_environment(void)
{
    const char *requested = getenv("MMUKO_FAULT_INJECT");
    if (!requested) return MMUKO_FAULT_NONE;
    if (strcmp(requested, "kernel") == 0) return MMUKO_FAULT_KERNEL_CONTEXT;
    if (strcmp(requested, "module") == 0) return MMUKO_FAULT_MODULE_CONTEXT;
    if (strcmp(requested, "domain") == 0) return MMUKO_FAULT_DOMAIN_CRASH;
    return MMUKO_FAULT_NONE;
}

/* Control-plane fault entries. They run as the target context (on its stack)
 * but contain no subsystem logic: they stand in for a subsystem that failed
 * before doing any work. */
static void mmuko_program_startup_fault_entry(void *argument)
{
    const mmuko_entry_context *entry = (const mmuko_entry_context *)argument;
    printf("[program] injected startup fault cluster=%s generation=%u\n",
           mmuko_cluster_name(entry->cluster_id), entry->generation);
    mmuko_context_exit(entry->self, MMUKO_INJECTED_STARTUP_FAULT);
}

static void mmuko_program_crash_entry(void *argument)
{
    const mmuko_entry_context *entry = (const mmuko_entry_context *)argument;
    printf("[program] injected hard crash cluster=%s generation=%u: SIGKILL inside the context (process-level fault)\n",
           mmuko_cluster_name(entry->cluster_id), entry->generation);
    raise(SIGKILL);
    mmuko_context_exit(entry->self, 128 + SIGKILL);
}

static void program_trace(void *sink, const mmuko_scheduler_event *event)
{
    const char *role = mmuko_context_role_name(event->role);
    (void)sink;

    switch (event->kind) {
    case MMUKO_SCHEDULER_EVENT_REGISTERED:
        printf("[program] %s context READY cluster=%s generation=%u\n",
               role, mmuko_cluster_name(event->cluster), event->generation);
        break;
    case MMUKO_SCHEDULER_EVENT_TOPOLOGY:
        if (event->code == MMUKO_TOPOLOGY_OK)
            printf("[program] context topology valid: program=neutral, kernel=A, module=B, peer edge A<->B (bipartite)\n");
        else
            printf("[program] context topology invalid status=%d\n", event->code);
        break;
    case MMUKO_SCHEDULER_EVENT_DISPATCH:
        printf("[scheduler] %s -> %s\n", mmuko_context_role_name(event->from), role);
        break;
    case MMUKO_SCHEDULER_EVENT_TERMINATED:
        printf("[scheduler] %s context TERMINATED code=%d generation=%u\n", role, event->code, event->generation);
        break;
    case MMUKO_SCHEDULER_EVENT_FAULTED:
        printf("[scheduler] %s context FAULTED code=%d generation=%u; it will not be resumed\n",
               role, event->code, event->generation);
        break;
    case MMUKO_SCHEDULER_EVENT_RECREATED:
        printf("[scheduler] cluster=%s recreating %s context generation=%u (restart %u/%u)\n",
               mmuko_cluster_name(event->cluster), role, event->generation,
               event->restarts, event->restart_budget);
        break;
    case MMUKO_SCHEDULER_EVENT_ABANDONED:
        printf("[scheduler] %s context restart budget exhausted; cluster=%s stays isolated\n",
               role, mmuko_cluster_name(event->cluster));
        break;
    case MMUKO_SCHEDULER_EVENT_REJECTED:
        printf("[scheduler] resume of %s context refused status=%d\n", role, event->code);
        break;
    case MMUKO_SCHEDULER_EVENT_IDLE:
        printf("[scheduler] no resumable contexts remain; control returns to program\n");
        break;
    }
}

/* Runs in the execution-domain process: the one scheduling domain in which
 * the program, kernel and module contexts share an address space. */
static int run_execution_domain(uint32_t generation, int ready_fd)
{
    mmuko_scheduler scheduler;
    mmuko_channel channel;
    mmuko_stack_region kernel_stack = {0};
    mmuko_stack_region module_stack = {0};
    mmuko_fault_plan plan = generation == 0u ? fault_plan_from_environment() : MMUKO_FAULT_NONE;
    int rc;

    printf("[program] execution domain node=2 parent=1 depth=2 color=%d pid=%ld generation=%u\n",
           (int)mmuko_topology_color_for_depth(2), (long)getpid(), generation);

    if (mmuko_stack_allocate(&kernel_stack, MMUKO_CONTEXT_STACK_BYTES) != 0 ||
        mmuko_stack_allocate(&module_stack, MMUKO_CONTEXT_STACK_BYTES) != 0) {
        perror("mmuko_stack_allocate");
        mmuko_stack_release(&kernel_stack);
        mmuko_stack_release(&module_stack);
        mmuko_supervisor_close_ready(ready_fd);
        return 70;
    }

    mmuko_channel_init(&channel, MMUKO_CLUSTER_A_KERNEL, MMUKO_CLUSTER_B_MODULE);

    const mmuko_context_registration kernel = {
        .role = MMUKO_CONTEXT_KERNEL,
        .cluster = MMUKO_CLUSTER_A_KERNEL,
        .entry = mmuko_kernel_context_entry,
        .stack = kernel_stack.base,
        .stack_size = kernel_stack.size,
        .peer = mmuko_channel_endpoint_of(&channel, MMUKO_CLUSTER_A_KERNEL),
        .restart_budget = MMUKO_CONTEXT_RESTART_BUDGET,
        .fault_entry = plan == MMUKO_FAULT_KERNEL_CONTEXT ? mmuko_program_startup_fault_entry : NULL,
        .fault_generation = 0
    };
    const mmuko_context_registration module = {
        .role = MMUKO_CONTEXT_MODULE,
        .cluster = MMUKO_CLUSTER_B_MODULE,
        .entry = mmuko_module_context_entry,
        .stack = module_stack.base,
        .stack_size = module_stack.size,
        .peer = mmuko_channel_endpoint_of(&channel, MMUKO_CLUSTER_B_MODULE),
        .restart_budget = MMUKO_CONTEXT_RESTART_BUDGET,
        .fault_entry = plan == MMUKO_FAULT_MODULE_CONTEXT ? mmuko_program_startup_fault_entry
                     : plan == MMUKO_FAULT_DOMAIN_CRASH ? mmuko_program_crash_entry
                     : NULL,
        .fault_generation = 0
    };

    /* Adopts this thread as the program context; kernel and module each get
     * their own guarded stack - never the program stack. */
    if (mmuko_scheduler_init(&scheduler, program_trace, NULL) != MMUKO_SCHEDULER_OK) {
        rc = 71;
        goto out;
    }
    if (mmuko_scheduler_register(&scheduler, &kernel) != MMUKO_SCHEDULER_OK ||
        mmuko_supervisor_signal_ready(ready_fd, MMUKO_CLUSTER_A_KERNEL) != 0 ||
        mmuko_scheduler_register(&scheduler, &module) != MMUKO_SCHEDULER_OK ||
        mmuko_supervisor_signal_ready(ready_fd, MMUKO_CLUSTER_B_MODULE) != 0) {
        rc = 72;
        goto out;
    }
    mmuko_supervisor_close_ready(ready_fd);
    ready_fd = -1;

    if (mmuko_scheduler_validate(&scheduler) != MMUKO_SCHEDULER_OK) {
        rc = 69;
        goto out;
    }

    rc = mmuko_scheduler_run(&scheduler, MMUKO_DISPATCH_LIMIT);
    printf("[program] execution domain generation=%u finished kernel=%s module=%s dispatches=%u status=%d\n",
           generation,
           mmuko_context_state_name(scheduler.kernel.state),
           mmuko_context_state_name(scheduler.module.state),
           scheduler.dispatches, rc);
    if (rc < 0) rc = 73;
    else if (rc > 255) rc = 1;

out:
    mmuko_supervisor_close_ready(ready_fd);
    mmuko_stack_release(&kernel_stack);
    mmuko_stack_release(&module_stack);
    return rc;
}

int mmuko_program_main(void)
{
    /* Process-isolation tree. Context topology is validated separately, in
     * the domain, once the contexts actually exist. */
    const mmuko_process_node processes[] = {
        {0, 0, 0, MMUKO_COLOR_ZERO, MMUKO_CLUSTER_PROGRAM},
        {1, 0, 1, MMUKO_COLOR_ONE,  MMUKO_CLUSTER_PROGRAM},
        {2, 1, 2, MMUKO_COLOR_ZERO, MMUKO_CLUSTER_PROGRAM}
    };
    const mmuko_supervisor_spec supervisor = {
        .domain = run_execution_domain,
        .restart_budget = MMUKO_DOMAIN_RESTART_BUDGET
    };
    int rc;

    printf("[program] bootstrap root node=0 depth=0 color=0 pid=%ld\n", (long)getpid());
    if (mmuko_topology_validate_tree(processes, sizeof processes / sizeof processes[0]) != 0) {
        fputs("[program] invalid process topology\n", stderr);
        return 69;
    }

    rc = mmuko_supervisor_launch(&supervisor);
    if (rc != 0) return rc;

    printf("[program] cluster A registered -> mmuko_kernel_main; cluster B registered -> mmuko_module_main\n");
    printf("[program] execution domain ready; bootstrap parent retires now\n");

    /* Parent retirement is deliberate: the supervisor owns process restarts
     * and the domain's scheduler owns context switching. */
    _exit(0);
}
