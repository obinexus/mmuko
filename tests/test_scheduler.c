/* Control-plane tests: the program scheduler dispatches kernel and module
 * contexts, contains faults per context, and refuses invalid topologies.
 * Test entries stand in for the subsystem trampolines so each property can
 * be observed directly. */
#include <mmuko/channel.h>
#include <mmuko/scheduler.h>
#include <mmuko/stack.h>
#include <mmuko/topology.h>

#include <stdio.h>
#include <string.h>

#define STEPS 3u
#define STACK_BYTES (64u * 1024u)

static unsigned failures;

#define CHECK(expr)                                                                   \
    do {                                                                              \
        if (!(expr)) {                                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                               \
        }                                                                             \
    } while (0)

static void report(const char *name, unsigned failures_before)
{
    printf("%s: %s\n", name, failures == failures_before ? "HELD" : "FAILED");
}

/* ---- observation --------------------------------------------------------- */

typedef struct step_record {
    mmuko_context_role role;
    uint32_t generation;
    unsigned step;
} step_record;

static step_record steps[32];
static size_t step_count;
static mmuko_scheduler_event events[64];
static size_t event_count;
static unsigned entries[3][4];              /* [role][generation] */
static int module_fault_code;               /* non-zero: module gen 0 faults after step 0 */
static mmuko_scheduler *breach_target;      /* corrupt kernel canary once, after its first yield */
static int breach_done;

static void reset_observation(void)
{
    step_count = 0;
    event_count = 0;
    memset(entries, 0, sizeof entries);
    module_fault_code = 0;
    breach_target = NULL;
    breach_done = 0;
}

static void record_event(void *sink, const mmuko_scheduler_event *event)
{
    (void)sink;
    if (event_count < sizeof events / sizeof events[0]) events[event_count++] = *event;

    /* Watchdog stand-in: after the kernel's first yield, smash its canary. */
    if (breach_target && !breach_done && event->kind == MMUKO_SCHEDULER_EVENT_DISPATCH &&
        event->role == MMUKO_CONTEXT_MODULE && event->from == MMUKO_CONTEXT_KERNEL) {
        ((volatile unsigned char *)mmuko_context_stack_floor(&breach_target->kernel))[-8] ^= 0xFFu;
        breach_done = 1;
    }
}

static size_t count_events(mmuko_scheduler_event_kind kind, mmuko_context_role role)
{
    size_t n = 0;
    for (size_t i = 0; i < event_count; i++) {
        if (events[i].kind == kind && events[i].role == role) n++;
    }
    return n;
}

/* Subsystem stand-in: STEPS steps with a yield between them. */
static void stepping_entry(void *argument)
{
    const mmuko_entry_context *context = (const mmuko_entry_context *)argument;
    mmuko_context_role role = context->self->role;
    volatile unsigned local = 0;

    if (context->generation < 4u) entries[role][context->generation]++;
    for (unsigned step = 0; step < STEPS; step++) {
        if (step != 0u) mmuko_context_yield(context->self);
        if (step_count < sizeof steps / sizeof steps[0])
            steps[step_count++] = (step_record){role, context->generation, local};
        local++;
        /* Mid-run fault: after one yield/resume cycle, with a live frame. */
        if (role == MMUKO_CONTEXT_MODULE && module_fault_code != 0 && context->generation == 0u && step == 1u)
            mmuko_context_exit(context->self, module_fault_code);
    }
}

static void startup_fault_entry(void *argument)
{
    const mmuko_entry_context *context = (const mmuko_entry_context *)argument;
    mmuko_context_exit(context->self, 75);
}

typedef struct fixture {
    mmuko_scheduler scheduler;
    mmuko_channel channel;
    mmuko_stack_region kernel_stack;
    mmuko_stack_region module_stack;
} fixture;

