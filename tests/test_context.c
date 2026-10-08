/* Execution-context tests: real resumption, independent stacks, ABI layout,
 * register preservation, lifecycle, and rejection of dead contexts. */
#include <mmuko/context.h>

#include "mmuko_context_arch.h"
#include "mmuko_context_layout.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_STACK_BYTES (64u * 1024u)
#define STEPS 3u

static unsigned failures;

#define CHECK(expr)                                                                   \
    do {                                                                              \
        if (!(expr)) {                                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                               \
        }                                                                             \
    } while (0)

/* Assembly probes: tests/arch/<arch>/mmuko_test_probe.S */
uintptr_t mmuko_probe_caller_sp(void);
uint64_t mmuko_probe_switch_preserves(mmuko_context *from, mmuko_context *to);
void mmuko_probe_scrambler_entry(void *pair);

typedef struct mmuko_probe_pair {
    mmuko_context *self;
    mmuko_context *peer;
    uint64_t scrambler_mask;
    uint64_t rounds;
} mmuko_probe_pair;

_Static_assert(offsetof(mmuko_probe_pair, self) == 0, "probe pair layout used by assembly");
_Static_assert(offsetof(mmuko_probe_pair, peer) == 8, "probe pair layout used by assembly");
_Static_assert(offsetof(mmuko_probe_pair, scrambler_mask) == 16, "probe pair layout used by assembly");
_Static_assert(offsetof(mmuko_probe_pair, rounds) == 24, "probe pair layout used by assembly");

static _Alignas(16) unsigned char kernel_stack[TEST_STACK_BYTES];
static _Alignas(16) unsigned char module_stack[TEST_STACK_BYTES];
static _Alignas(16) unsigned char spare_stack[TEST_STACK_BYTES];

static int within(uintptr_t value, uintptr_t low, uintptr_t high)
{
    return value >= low && value < high;
}

static void report(const char *name, unsigned failures_before)
{
    printf("%s: %s\n", name, failures == failures_before ? "HELD" : "FAILED");
}

/* ---- resumption ---------------------------------------------------------- */

typedef struct trace_entry {
    mmuko_context_role role;
    unsigned counter;
} trace_entry;

static trace_entry trace_log[16];
static size_t trace_len;

typedef struct resumption_probe {
    mmuko_context *self;
    mmuko_context *program;
    unsigned entries;           /* times the entry function started */
    unsigned completed;
    uintptr_t sp[STEPS];
    uintptr_t counter_address[STEPS];
    int states_ok;
    int locals_ok;
} resumption_probe;

static void resumption_entry(void *argument)
{
    resumption_probe *probe = (resumption_probe *)argument;
    volatile unsigned counter = 0;
    unsigned char local[256];
    const unsigned char fill = (unsigned char)(0x40u + (unsigned)probe->self->role);

    probe->entries++;
    memset(local, fill, sizeof local);

    for (unsigned step = 0; step < STEPS; step++) {
        if (trace_len < sizeof trace_log / sizeof trace_log[0])
            trace_log[trace_len++] = (trace_entry){probe->self->role, counter};
        probe->sp[step] = mmuko_probe_caller_sp();
        probe->counter_address[step] = (uintptr_t)&counter;
        if (probe->self->state != MMUKO_CONTEXT_RUNNING ||
            probe->program->state != MMUKO_CONTEXT_SUSPENDED)
            probe->states_ok = 0;
        for (size_t i = 0; i < sizeof local; i++) {
            if (local[i] != fill) probe->locals_ok = 0;
        }
        counter++;
        if (step + 1u < STEPS && mmuko_context_yield(probe->self) != MMUKO_CONTEXT_OK)
            probe->states_ok = 0;
    }
    probe->completed++;
    /* Return normally: control must land in the context-exit trampoline. */
}

