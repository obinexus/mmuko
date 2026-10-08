#include <mmuko/channel.h>
#include <mmuko/scheduler.h>
#include <mmuko/topology.h>

/* Control plane. Decides which execution context owns the CPU next and what
 * happens when one faults. It never calls mmuko_kernel_main or
 * mmuko_module_main: it only resumes contexts whose entry trampolines were
 * supplied at registration. Freestanding (no libc). */

static void emit(const mmuko_scheduler *scheduler,
                 mmuko_scheduler_event_kind kind,
                 const mmuko_scheduler_slot *slot,
                 mmuko_context_role from,
                 int code)
{
    mmuko_scheduler_event event;

    if (!scheduler->trace) return;
    event.kind = kind;
    event.role = slot ? slot->role : MMUKO_CONTEXT_PROGRAM;
    event.from = from;
    event.cluster = slot ? slot->cluster : MMUKO_CLUSTER_PROGRAM;
    event.generation = slot ? slot->entry_context.generation : 0u;
    event.restarts = slot ? slot->restarts : 0u;
    event.restart_budget = slot ? slot->restart_budget : 0u;
    event.code = code;
    scheduler->trace(scheduler->trace_sink, &event);
}

static mmuko_scheduler_slot *slot_for_role(mmuko_scheduler *scheduler, mmuko_context_role role)
{
    if (role == MMUKO_CONTEXT_KERNEL) return &scheduler->slots[0];
    if (role == MMUKO_CONTEXT_MODULE) return &scheduler->slots[1];
    return 0;
}

const mmuko_scheduler_slot *mmuko_scheduler_slot_of(const mmuko_scheduler *scheduler, mmuko_context_role role)
{
    return scheduler ? slot_for_role((mmuko_scheduler *)scheduler, role) : 0;
}

static void slot_clear(mmuko_scheduler_slot *slot, mmuko_context *context, mmuko_context_role role)
{
    slot->context = context;
    slot->role = role;
    slot->cluster = MMUKO_CLUSTER_PROGRAM;
    slot->entry = 0;
    slot->fault_entry = 0;
    slot->fault_generation = 0;
    slot->stack = 0;
    slot->stack_size = 0;
    slot->entry_context.abi_version = MMUKO_ABI_VERSION;
    slot->entry_context.cluster_id = MMUKO_CLUSTER_PROGRAM;
    slot->entry_context.generation = 0;
    slot->entry_context.self = context;
    slot->entry_context.peer = 0;
    slot->restart_budget = 0;
    slot->restarts = 0;
    slot->registered = false;
    slot->abandoned = false;
}

int mmuko_scheduler_init(mmuko_scheduler *scheduler, mmuko_scheduler_trace_fn trace, void *trace_sink)
{
    if (!scheduler) return MMUKO_SCHEDULER_EINVAL;
    mmuko_context_reset(&scheduler->program);
    mmuko_context_reset(&scheduler->kernel);
    mmuko_context_reset(&scheduler->module);
    if (mmuko_context_init_current(&scheduler->program) != MMUKO_CONTEXT_OK) return MMUKO_SCHEDULER_ESTATE;
    scheduler->current = &scheduler->program;
    slot_clear(&scheduler->slots[0], &scheduler->kernel, MMUKO_CONTEXT_KERNEL);
    slot_clear(&scheduler->slots[1], &scheduler->module, MMUKO_CONTEXT_MODULE);
    scheduler->last = MMUKO_CONTEXT_PROGRAM;
    scheduler->topology_valid = false;
    scheduler->dispatches = 0;
    scheduler->trace = trace;
    scheduler->trace_sink = trace_sink;
    return MMUKO_SCHEDULER_OK;
}

/* (Re)create the slot's context for its current generation. */
static int slot_start(mmuko_scheduler *scheduler, mmuko_scheduler_slot *slot)
{
    mmuko_context_entry_fn entry = slot->entry;
    int rc;

    if (slot->fault_entry && slot->entry_context.generation == slot->fault_generation)
        entry = slot->fault_entry;
    rc = mmuko_context_init(slot->context, slot->role, slot->stack, slot->stack_size,
                            entry, &slot->entry_context);
    if (rc != MMUKO_CONTEXT_OK) return rc;
    return mmuko_context_set_owner(slot->context, &scheduler->program);
}