static int setup(fixture *f, uint32_t module_budget, mmuko_cluster_id module_cluster,
                 mmuko_context_entry_fn module_fault_entry)
{
    memset(f, 0, sizeof *f);
    reset_observation();
    if (mmuko_stack_allocate(&f->kernel_stack, STACK_BYTES) != 0) return -1;
    if (mmuko_stack_allocate(&f->module_stack, STACK_BYTES) != 0) return -1;
    mmuko_channel_init(&f->channel, MMUKO_CLUSTER_A_KERNEL, module_cluster);
    if (mmuko_scheduler_init(&f->scheduler, record_event, NULL) != MMUKO_SCHEDULER_OK) return -1;

    const mmuko_context_registration kernel = {
        .role = MMUKO_CONTEXT_KERNEL, .cluster = MMUKO_CLUSTER_A_KERNEL, .entry = stepping_entry,
        .stack = f->kernel_stack.base, .stack_size = f->kernel_stack.size,
        .peer = mmuko_channel_endpoint_of(&f->channel, MMUKO_CLUSTER_A_KERNEL), .restart_budget = 1
    };
    const mmuko_context_registration module = {
        .role = MMUKO_CONTEXT_MODULE, .cluster = module_cluster, .entry = stepping_entry,
        .stack = f->module_stack.base, .stack_size = f->module_stack.size,
        .peer = mmuko_channel_endpoint_of(&f->channel, module_cluster), .restart_budget = module_budget,
        .fault_entry = module_fault_entry, .fault_generation = 0
    };
    if (mmuko_scheduler_register(&f->scheduler, &kernel) != MMUKO_SCHEDULER_OK) return -1;
    if (mmuko_scheduler_register(&f->scheduler, &module) != MMUKO_SCHEDULER_OK) return -1;
    return 0;
}

static void teardown(fixture *f)
{
    mmuko_stack_release(&f->kernel_stack);
    mmuko_stack_release(&f->module_stack);
}

static int kernel_steps_continuous(void)
{
    unsigned expected = 0;
    for (size_t i = 0; i < step_count; i++) {
        if (steps[i].role != MMUKO_CONTEXT_KERNEL) continue;
        if (steps[i].generation != 0u || steps[i].step != expected) return 0;
        expected++;
    }
    return expected == STEPS;
}

/* ---- tests --------------------------------------------------------------- */

static void test_round_robin(void)
{
    const unsigned before = failures;
    fixture f;

    CHECK(setup(&f, 1, MMUKO_CLUSTER_B_MODULE, NULL) == 0);
    CHECK(mmuko_scheduler_validate(&f.scheduler) == MMUKO_SCHEDULER_OK);
    CHECK(mmuko_scheduler_run(&f.scheduler, 32) == 0);

    const step_record expected[] = {
        {MMUKO_CONTEXT_KERNEL, 0, 0}, {MMUKO_CONTEXT_MODULE, 0, 0},
        {MMUKO_CONTEXT_KERNEL, 0, 1}, {MMUKO_CONTEXT_MODULE, 0, 1},
        {MMUKO_CONTEXT_KERNEL, 0, 2}, {MMUKO_CONTEXT_MODULE, 0, 2}
    };
    CHECK(step_count == 6u);
    for (size_t i = 0; i < 6u && i < step_count; i++) {
        CHECK(steps[i].role == expected[i].role && steps[i].step == expected[i].step);
    }
    CHECK(entries[MMUKO_CONTEXT_KERNEL][0] == 1u && entries[MMUKO_CONTEXT_MODULE][0] == 1u);

    /* program -> kernel, kernel -> module, module -> kernel, ... */
    const mmuko_context_role from[] = {MMUKO_CONTEXT_PROGRAM, MMUKO_CONTEXT_KERNEL, MMUKO_CONTEXT_MODULE,
                                       MMUKO_CONTEXT_KERNEL, MMUKO_CONTEXT_MODULE, MMUKO_CONTEXT_KERNEL};
    const mmuko_context_role to[] = {MMUKO_CONTEXT_KERNEL, MMUKO_CONTEXT_MODULE, MMUKO_CONTEXT_KERNEL,
                                     MMUKO_CONTEXT_MODULE, MMUKO_CONTEXT_KERNEL, MMUKO_CONTEXT_MODULE};
    size_t d = 0;
    for (size_t i = 0; i < event_count; i++) {
        if (events[i].kind != MMUKO_SCHEDULER_EVENT_DISPATCH) continue;
        CHECK(d < 6u);
        if (d < 6u) CHECK(events[i].from == from[d] && events[i].role == to[d]);
        d++;
    }
    CHECK(d == 6u && f.scheduler.dispatches == 6u);
    CHECK(f.scheduler.kernel.state == MMUKO_CONTEXT_TERMINATED);
    CHECK(f.scheduler.module.state == MMUKO_CONTEXT_TERMINATED);
    CHECK(f.scheduler.current == &f.scheduler.program);
    CHECK(f.scheduler.program.state == MMUKO_CONTEXT_RUNNING);
    teardown(&f);
    report("scheduler round robin (program -> kernel <-> module, single entry each)", before);
}