static void test_resumption(void)
{
    const unsigned before = failures;
    mmuko_context program, kernel, module;
    resumption_probe kp = {0}, mp = {0};
    int program_marker = 0;
    unsigned rounds = 0;

    mmuko_context_reset(&program);
    mmuko_context_reset(&kernel);
    mmuko_context_reset(&module);
    trace_len = 0;

    CHECK(mmuko_context_init_current(&program) == MMUKO_CONTEXT_OK);
    CHECK(program.state == MMUKO_CONTEXT_RUNNING);

    kp = (resumption_probe){&kernel, &program, 0, 0, {0}, {0}, 1, 1};
    mp = (resumption_probe){&module, &program, 0, 0, {0}, {0}, 1, 1};
    CHECK(mmuko_context_init(&kernel, MMUKO_CONTEXT_KERNEL, kernel_stack, sizeof kernel_stack,
                             resumption_entry, &kp) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_init(&module, MMUKO_CONTEXT_MODULE, module_stack, sizeof module_stack,
                             resumption_entry, &mp) == MMUKO_CONTEXT_OK);
    CHECK(kernel.state == MMUKO_CONTEXT_READY && module.state == MMUKO_CONTEXT_READY);
    CHECK(mmuko_context_set_owner(&kernel, &program) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_set_owner(&module, &program) == MMUKO_CONTEXT_OK);

    while ((mmuko_context_is_resumable(&kernel) || mmuko_context_is_resumable(&module)) && rounds < 10u) {
        if (mmuko_context_is_resumable(&kernel)) {
            CHECK(mmuko_context_resume(&program, &kernel) == MMUKO_CONTEXT_OK);
            CHECK(program.state == MMUKO_CONTEXT_RUNNING);
            if (kernel.state != MMUKO_CONTEXT_TERMINATED) CHECK(kernel.state == MMUKO_CONTEXT_SUSPENDED);
        }
        if (mmuko_context_is_resumable(&module)) {
            CHECK(mmuko_context_resume(&program, &module) == MMUKO_CONTEXT_OK);
            if (module.state != MMUKO_CONTEXT_TERMINATED) CHECK(module.state == MMUKO_CONTEXT_SUSPENDED);
        }
        rounds++;
    }

    /* kernel 0, module 0, kernel 1, module 1, kernel 2, module 2 - only
     * possible if each frame resumes after its yield with its counter intact.
     * Restarting from the top would log counter 0 again. */
    const trace_entry expected[] = {
        {MMUKO_CONTEXT_KERNEL, 0}, {MMUKO_CONTEXT_MODULE, 0},
        {MMUKO_CONTEXT_KERNEL, 1}, {MMUKO_CONTEXT_MODULE, 1},
        {MMUKO_CONTEXT_KERNEL, 2}, {MMUKO_CONTEXT_MODULE, 2}
    };
    CHECK(trace_len == sizeof expected / sizeof expected[0]);
    for (size_t i = 0; i < trace_len && i < sizeof expected / sizeof expected[0]; i++) {
        CHECK(trace_log[i].role == expected[i].role);
        CHECK(trace_log[i].counter == expected[i].counter);
        printf("  %s %u\n", mmuko_context_role_name(trace_log[i].role), trace_log[i].counter);
    }

    CHECK(kp.entries == 1u && mp.entries == 1u);      /* one entry per main */
    CHECK(kp.completed == 1u && mp.completed == 1u);
    CHECK(kp.states_ok && mp.states_ok);
    CHECK(kp.locals_ok && mp.locals_ok);
    CHECK(kernel.resume_count == STEPS && module.resume_count == STEPS);

    /* Return trampoline: entry returned normally -> TERMINATED, code 0. */
    CHECK(kernel.state == MMUKO_CONTEXT_TERMINATED && kernel.exit_code == 0);
    CHECK(module.state == MMUKO_CONTEXT_TERMINATED && module.exit_code == 0);
    CHECK(program.state == MMUKO_CONTEXT_RUNNING);

    /* Same frame across every resumption; 16-byte aligned; on its own stack. */
    for (unsigned s = 0; s < STEPS; s++) {
        CHECK(kp.counter_address[s] == kp.counter_address[0]);
        CHECK(mp.counter_address[s] == mp.counter_address[0]);
        CHECK(kp.sp[s] % MMUKO_CONTEXT_STACK_ALIGN == 0u);
        CHECK(mp.sp[s] % MMUKO_CONTEXT_STACK_ALIGN == 0u);
        CHECK(within(kp.sp[s], mmuko_context_stack_floor(&kernel), mmuko_context_stack_top(&kernel)));
        CHECK(within(mp.sp[s], mmuko_context_stack_floor(&module), mmuko_context_stack_top(&module)));
    }
    CHECK(!within(kp.sp[0], (uintptr_t)module_stack, (uintptr_t)module_stack + sizeof module_stack));
    CHECK(!within(mp.sp[0], (uintptr_t)kernel_stack, (uintptr_t)kernel_stack + sizeof kernel_stack));
    CHECK(!within((uintptr_t)&program_marker, (uintptr_t)kernel_stack, (uintptr_t)kernel_stack + sizeof kernel_stack));
    CHECK(!within((uintptr_t)&program_marker, (uintptr_t)module_stack, (uintptr_t)module_stack + sizeof module_stack));
    CHECK(mmuko_context_stack_intact(&kernel) && mmuko_context_stack_intact(&module));

    /* Terminated contexts are refused and nothing switches. */
    CHECK(mmuko_context_resume(&program, &kernel) == MMUKO_CONTEXT_ETERMINATED);
    CHECK(kernel.resume_count == STEPS);
    report("context resumption (one entry per main, ordered resume, independent stacks)", before);
}