int mmuko_scheduler_register(mmuko_scheduler *scheduler, const mmuko_context_registration *registration)
{
    mmuko_scheduler_slot *slot;

    if (!scheduler || !registration || !registration->entry) return MMUKO_SCHEDULER_EINVAL;
    slot = slot_for_role(scheduler, registration->role);
    if (!slot) return MMUKO_SCHEDULER_EINVAL;
    if (slot->registered || scheduler->current != &scheduler->program) return MMUKO_SCHEDULER_ESTATE;
    /* An endpoint can only be handed to the cluster it speaks for. */
    if (registration->peer && registration->peer->cluster != registration->cluster) return MMUKO_SCHEDULER_EINVAL;

    slot->cluster = registration->cluster;
    slot->entry = registration->entry;
    slot->fault_entry = registration->fault_entry;
    slot->fault_generation = registration->fault_generation;
    slot->stack = registration->stack;
    slot->stack_size = registration->stack_size;
    slot->entry_context.cluster_id = registration->cluster;
    slot->entry_context.generation = 0;
    slot->entry_context.peer = registration->peer;
    slot->restart_budget = registration->restart_budget;
    slot->restarts = 0;
    slot->abandoned = false;

    if (slot_start(scheduler, slot) != MMUKO_CONTEXT_OK) return MMUKO_SCHEDULER_EINVAL;
    slot->registered = true;
    scheduler->topology_valid = false;
    emit(scheduler, MMUKO_SCHEDULER_EVENT_REGISTERED, slot, MMUKO_CONTEXT_PROGRAM, 0);
    return MMUKO_SCHEDULER_OK;
}

int mmuko_scheduler_validate(mmuko_scheduler *scheduler)
{
    mmuko_context_vertex vertices[1u + MMUKO_SCHEDULER_EXECUTION_SLOTS];
    mmuko_context_edge edges[MMUKO_SCHEDULER_EXECUTION_SLOTS + 1u];
    size_t edge_count = 0;
    bool all_peers = true;
    int rc;

    if (!scheduler) return MMUKO_SCHEDULER_EINVAL;

    vertices[0].role = MMUKO_CONTEXT_PROGRAM;
    vertices[0].cluster = MMUKO_CLUSTER_PROGRAM;
    vertices[0].color = MMUKO_CONTEXT_COLOR_NEUTRAL;
    for (size_t i = 0; i < MMUKO_SCHEDULER_EXECUTION_SLOTS; i++) {
        const mmuko_scheduler_slot *slot = &scheduler->slots[i];
        if (!slot->registered) {
            scheduler->topology_valid = false;
            emit(scheduler, MMUKO_SCHEDULER_EVENT_TOPOLOGY, 0, MMUKO_CONTEXT_PROGRAM, MMUKO_TOPOLOGY_EBILATERAL);
            return MMUKO_SCHEDULER_ETOPOLOGY;
        }
        vertices[1u + i].role = slot->role;
        vertices[1u + i].cluster = slot->cluster;
        vertices[1u + i].color = mmuko_topology_color_for_cluster(slot->cluster);
        if (slot->context->owner == &scheduler->program) {
            edges[edge_count].from = MMUKO_CONTEXT_PROGRAM;
            edges[edge_count].to = slot->role;
            edges[edge_count].kind = MMUKO_CONTEXT_EDGE_CONTROL;
            edge_count++;
        }
        if (!slot->entry_context.peer) all_peers = false;
    }
    if (all_peers) {
        edges[edge_count].from = MMUKO_CONTEXT_KERNEL;
        edges[edge_count].to = MMUKO_CONTEXT_MODULE;
        edges[edge_count].kind = MMUKO_CONTEXT_EDGE_PEER;
        edge_count++;
    }

    rc = mmuko_topology_validate_contexts(vertices, 1u + MMUKO_SCHEDULER_EXECUTION_SLOTS, edges, edge_count);
    scheduler->topology_valid = rc == MMUKO_TOPOLOGY_OK;
    emit(scheduler, MMUKO_SCHEDULER_EVENT_TOPOLOGY, 0, MMUKO_CONTEXT_PROGRAM, rc);
    return scheduler->topology_valid ? MMUKO_SCHEDULER_OK : MMUKO_SCHEDULER_ETOPOLOGY;
}

