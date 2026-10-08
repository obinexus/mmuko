# MMUKO Dual Entry Point

C + assembly reference implementation of the MMUKO bilateral runtime described by the supplied `mmuko-boot`, `mmuko-structural-abi-snapshot`, DOP adapter, and PLP reference material.

The runtime has one bootstrap entry and two subsystem entry points. The two subsystems run as **saved execution contexts**: each has its own stack and saved registers, and they switch cooperatively through an assembly routine. A neutral scheduler owned by `mmuko_program_main()` decides which one runs.

| Symbol | Plane | Responsibility |
|---|---|---|
| `main` | — | process entry; calls `mmuko_program_main` |
| `mmuko_program_main()` | control | topology, process isolation, context scheduling |
| `mmuko_kernel_main()` | execution | **Cluster A**: kernel/TUI concern |
| `mmuko_module_main()` | execution | **Cluster B**: module/GUI concern (Orion) |
| `mmuko_kernel_context_entry` / `mmuko_module_context_entry` | execution | trampolines: context start → ABI binding → subsystem main |
| `mmuko_context_init` / `mmuko_context_switch` / `mmuko_context_yield` | shared infrastructure | execution-context core |

```text
              CONTROL PLANE
             mmuko_program_main
                    |
              context scheduler            (src/program/mmuko_scheduler.c)
                /       \
               v         v
        EXECUTION PLANE
      kernel context   module context      (src/context + arch/*/mmuko_context_switch.S)
            |                 |
   mmuko_kernel_main   mmuko_module_main
            |                 |
           TUI            GUI / Orion

              DATA PLANE
       <---- DOP adapter wire records ---->   (src/adapter)
```

## Two layers, kept separate

| Layer | Mechanism | Unit of failure | Recovery |
|---|---|---|---|
| Process isolation | `fork` / `waitpid` (POSIX) | the execution-domain process | supervisor restarts the whole domain (budget 1) |
| Execution context | saved registers + own stack + `mmuko_context_switch` | one context (kernel or module) | scheduler re-creates that context (budget 1) |

Kernel and module contexts share one address space: the **execution-domain** process. A register-level switch never crosses a process boundary, and the bootstrap and supervisor processes never touch a context.

```text
node 0  bootstrap  (mmuko_program_main)         depth 0 / color 0   retires after hand-off
  node 1  supervisor                            depth 1 / color 1   owns the process restart budget
    node 2  execution domain                    depth 2 / color 0
              program context   (neutral scheduler, process main stack)
              kernel context    (Cluster A, own guarded stack)
              module context    (Cluster B, own guarded stack)
```

## Execution contexts

`include/mmuko/context.h` is architecture-neutral. Its machine state is an opaque 192-byte block, and the register layout lives only in `arch/<arch>/<abi>/mmuko_context_layout.h`, which kernel and module code never include.

```c
typedef struct mmuko_context {
    mmuko_context_machine machine;   /* offset 0: opaque, addressed by assembly */
    mmuko_context_role role;         /* PROGRAM | KERNEL | MODULE */
    mmuko_context_state state;       /* NEW READY RUNNING SUSPENDED FAULTED TERMINATED */
    void *stack_base;
    size_t stack_size;
    mmuko_context_entry_fn entry;
    void *argument;
    struct mmuko_context *owner;     /* the only context allowed to resume this one */
    int exit_code;
    uint32_t resume_count;
} mmuko_context;
```

- `mmuko_context_init(ctx, role, stack, size, entry, argument)` creates a context on its own stack, writes a canary block at the stack's low end, and seeds the first switch-in to enter `mmuko_context_start`.
- `mmuko_context_resume(owner, target)` switches only to an owned `READY` or `SUSPENDED` context with an intact stack canary. It refuses `FAULTED`, `TERMINATED`, unowned, or stack-breached contexts without switching.
- `mmuko_context_yield(self)` switches to the owner. Execution resumes at the instruction after the call.
- When `entry` returns, it lands in the assembly **context-exit trampoline**, which calls `mmuko_context_exit(ctx, 0)`. That marks the context `TERMINATED` and switches to the owner. The trampoline never executes `ret` into uninitialized memory.

### ABI bindings

| | x86-64 System V | AArch64 AAPCS64 |
|---|---|---|
| file | `arch/x86_64/sysv/mmuko_context_switch.S` | `arch/aarch64/aapcs64/mmuko_context_switch.S` |
| saved | `rsp`, resume `rip`, `rbx rbp r12–r15`, MXCSR, x87 CW | `x19–x28`, `x29`, `x30`, `sp`, resume `pc`, `d8–d15`, FPCR |
| resume | load `rsp`, `jmp *rip` | load `sp`, `ret x16` (BTI-safe) |
| start seeds | `rbx`=context, `r12`=entry, `r13`=argument | `x19`=context, `x20`=entry, `x21`=argument |
| entry alignment | `rsp ≡ 8 (mod 16)` at entry (after `call`) | `sp ≡ 0 (mod 16)` always |
| layout size | 72 bytes | 184 bytes |

