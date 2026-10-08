#include <mmuko/context.h>

#include "mmuko_context_arch.h"

/* Freestanding: no libc. Every switch goes through the ABI binding in
 * arch/<arch>/<abi>/mmuko_context_switch.S; this file owns the lifecycle,
 * ownership and integrity rules around it. */

#define MMUKO_CONTEXT_CANARY_WORDS (MMUKO_CONTEXT_CANARY_BYTES / 8)
#define MMUKO_CONTEXT_CANARY_SEED 0x4d4d554b43545821ull /* "MMUKCTX!" */

static uintptr_t align_up(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uintptr_t align_down(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1u);
}

static uint64_t *canary_block(const mmuko_context *context)
{
    return (uint64_t *)align_up((uintptr_t)context->stack_base, MMUKO_CONTEXT_STACK_ALIGN);
}

/* Bound to the context address so a block copied from another stack still
 * reads as a breach. */
static uint64_t canary_word(const mmuko_context *context, unsigned index)
{
    return MMUKO_CONTEXT_CANARY_SEED ^ ((uint64_t)(uintptr_t)context << 1) ^ index;
}

void mmuko_context_reset(mmuko_context *context)
{
    if (!context) return;
    for (unsigned i = 0; i < MMUKO_CONTEXT_MACHINE_BYTES / 8; i++) context->machine.words[i] = 0;
    context->role = MMUKO_CONTEXT_PROGRAM;
    context->state = MMUKO_CONTEXT_NEW;
    context->stack_base = 0;
    context->stack_size = 0;
    context->entry = 0;
    context->argument = 0;
    context->owner = 0;
    context->exit_code = 0;
    context->resume_count = 0;
}

int mmuko_context_init(mmuko_context *context,
                       mmuko_context_role role,
                       void *stack,
                       size_t stack_size,
                       mmuko_context_entry_fn entry,
                       void *argument)
{
    uintptr_t base;
    uintptr_t top;

    if (!context || !stack || !entry) return MMUKO_CONTEXT_EINVAL;
    if (role != MMUKO_CONTEXT_KERNEL && role != MMUKO_CONTEXT_MODULE) return MMUKO_CONTEXT_EINVAL;
    /* A live context (running or holding a suspended frame) cannot be
     * overwritten; it must fault or terminate first. */
    if (context->state == MMUKO_CONTEXT_RUNNING || context->state == MMUKO_CONTEXT_SUSPENDED)
        return MMUKO_CONTEXT_ESTATE;
    if (stack_size < MMUKO_CONTEXT_STACK_MIN) return MMUKO_CONTEXT_EINVAL;
    if (stack_size > UINTPTR_MAX - (uintptr_t)stack) return MMUKO_CONTEXT_EINVAL;

    base = align_up((uintptr_t)stack, MMUKO_CONTEXT_STACK_ALIGN);
    top = align_down((uintptr_t)stack + stack_size, MMUKO_CONTEXT_STACK_ALIGN);
    if (top <= base || top - base < MMUKO_CONTEXT_STACK_MIN / 2) return MMUKO_CONTEXT_EINVAL;

    mmuko_context_reset(context);
    context->role = role;
    context->stack_base = stack;
    context->stack_size = stack_size;
    context->entry = entry;
    context->argument = argument;

    for (unsigned i = 0; i < MMUKO_CONTEXT_CANARY_WORDS; i++)
        canary_block(context)[i] = canary_word(context, i);

    mmuko_context_arch_prepare(context, top);
    context->state = MMUKO_CONTEXT_READY;
    return MMUKO_CONTEXT_OK;
}

int mmuko_context_init_current(mmuko_context *context)
{
    if (!context) return MMUKO_CONTEXT_EINVAL;
    if (context->state == MMUKO_CONTEXT_RUNNING || context->state == MMUKO_CONTEXT_SUSPENDED)
        return MMUKO_CONTEXT_ESTATE;
    mmuko_context_reset(context);
    context->role = MMUKO_CONTEXT_PROGRAM;
    /* The machine block is filled by the first mmuko_context_switch away. */
    context->state = MMUKO_CONTEXT_RUNNING;
    return MMUKO_CONTEXT_OK;
}

int mmuko_context_set_owner(mmuko_context *context, mmuko_context *owner)
{
    if (!context || !owner || context == owner) return MMUKO_CONTEXT_EINVAL;
    if (owner->owner == context) return MMUKO_CONTEXT_EINVAL;
    if (context->state != MMUKO_CONTEXT_READY) return MMUKO_CONTEXT_ESTATE;
    context->owner = owner;
    return MMUKO_CONTEXT_OK;
}

int mmuko_context_is_resumable(const mmuko_context *context)
{
    return context &&
           (context->state == MMUKO_CONTEXT_READY || context->state == MMUKO_CONTEXT_SUSPENDED);
}

int mmuko_context_stack_intact(const mmuko_context *context)
{
    if (!context) return 0;
    if (!context->stack_base) return 1; /* program context: runs on the caller's stack */
    for (unsigned i = 0; i < MMUKO_CONTEXT_CANARY_WORDS; i++) {
        if (canary_block(context)[i] != canary_word(context, i)) return 0;
    }
    return 1;
}

static void fault_stack_breach(mmuko_context *context)
{
    context->state = MMUKO_CONTEXT_FAULTED;
    context->exit_code = MMUKO_CONTEXT_EXIT_STACK_BREACH;
}