static void test_mid_run_fault_recreated(void)
{
    const unsigned before = failures;
    fixture f;

    CHECK(setup(&f, 1, MMUKO_CLUSTER_B_MODULE, NULL) == 0);
    module_fault_code = 9;
    CHECK(mmuko_scheduler_run(&f.scheduler, 32) == 0);

    CHECK(count_events(MMUKO_SCHEDULER_EVENT_FAULTED, MMUKO_CONTEXT_MODULE) == 1u);
    CHECK(count_events(MMUKO_SCHEDULER_EVENT_RECREATED, MMUKO_CONTEXT_MODULE) == 1u);
    CHECK(count_events(MMUKO_SCHEDULER_EVENT_FAULTED, MMUKO_CONTEXT_KERNEL) == 0u);
    CHECK(entries[MMUKO_CONTEXT_MODULE][0] == 1u && entries[MMUKO_CONTEXT_MODULE][1] == 1u);
    /* The kernel was entered once and kept counting through the module fault. */
    CHECK(entries[MMUKO_CONTEXT_KERNEL][0] == 1u && entries[MMUKO_CONTEXT_KERNEL][1] == 0u);
    CHECK(kernel_steps_continuous());
    CHECK(mmuko_context_stack_intact(&f.scheduler.kernel));
    CHECK(f.scheduler.kernel.state == MMUKO_CONTEXT_TERMINATED);
    CHECK(f.scheduler.module.state == MMUKO_CONTEXT_TERMINATED);
    CHECK(f.scheduler.slots[1].entry_context.generation == 1u);
    teardown(&f);
    report("scheduler mid-run module fault (kernel untouched, module recreated generation=1)", before);
}

static void test_budget_exhausted(void)
{
    const unsigned before = failures;
    fixture f;

    CHECK(setup(&f, 0, MMUKO_CLUSTER_B_MODULE, NULL) == 0);
    module_fault_code = 9;
    CHECK(mmuko_scheduler_run(&f.scheduler, 32) == 9);
    CHECK(count_events(MMUKO_SCHEDULER_EVENT_ABANDONED, MMUKO_CONTEXT_MODULE) == 1u);
    CHECK(f.scheduler.module.state == MMUKO_CONTEXT_FAULTED);
    CHECK(f.scheduler.module.resume_count == 2u);      /* start + one resume, never again */
    CHECK(f.scheduler.kernel.state == MMUKO_CONTEXT_TERMINATED);
    CHECK(kernel_steps_continuous());
    CHECK(mmuko_context_resume(&f.scheduler.program, &f.scheduler.module) == MMUKO_CONTEXT_EFAULTED);
    teardown(&f);
    report("scheduler restart budget exhausted (FAULTED context never resumed, kernel completes)", before);
}