The layout offsets are shared between C and assembly. They are checked by `_Static_assert` in `mmuko_context_arch.c` and by `.if/.error` in the `.S` files, so the two cannot silently diverge.

## Scheduler (`mmuko_program_main`'s control plane)

```c
typedef struct mmuko_scheduler {
    mmuko_context program;   /* adopts the domain's main thread */
    mmuko_context kernel;
    mmuko_context module;
    mmuko_context *current;
    mmuko_scheduler_slot slots[2];   /* entry trampoline, stack, entry context, restart budget */
    ...
} mmuko_scheduler;
```

1. Initialize the program context (the current thread).
2. Allocate a guarded kernel stack and module stack (`mmap` with a `PROT_NONE` guard page).
3. Register the kernel context (Cluster A) and the module context (Cluster B). Both start `READY` and are owned by the program context.
4. Validate the context topology: program is the neutral vertex; kernel = colour A; module = colour B. Every peer edge must cross A|B, which makes the execution plane bipartite, and every execution context must have exactly one control edge from program.
5. Dispatch round-robin: `program → kernel`, `kernel → module`, `module → kernel`, … Each dispatch is `mmuko_context_resume(&program, ctx)`. Every switch passes through the program context, so kernel and module never resume each other (an ownership check enforces this).
6. After each dispatch, apply the fault policy. A `TERMINATED` context is done. A `FAULTED` context is never resumed: it is re-created as a new generation while its budget lasts, and abandoned otherwise.

The scheduler is freestanding and performs no I/O. It reports structured events to a trace callback, and `mmuko_program_main` prints them.

## Separation of concern

```c
#include <mmuko/kernel>  /* TUI / kernel subsystem */
#include <mmuko/module>  /* GUI / plug-in subsystem, e.g. Orion */
```

`make separation-test` enforces the rules at two levels:

- **Source level:** kernel code includes neither `<mmuko/module>` nor any control-plane header (`scheduler.h`, `program.h`, `stack.h`) nor the architecture-private context headers. The same holds for module code with respect to kernel. Shared infrastructure (`src/context`, `src/adapter`) includes no concern at all.
- **Link level:** `nm -u` on kernel objects must show no `mmuko_module_*` or control-plane symbols, and vice versa.

Both subsystems depend only on the neutral `<mmuko/context.h>`, `<mmuko/adapter.h>` and `<mmuko/channel.h>`.

## Data plane (DOP adapter)

Context switching decides *who owns the CPU*. The adapter decides *what data crosses*. Kernel and module exchange only fixed 128-byte `MMKA` records: explicit version, big-endian fields, source/target cluster, sequence, ontology tag, bounded payload, and CRC-32. Inside the domain these records travel through `mmuko_channel`, a non-blocking duplex ring. An endpoint can only send as its own cluster, and it rejects corrupt or misrouted records. No struct or pointer from either subsystem crosses this boundary. The same wire contract also has a POSIX socket binding (`mmuko_adapter_send_fd` / `recv_fd`).

## Freestanding core

The context core, both ABI bindings, the scheduler, the topology checks, the adapter codec, the in-domain channel, and the kernel boot phases use no libc. `make freestanding-check` compiles them with `-ffreestanding -fno-builtin -fno-pic -nostdinc` and verifies they reference no symbol outside `mmuko_*`. Only the program's process layer, the stack allocator, and the subsystems' `printf` logging are hosted.

## Build and test (Linux / WSL)

```sh
make                    # build/mmuko for the host architecture
make demo               # run and print the full log
make test               # unit + context + separation + symbols + integration
make context-test       # context core, ABI layout, register preservation, scheduler
make fault-test         # context-level (kernel, module) and process-level (domain) faults
make separation-test
make freestanding-check
make aarch64-test       # cross-build with aarch64-linux-gnu-gcc, run everything under qemu-aarch64
```

On Windows, run the build inside WSL. The Makefile deliberately rejects native PowerShell/cmd because the process-isolation layer uses POSIX `fork`, `waitpid`, and `mmap`.

Fault injection is controlled by `MMUKO_FAULT_INJECT`:

| value | layer | effect |
|---|---|---|
| `module` | context | module context generation 0 faults at start (code 75). The scheduler re-creates it as generation 1, and the kernel context keeps running untouched. |
| `kernel` | context | the same, for the kernel context |
| `domain` | process | the module context kills the process with SIGKILL. The supervisor restarts the whole execution domain. |

## Explicit non-goals

- No NSIGII, trinary, or enzyme logic. The supplied NSIGII pseudocode is excluded reference material.
- Not a general coroutine library: contexts exist only for the program/kernel/module topology.

See `docs/DESIGN.md` for rationale and `TEST_RESULTS.md` for measured results.