/* Apply fault policy after a dispatch returns to the program context. */
static void settle(mmuko_scheduler *scheduler, mmuko_scheduler_slot *slot)
{
    mmuko_context *context = slot->context;

    if (context->state == MMUKO_CONTEXT_TERMINATED) {
        emit(scheduler, MMUKO_SCHEDULER_EVENT_TERMINATED, slot, MMUKO_CONTEXT_PROGRAM, context->exit_code);
        return;
    }
    if (context->state != MMUKO_CONTEXT_FAULTED) return;

    emit(scheduler, MMUKO_SCHEDULER_EVENT_FAULTED, slot, MMUKO_CONTEXT_PROGRAM, context->exit_code);
    if (slot->restarts >= slot->restart_budget) {
        slot->abandoned = true;
        emit(scheduler, MMUKO_SCHEDULER_EVENT_ABANDONED, slot, MMUKO_CONTEXT_PROGRAM, context->exit_code);
        return;
    }

    /* Deliberate re-creation: a fresh frame on the same (owned) stack, a new
     * generation, and a new single entry into the subsystem main. */
    int exit_code = context->exit_code;
    slot->restarts++;
    slot->entry_context.generation++;
    if (slot_start(scheduler, slot) != MMUKO_CONTEXT_OK) {
        slot->abandoned = true;
        emit(scheduler, MMUKO_SCHEDULER_EVENT_ABANDONED, slot, MMUKO_CONTEXT_PROGRAM, exit_code);
        return;
    }
    emit(scheduler, MMUKO_SCHEDULER_EVENT_RECREATED, slot, MMUKO_CONTEXT_PROGRAM, exit_code);
}

int mmuko_scheduler_run(mmuko_scheduler *scheduler, uint32_t max_dispatches)
{
    if (!scheduler) return MMUKO_SCHEDULER_EINVAL;
    if (scheduler->current != &scheduler->program || scheduler->program.state != MMUKO_CONTEXT_RUNNING)
        return MMUKO_SCHEDULER_ESTATE;
    if (!scheduler->topology_valid && mmuko_scheduler_validate(scheduler) != MMUKO_SCHEDULER_OK)
        return MMUKO_SCHEDULER_ETOPOLOGY;

    for (;;) {
        bool dispatched = false;

        for (size_t i = 0; i < MMUKO_SCHEDULER_EXECUTION_SLOTS; i++) {
            mmuko_scheduler_slot *slot = &scheduler->slots[i];
            int status;

            if (!mmuko_context_is_resumable(slot->context)) continue;
            if (scheduler->dispatches >= max_dispatches) return MMUKO_SCHEDULER_ELIMIT;

            emit(scheduler, MMUKO_SCHEDULER_EVENT_DISPATCH, slot, scheduler->last, 0);
            scheduler->dispatches++;
            scheduler->current = slot->context;
            status = mmuko_context_resume(&scheduler->program, slot->context);
            scheduler->current = &scheduler->program;
            scheduler->last = slot->role;
            dispatched = true;

            if (status != MMUKO_CONTEXT_OK) {
                emit(scheduler, MMUKO_SCHEDULER_EVENT_REJECTED, slot, MMUKO_CONTEXT_PROGRAM, status);
                /* A context the scheduler cannot run must not be retried forever. */
                if (mmuko_context_is_resumable(slot->context)) mmuko_context_mark_faulted(slot->context, 1);
            }
            settle(scheduler, slot);
        }
        if (!dispatched) break;
    }

    emit(scheduler, MMUKO_SCHEDULER_EVENT_IDLE, 0, scheduler->last, 0);
    for (size_t i = 0; i < MMUKO_SCHEDULER_EXECUTION_SLOTS; i++) {
        const mmuko_context *context = scheduler->slots[i].context;
        if (context->state != MMUKO_CONTEXT_TERMINATED) return context->exit_code != 0 ? context->exit_code : 1;
    }
    return MMUKO_SCHEDULER_OK;
}
