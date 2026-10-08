#ifndef MMUKO_CONTEXT_H
#define MMUKO_CONTEXT_H

/* Neutral execution-context abstraction (execution plane).
 *
 * A context is a saved machine state plus its own stack. Contexts switch
 * cooperatively through mmuko_context_switch(), which is implemented per ABI
 * in arch/<arch>/<abi>/mmuko_context_switch.S. The machine state is an opaque
 * block here: register layouts live in arch/<arch>/<abi>/mmuko_context_layout.h
 * and are never visible through <mmuko/kernel> or <mmuko/module>.
 *
 * The context core is freestanding: it uses no libc headers or symbols
 * (enforced by `make freestanding-check`).
 */

/* Size of the opaque machine-state block shared with the assembly bindings. */
#define MMUKO_CONTEXT_MACHINE_BYTES 192
/* Stack-pointer alignment required by both x86-64 System V and AAPCS64. */
#define MMUKO_CONTEXT_STACK_ALIGN 16
/* Smallest stack mmuko_context_init accepts. */
#define MMUKO_CONTEXT_STACK_MIN 16384
/* Canary block written at the low end of every context stack. */
#define MMUKO_CONTEXT_CANARY_BYTES 64

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

typedef enum mmuko_context_role {
    MMUKO_CONTEXT_PROGRAM = 0,
    MMUKO_CONTEXT_KERNEL = 1,
    MMUKO_CONTEXT_MODULE = 2
} mmuko_context_role;

typedef enum mmuko_context_state {
    MMUKO_CONTEXT_NEW = 0,      /* storage exists; not initialized */
    MMUKO_CONTEXT_READY,        /* has a stack and entry; never run */
    MMUKO_CONTEXT_RUNNING,      /* currently owns the CPU */
    MMUKO_CONTEXT_SUSPENDED,    /* yielded; resumes after its yield point */
    MMUKO_CONTEXT_FAULTED,      /* failed; never resumed again */
    MMUKO_CONTEXT_TERMINATED    /* entry returned; never resumed again */
} mmuko_context_state;

typedef enum mmuko_context_status {
    MMUKO_CONTEXT_OK = 0,
    MMUKO_CONTEXT_EINVAL = -1,
    MMUKO_CONTEXT_ESTATE = -2,
    MMUKO_CONTEXT_ENOTOWNER = -3,
    MMUKO_CONTEXT_EFAULTED = -4,
    MMUKO_CONTEXT_ETERMINATED = -5,
    MMUKO_CONTEXT_ESTACK = -6
} mmuko_context_status;

/* Exit code recorded when a stack-canary breach faults a context. */
#define MMUKO_CONTEXT_EXIT_STACK_BREACH 86

typedef void (*mmuko_context_entry_fn)(void *argument);

/* Opaque saved machine state. Only the architecture binding knows which
 * word holds which register. */
typedef struct mmuko_context_machine {
    _Alignas(16) uint64_t words[MMUKO_CONTEXT_MACHINE_BYTES / 8];
} mmuko_context_machine;

typedef struct mmuko_context {
    mmuko_context_machine machine;  /* offset 0: addressed by the assembly */
    mmuko_context_role role;
    mmuko_context_state state;
    void *stack_base;               /* as supplied by the owner of the memory */
    size_t stack_size;
    mmuko_context_entry_fn entry;
    void *argument;
    struct mmuko_context *owner;    /* the only context allowed to resume this one */
    int exit_code;
    uint32_t resume_count;          /* switches into this context */
} mmuko_context;

/* Lifecycle --------------------------------------------------------------- */

/* Return a context to MMUKO_CONTEXT_NEW (all fields cleared). */
void mmuko_context_reset(mmuko_context *context);

/* Create an execution context with its own stack. entry(argument) runs on the
 * first resume. When entry returns, the context-exit trampoline marks the
 * context TERMINATED and switches to its owner. Accepted roles: KERNEL,
 * MODULE. Allowed from NEW, READY, FAULTED or TERMINATED (re-creation). */
int mmuko_context_init(mmuko_context *context,
                       mmuko_context_role role,
                       void *stack,
                       size_t stack_size,
                       mmuko_context_entry_fn entry,
                       void *argument);

/* Adopt the currently executing thread of control as the PROGRAM context.
 * It keeps the caller's stack and starts out RUNNING. */
int mmuko_context_init_current(mmuko_context *context);

/* Bind a READY context to the context that will schedule it. */
int mmuko_context_set_owner(mmuko_context *context, mmuko_context *owner);

/* Switching --------------------------------------------------------------- */

/* Owner -> target. Refuses FAULTED, TERMINATED, non-owned, or stack-breached
 * contexts without switching. Returns when target yields or exits. */
int mmuko_context_resume(mmuko_context *owner, mmuko_context *target);

/* self -> self->owner. Returns when the owner resumes self again; execution
 * continues at the instruction after this call. */
int mmuko_context_yield(mmuko_context *self);

/* Finish self: exit_code 0 -> TERMINATED, otherwise FAULTED. Never returns. */
_Noreturn void mmuko_context_exit(mmuko_context *self, int exit_code);

/* Control-plane fault: mark a READY or SUSPENDED context FAULTED. */
int mmuko_context_mark_faulted(mmuko_context *context, int exit_code);

/* Inspection -------------------------------------------------------------- */

int mmuko_context_is_resumable(const mmuko_context *context);
int mmuko_context_stack_intact(const mmuko_context *context);
uintptr_t mmuko_context_stack_floor(const mmuko_context *context);
uintptr_t mmuko_context_stack_top(const mmuko_context *context);
uintptr_t mmuko_context_saved_stack_pointer(const mmuko_context *context);
uintptr_t mmuko_context_saved_resume_address(const mmuko_context *context);
const char *mmuko_context_role_name(mmuko_context_role role);
const char *mmuko_context_state_name(mmuko_context_state state);

/* Architecture binding ---------------------------------------------------- */

/* Save the running machine state into from and continue to. Raw primitive:
 * it performs no state or ownership checks. Scheduling code uses
 * mmuko_context_resume() and mmuko_context_yield(). */
void mmuko_context_switch(mmuko_context *from, mmuko_context *to);

#endif /* __ASSEMBLER__ */

#endif