/* ---- lifecycle, ownership and rejection ---------------------------------- */

typedef struct lifecycle_probe {
    mmuko_context *self;
    mmuko_context *other;
    int exit_code;
    int yield_first;
    int resume_other_status;
    unsigned entries;
} lifecycle_probe;

static void lifecycle_entry(void *argument)
{
    lifecycle_probe *probe = (lifecycle_probe *)argument;
    probe->entries++;
    /* A context that does not own `other` may not resume it. */
    if (probe->other) probe->resume_other_status = mmuko_context_resume(probe->self, probe->other);
    if (probe->yield_first) mmuko_context_yield(probe->self);
    mmuko_context_exit(probe->self, probe->exit_code);
}

static void test_lifecycle(void)
{
    const unsigned before = failures;
    mmuko_context program, a, b;
    lifecycle_probe pa = {0}, pb = {0};

    mmuko_context_reset(&program);
    mmuko_context_reset(&a);
    mmuko_context_reset(&b);
    CHECK(a.state == MMUKO_CONTEXT_NEW);
    CHECK(mmuko_context_init_current(&program) == MMUKO_CONTEXT_OK);

    /* Invalid creation leaves the context NEW. */
    CHECK(mmuko_context_init(&a, MMUKO_CONTEXT_KERNEL, NULL, sizeof kernel_stack, lifecycle_entry, &pa) == MMUKO_CONTEXT_EINVAL);
    CHECK(mmuko_context_init(&a, MMUKO_CONTEXT_KERNEL, kernel_stack, 1024u, lifecycle_entry, &pa) == MMUKO_CONTEXT_EINVAL);
    CHECK(mmuko_context_init(&a, MMUKO_CONTEXT_KERNEL, kernel_stack, sizeof kernel_stack, NULL, &pa) == MMUKO_CONTEXT_EINVAL);
    CHECK(mmuko_context_init(&a, MMUKO_CONTEXT_PROGRAM, kernel_stack, sizeof kernel_stack, lifecycle_entry, &pa) == MMUKO_CONTEXT_EINVAL);
    CHECK(a.state == MMUKO_CONTEXT_NEW);
    CHECK(mmuko_context_resume(&program, &a) != MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_yield(&program) == MMUKO_CONTEXT_EINVAL);  /* program has no owner */

    /* Misaligned stack: the core aligns, never trusts the caller. */
    pa = (lifecycle_probe){&a, &b, 0, 1, 1, 0};
    pb = (lifecycle_probe){&b, NULL, 7, 0, 0, 0};
    CHECK(mmuko_context_init(&a, MMUKO_CONTEXT_KERNEL, kernel_stack + 3, sizeof kernel_stack - 3u,
                             lifecycle_entry, &pa) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_saved_stack_pointer(&a) % MMUKO_CONTEXT_STACK_ALIGN == 0u);
    CHECK(within(mmuko_context_saved_stack_pointer(&a), mmuko_context_stack_floor(&a), mmuko_context_stack_top(&a) + 1u));
    CHECK(mmuko_context_stack_floor(&a) % 16u == 0u);
    CHECK(mmuko_context_init(&b, MMUKO_CONTEXT_MODULE, module_stack, sizeof module_stack,
                             lifecycle_entry, &pb) == MMUKO_CONTEXT_OK);

    /* Unowned contexts cannot be resumed; ownership is explicit. */
    CHECK(mmuko_context_resume(&program, &a) == MMUKO_CONTEXT_ENOTOWNER);
    CHECK(a.resume_count == 0u);
    CHECK(mmuko_context_set_owner(&a, &a) == MMUKO_CONTEXT_EINVAL);
    CHECK(mmuko_context_set_owner(&a, &program) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_set_owner(&b, &program) == MMUKO_CONTEXT_OK);

    /* a: tries to resume b (owned by program) -> refused; then yields. */
    CHECK(mmuko_context_resume(&program, &a) == MMUKO_CONTEXT_OK);
    CHECK(pa.resume_other_status == MMUKO_CONTEXT_ENOTOWNER);
    CHECK(b.state == MMUKO_CONTEXT_READY && b.resume_count == 0u);
    CHECK(a.state == MMUKO_CONTEXT_SUSPENDED);
    /* A live (suspended) context cannot be re-created over. */
    CHECK(mmuko_context_init(&a, MMUKO_CONTEXT_KERNEL, kernel_stack, sizeof kernel_stack,
                             lifecycle_entry, &pa) == MMUKO_CONTEXT_ESTATE);
    CHECK(mmuko_context_resume(&program, &a) == MMUKO_CONTEXT_OK);
    CHECK(a.state == MMUKO_CONTEXT_TERMINATED && a.exit_code == 0);
    CHECK(pa.entries == 1u);

    /* b: exits with a non-zero code -> FAULTED, and is never resumed again. */
    CHECK(mmuko_context_resume(&program, &b) == MMUKO_CONTEXT_OK);
    CHECK(b.state == MMUKO_CONTEXT_FAULTED && b.exit_code == 7);
    CHECK(mmuko_context_resume(&program, &b) == MMUKO_CONTEXT_EFAULTED);
    CHECK(b.resume_count == 1u && pb.entries == 1u);
    CHECK(program.state == MMUKO_CONTEXT_RUNNING);

    /* Deliberate re-creation of a faulted context: a fresh, single entry. */
    pb.exit_code = 0;
    CHECK(mmuko_context_init(&b, MMUKO_CONTEXT_MODULE, module_stack, sizeof module_stack,
                             lifecycle_entry, &pb) == MMUKO_CONTEXT_OK);
    CHECK(b.state == MMUKO_CONTEXT_READY && b.resume_count == 0u);
    CHECK(mmuko_context_set_owner(&b, &program) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_resume(&program, &b) == MMUKO_CONTEXT_OK);
    CHECK(b.state == MMUKO_CONTEXT_TERMINATED && pb.entries == 2u);

    /* Control-plane fault of a suspended context. */
    pa = (lifecycle_probe){&a, NULL, 0, 1, 0, 0};
    CHECK(mmuko_context_init(&a, MMUKO_CONTEXT_KERNEL, kernel_stack, sizeof kernel_stack,
                             lifecycle_entry, &pa) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_set_owner(&a, &program) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_resume(&program, &a) == MMUKO_CONTEXT_OK);
    CHECK(a.state == MMUKO_CONTEXT_SUSPENDED);
    CHECK(mmuko_context_mark_faulted(&program, 3) == MMUKO_CONTEXT_EINVAL);
    CHECK(mmuko_context_mark_faulted(&a, 3) == MMUKO_CONTEXT_OK);
    CHECK(a.state == MMUKO_CONTEXT_FAULTED && a.exit_code == 3);
    CHECK(mmuko_context_resume(&program, &a) == MMUKO_CONTEXT_EFAULTED);
    CHECK(a.resume_count == 1u);

    /* Stack-canary breach on a suspended context: refused, FAULTED. */
    pa = (lifecycle_probe){&a, NULL, 0, 1, 0, 0};
    CHECK(mmuko_context_init(&a, MMUKO_CONTEXT_KERNEL, kernel_stack, sizeof kernel_stack,
                             lifecycle_entry, &pa) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_set_owner(&a, &program) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_resume(&program, &a) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_stack_intact(&a));
    ((volatile unsigned char *)mmuko_context_stack_floor(&a))[-1] ^= 0x5Au; /* simulated overflow */
    CHECK(!mmuko_context_stack_intact(&a));
    CHECK(mmuko_context_resume(&program, &a) == MMUKO_CONTEXT_ESTACK);
    CHECK(a.state == MMUKO_CONTEXT_FAULTED && a.exit_code == MMUKO_CONTEXT_EXIT_STACK_BREACH);
    CHECK(a.resume_count == 1u);
    CHECK(mmuko_context_resume(&program, &a) == MMUKO_CONTEXT_EFAULTED);

    report("context lifecycle (states, ownership, FAULTED/TERMINATED rejection, canary)", before);
}