int mmuko_context_resume(mmuko_context *owner, mmuko_context *target)
{
    if (!owner || !target || owner == target) return MMUKO_CONTEXT_EINVAL;
    if (owner->state != MMUKO_CONTEXT_RUNNING) return MMUKO_CONTEXT_ESTATE;
    if (target->owner != owner) return MMUKO_CONTEXT_ENOTOWNER;

    switch (target->state) {
    case MMUKO_CONTEXT_READY:
    case MMUKO_CONTEXT_SUSPENDED:
        break;
    case MMUKO_CONTEXT_FAULTED:
        return MMUKO_CONTEXT_EFAULTED;
    case MMUKO_CONTEXT_TERMINATED:
        return MMUKO_CONTEXT_ETERMINATED;
    default:
        return MMUKO_CONTEXT_ESTATE;
    }

    if (!mmuko_context_stack_intact(target)) {
        fault_stack_breach(target);
        return MMUKO_CONTEXT_ESTACK;
    }

    owner->state = MMUKO_CONTEXT_SUSPENDED;
    target->state = MMUKO_CONTEXT_RUNNING;
    target->resume_count++;
    mmuko_context_switch(owner, target);

    /* Back in the owner: target yielded (SUSPENDED) or exited. The yielding
     * side already set owner->state to RUNNING. */
    if (target->state == MMUKO_CONTEXT_SUSPENDED && !mmuko_context_stack_intact(target)) {
        fault_stack_breach(target);
        return MMUKO_CONTEXT_ESTACK;
    }
    return MMUKO_CONTEXT_OK;
}

int mmuko_context_yield(mmuko_context *self)
{
    if (!self || !self->owner) return MMUKO_CONTEXT_EINVAL;
    if (self->state != MMUKO_CONTEXT_RUNNING) return MMUKO_CONTEXT_ESTATE;
    if (self->owner->state != MMUKO_CONTEXT_SUSPENDED) return MMUKO_CONTEXT_ESTATE;

    self->state = MMUKO_CONTEXT_SUSPENDED;
    self->owner->state = MMUKO_CONTEXT_RUNNING;
    mmuko_context_switch(self, self->owner);

    /* Resumed by the owner through mmuko_context_resume. */
    return MMUKO_CONTEXT_OK;
}

_Noreturn void mmuko_context_exit(mmuko_context *self, int exit_code)
{
    if (!self || !self->owner || self->state != MMUKO_CONTEXT_RUNNING ||
        self->owner->state != MMUKO_CONTEXT_SUSPENDED)
        __builtin_trap();

    self->exit_code = exit_code;
    self->state = exit_code == 0 ? MMUKO_CONTEXT_TERMINATED : MMUKO_CONTEXT_FAULTED;
    self->owner->state = MMUKO_CONTEXT_RUNNING;
    mmuko_context_switch(self, self->owner);

    /* mmuko_context_resume refuses TERMINATED and FAULTED contexts, so this
     * frame is never re-entered. Trap rather than return into nothing. */
    __builtin_trap();
}

int mmuko_context_mark_faulted(mmuko_context *context, int exit_code)
{
    if (!context || context->role == MMUKO_CONTEXT_PROGRAM) return MMUKO_CONTEXT_EINVAL;
    if (!mmuko_context_is_resumable(context)) return MMUKO_CONTEXT_ESTATE;
    context->state = MMUKO_CONTEXT_FAULTED;
    context->exit_code = exit_code != 0 ? exit_code : 1;
    return MMUKO_CONTEXT_OK;
}

uintptr_t mmuko_context_stack_floor(const mmuko_context *context)
{
    if (!context || !context->stack_base) return 0;
    return (uintptr_t)canary_block(context) + MMUKO_CONTEXT_CANARY_BYTES;
}

uintptr_t mmuko_context_stack_top(const mmuko_context *context)
{
    if (!context || !context->stack_base) return 0;
    return align_down((uintptr_t)context->stack_base + context->stack_size, MMUKO_CONTEXT_STACK_ALIGN);
}

uintptr_t mmuko_context_saved_stack_pointer(const mmuko_context *context)
{
    return context ? mmuko_context_arch_saved_sp(context) : 0;
}

uintptr_t mmuko_context_saved_resume_address(const mmuko_context *context)
{
    return context ? mmuko_context_arch_saved_pc(context) : 0;
}

const char *mmuko_context_role_name(mmuko_context_role role)
{
    switch (role) {
    case MMUKO_CONTEXT_PROGRAM: return "program";
    case MMUKO_CONTEXT_KERNEL: return "kernel";
    case MMUKO_CONTEXT_MODULE: return "module";
    default: return "unknown";
    }
}

const char *mmuko_context_state_name(mmuko_context_state state)
{
    switch (state) {
    case MMUKO_CONTEXT_NEW: return "NEW";
    case MMUKO_CONTEXT_READY: return "READY";
    case MMUKO_CONTEXT_RUNNING: return "RUNNING";
    case MMUKO_CONTEXT_SUSPENDED: return "SUSPENDED";
    case MMUKO_CONTEXT_FAULTED: return "FAULTED";
    case MMUKO_CONTEXT_TERMINATED: return "TERMINATED";
    default: return "INVALID";
    }
}