static void test_startup_fault_injection(void)
{
    const unsigned before = failures;
    fixture f;

    CHECK(setup(&f, 1, MMUKO_CLUSTER_B_MODULE, startup_fault_entry) == 0);
    CHECK(mmuko_scheduler_run(&f.scheduler, 32) == 0);
    CHECK(count_events(MMUKO_SCHEDULER_EVENT_FAULTED, MMUKO_CONTEXT_MODULE) == 1u);
    CHECK(entries[MMUKO_CONTEXT_MODULE][0] == 0u);    /* fault entry ran instead of the subsystem */
    CHECK(entries[MMUKO_CONTEXT_MODULE][1] == 1u);
    CHECK(entries[MMUKO_CONTEXT_KERNEL][0] == 1u);
    for (size_t i = 0; i < event_count; i++) {
        if (events[i].kind == MMUKO_SCHEDULER_EVENT_FAULTED) CHECK(events[i].code == 75 && events[i].generation == 0u);
    }
    teardown(&f);
    report("scheduler control-plane startup fault (generation 0 faulted, generation 1 entered once)", before);
}

static void test_stack_breach(void)
{
    const unsigned before = failures;
    fixture f;

    CHECK(setup(&f, 1, MMUKO_CLUSTER_B_MODULE, NULL) == 0);
    breach_target = &f.scheduler;
    CHECK(mmuko_scheduler_run(&f.scheduler, 32) == 0);
    CHECK(breach_done == 1);
    CHECK(count_events(MMUKO_SCHEDULER_EVENT_REJECTED, MMUKO_CONTEXT_KERNEL) == 1u);
    CHECK(count_events(MMUKO_SCHEDULER_EVENT_RECREATED, MMUKO_CONTEXT_KERNEL) == 1u);
    for (size_t i = 0; i < event_count; i++) {
        if (events[i].kind == MMUKO_SCHEDULER_EVENT_REJECTED) CHECK(events[i].code == MMUKO_CONTEXT_ESTACK);
        if (events[i].kind == MMUKO_SCHEDULER_EVENT_FAULTED)
            CHECK(events[i].code == MMUKO_CONTEXT_EXIT_STACK_BREACH);
    }
    CHECK(entries[MMUKO_CONTEXT_KERNEL][0] == 1u && entries[MMUKO_CONTEXT_KERNEL][1] == 1u);
    CHECK(entries[MMUKO_CONTEXT_MODULE][0] == 1u);    /* module unaffected */
    CHECK(f.scheduler.module.state == MMUKO_CONTEXT_TERMINATED);
    teardown(&f);
    report("scheduler stack-canary breach (kernel refused and recreated, module unaffected)", before);
}

static void test_topology_rejected(void)
{
    const unsigned before = failures;
    fixture f;

    /* Module context registered into Cluster A: role/cluster/colour disagree
     * and the peer edge would join A to A. */
    CHECK(setup(&f, 1, MMUKO_CLUSTER_A_KERNEL, NULL) == 0);
    CHECK(mmuko_scheduler_validate(&f.scheduler) == MMUKO_SCHEDULER_ETOPOLOGY);
    CHECK(mmuko_scheduler_run(&f.scheduler, 32) == MMUKO_SCHEDULER_ETOPOLOGY);
    CHECK(count_events(MMUKO_SCHEDULER_EVENT_DISPATCH, MMUKO_CONTEXT_KERNEL) == 0u);
    CHECK(count_events(MMUKO_SCHEDULER_EVENT_DISPATCH, MMUKO_CONTEXT_MODULE) == 0u);
    CHECK(entries[MMUKO_CONTEXT_KERNEL][0] == 0u && entries[MMUKO_CONTEXT_MODULE][0] == 0u);
    teardown(&f);

    /* Unregistered module: not bilateral. */
    fixture g;
    memset(&g, 0, sizeof g);
    reset_observation();
    CHECK(mmuko_scheduler_init(&g.scheduler, record_event, NULL) == MMUKO_SCHEDULER_OK);
    CHECK(mmuko_scheduler_run(&g.scheduler, 32) == MMUKO_SCHEDULER_ETOPOLOGY);
    report("scheduler refuses invalid topology (no dispatch happens)", before);
}

int main(void)
{
    test_round_robin();
    test_mid_run_fault_recreated();
    test_budget_exhausted();
    test_startup_fault_injection();
    test_stack_breach();
    test_topology_rejected();
    if (failures != 0u) {
        fprintf(stderr, "test_scheduler: %u failure(s)\n", failures);
        return 1;
    }
    printf("scheduler-test: HELD\n");
    return 0;
}