/* ---- ABI layout ---------------------------------------------------------- */

static void noop_entry(void *argument)
{
    (void)argument;
}

static void test_abi_layout(void)
{
    const unsigned before = failures;
    mmuko_context c;
    int argument = 0;

    mmuko_context_reset(&c);
    CHECK(offsetof(mmuko_context, machine) == 0u);
    CHECK(sizeof(mmuko_context_machine) == MMUKO_CONTEXT_MACHINE_BYTES);
    CHECK(MMUKO_CTX_SIZE <= MMUKO_CONTEXT_MACHINE_BYTES);
    CHECK(_Alignof(mmuko_context) >= 16u);
    CHECK(mmuko_context_init(&c, MMUKO_CONTEXT_KERNEL, spare_stack, sizeof spare_stack, noop_entry, &argument) == MMUKO_CONTEXT_OK);
    CHECK(mmuko_context_saved_resume_address(&c) == (uintptr_t)&mmuko_context_start);
    CHECK(c.machine.words[MMUKO_CTX_SEED_CONTEXT / 8] == (uint64_t)(uintptr_t)&c);
    CHECK(c.machine.words[MMUKO_CTX_SEED_ENTRY / 8] == (uint64_t)(uintptr_t)noop_entry);
    CHECK(c.machine.words[MMUKO_CTX_SEED_ARGUMENT / 8] == (uint64_t)(uintptr_t)&argument);
    CHECK(mmuko_context_saved_stack_pointer(&c) % 16u == 0u);

#if defined(__x86_64__)
    const unsigned slots[] = {MMUKO_CTX_RSP, MMUKO_CTX_RIP, MMUKO_CTX_RBX, MMUKO_CTX_RBP,
                              MMUKO_CTX_R12, MMUKO_CTX_R13, MMUKO_CTX_R14, MMUKO_CTX_R15, MMUKO_CTX_MXCSR};
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++) {
        CHECK(slots[i] % 8u == 0u && slots[i] + 8u <= MMUKO_CTX_SIZE);
        for (size_t j = i + 1; j < sizeof slots / sizeof slots[0]; j++) CHECK(slots[i] != slots[j]);
    }
    CHECK(MMUKO_CTX_FPUCW == MMUKO_CTX_MXCSR + 4);
    /* rsp leaves the two zero words below the aligned top. */
    CHECK(mmuko_context_saved_stack_pointer(&c) == mmuko_context_stack_top(&c) - 16u);
    CHECK((c.machine.words[MMUKO_CTX_MXCSR / 8] & 0xFFFFFFFFu) == MMUKO_CTX_DEFAULT_MXCSR);
    CHECK((c.machine.words[MMUKO_CTX_MXCSR / 8] >> 32) == MMUKO_CTX_DEFAULT_FPUCW);
    CHECK(c.machine.words[MMUKO_CTX_RBP / 8] == 0u);
    report("context ABI layout (x86-64 System V)", before);
#elif defined(__aarch64__)
    CHECK(MMUKO_CTX_X20 == MMUKO_CTX_X19 + 8 && MMUKO_CTX_X28 == MMUKO_CTX_X19 + 72);
    CHECK(MMUKO_CTX_X29 == 80 && MMUKO_CTX_X30 == 88);
    CHECK(MMUKO_CTX_D14 == MMUKO_CTX_D8 + 48);
    CHECK(MMUKO_CTX_SP % 8u == 0u && MMUKO_CTX_PC % 8u == 0u && MMUKO_CTX_FPCR % 8u == 0u);
    CHECK(mmuko_context_saved_stack_pointer(&c) == mmuko_context_stack_top(&c));
    CHECK(c.machine.words[MMUKO_CTX_X29 / 8] == 0u);
    CHECK(c.machine.words[MMUKO_CTX_FPCR / 8] == 0u);
    report("context ABI layout (AArch64 AAPCS64)", before);
#else
#error "unsupported architecture"
#endif
}

/* ---- register preservation ----------------------------------------------- */

static void test_register_preservation(void)
{
    const unsigned before = failures;
    mmuko_context program, scrambler;
    mmuko_probe_pair pair;
    uint64_t first, second;

    mmuko_context_reset(&program);
    mmuko_context_reset(&scrambler);
    CHECK(mmuko_context_init_current(&program) == MMUKO_CONTEXT_OK);
    pair = (mmuko_probe_pair){&scrambler, &program, 0, 0};
    CHECK(mmuko_context_init(&scrambler, MMUKO_CONTEXT_KERNEL, spare_stack, sizeof spare_stack,
                             mmuko_probe_scrambler_entry, &pair) == MMUKO_CONTEXT_OK);

    /* Round 1 starts the scrambler; round 2 resumes it mid-function. Each
     * side loads its own pattern into every callee-saved register and the FP
     * control state, then switches away; both must get their own back. */
    first = mmuko_probe_switch_preserves(&program, &scrambler);
    second = mmuko_probe_switch_preserves(&program, &scrambler);
    CHECK(first == 0u);
    CHECK(second == 0u);
    CHECK(pair.rounds == 1u);
    CHECK(pair.scrambler_mask == 0u);
    if (first || second || pair.scrambler_mask)
        fprintf(stderr, "register masks: first=%#llx second=%#llx scrambler=%#llx\n",
                (unsigned long long)first, (unsigned long long)second,
                (unsigned long long)pair.scrambler_mask);
    report("register preservation (callee-saved + FP control, both directions)", before);
}

int main(void)
{
    test_abi_layout();
    test_resumption();
    test_lifecycle();
    test_register_preservation();
    if (failures != 0u) {
        fprintf(stderr, "test_context: %u failure(s)\n", failures);
        return 1;
    }
    printf("context-test: HELD\n");
    return 0;
}
