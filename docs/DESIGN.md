# MMUKO bilateral dual-entry design

## 1. Entry contract

| Layer | Symbol | Responsibility |
|---|---|---|
| Process | `main` | unbuffered stdio, then `mmuko_program_main` |
| Control plane | `mmuko_program_main` | process topology, supervisor hand-off, context registration, topology validation, scheduling |
| Execution plane, Cluster A | `mmuko_kernel_main` | MMUKO boot state, kernel/TUI concern |
| Execution plane, Cluster B | `mmuko_module_main` | module registry and GUI concern (Orion) |
| Shared infrastructure | `mmuko_context_*`, `mmuko_adapter_*`, `mmuko_channel_*` | owns no concern |

Two layers of assembly binding remain thin. They carry no policy:

- `mmuko_kernel_entry_asm` / `mmuko_module_entry_asm` bind the C entry contract to the platform calling convention. This binding existed before contexts did.
- `mmuko_context_switch` / `mmuko_context_start` / `mmuko_context_exit_trampoline` save and restore the ABI's callee-saved state.

The full path into a subsystem is:

```text
mmuko_context_resume(program, kernel)
  -> mmuko_context_switch                (asm: save program, load kernel)
  -> mmuko_context_start                 (asm: first switch-in only)
  -> mmuko_kernel_context_entry          (C trampoline)
  -> mmuko_kernel_entry_asm              (asm ABI binding)
  -> mmuko_kernel_main                   (entered once per context generation)
```

## 2. Process isolation versus execution contexts

These are different mechanisms, and the code keeps them apart.

**Process isolation** (`src/program/mmuko_supervisor.c`) uses `fork`, `waitpid` and a restart budget. Its unit is the execution-domain process, and it sees only exit statuses.

**Execution context** (`src/context`, `arch/*/mmuko_context_switch.S`) consists of saved callee-saved registers, a stack pointer and a resume address on a stack owned by the context. Its unit is one subsystem.

A register-level switch is only meaningful inside one address space, so all three contexts live in the same execution-domain process. The bootstrap and supervisor processes never hold or switch a context. No process's stack pointer is ever loaded in another process.

The trade-off is explicit. The original design put Cluster A and Cluster B in separate processes. The context model puts them in one scheduling domain, so they can switch, with these consequences:

- *Contained at context level:* a context exiting with a non-zero code, a control-plane fault, or a stack-canary breach. The faulted context is marked `FAULTED`, never resumed, and re-created as a new generation. The other context keeps its frame and is never restarted.
- *Contained at process level:* a hard crash inside either context, such as a signal or a guard-page overflow. It takes down the domain process, and the supervisor restarts the whole domain.

Stacks come from `mmap` with a `PROT_NONE` guard page below each one, so overflowing one context's stack faults instead of silently writing into the other's.

## 3. Context lifecycle

```text
          init                first resume / resume
  NEW ----------> READY ---------------------------> RUNNING
                    ^                                 |   |
     re-create      |                         yield  |   | exit(0) / entry returns
  (new generation)  |                                 v   v
  FAULTED ----------+                         SUSPENDED   TERMINATED
     ^                                            |
     | exit(code != 0), canary breach,            | resume
     | control-plane mark_faulted                 v
     +--------------------------------------- RUNNING
```

- Only an owned `READY` or `SUSPENDED` context can be resumed, and only by its owner. The kernel and module are both owned by the program context, so neither can resume the other.
- `FAULTED` and `TERMINATED` contexts are refused without switching.
- `mmuko_context_init` refuses to overwrite a `RUNNING` or `SUSPENDED` context, because that would discard a live frame. Re-creation therefore always follows a fault or termination.
- Leaving a context goes through `mmuko_context_exit`, either directly from the trampolines with the subsystem's return code or from the assembly exit trampoline when `entry` returns. `mmuko_context_exit` switches to the owner and then traps: a dead frame is never re-entered.

## 4. Why cluster colour and process depth are different axes

The process tree is two-coloured by depth parity (`mmuko_topology_validate_tree`). The context plane is coloured by concern: the program context is neutral, the kernel context is A, and the module context is B (`mmuko_topology_validate_contexts`).

The validator checks:

- exactly one neutral vertex, which is the program context;
- role, cluster and colour agree (kernel ↔ Cluster A ↔ A, module ↔ Cluster B ↔ B);
- every control edge runs from the neutral vertex to an execution context, and every execution context has exactly one;
- every peer edge joins A to B and never touches the neutral vertex, so the execution plane is bipartite.

The scheduler builds this graph from what was actually registered (owners and channel endpoints) and refuses to run if it is invalid.

## 5. Data plane

The DOP adapter's rule from the earlier design still applies: transfer structure and meaning, never native machine state. Kernel and module exchange encoded `MMKA` records through `mmuko_channel`, a pair of rings holding wire bytes. The channel enforces:

- the source must be the sending endpoint's own cluster;
- the target must be the receiving endpoint's cluster;
- magic, version, CRC and payload bounds must hold;
- a malformed record is consumed and reported, so it cannot wedge the ring.

The channel never blocks. A subsystem that finds nothing to read continues and yields at its next iteration, so execution switching and data exchange stay separate decisions.

## 6. Ontological context

Every adapter record carries a bounded ontology tag such as `mmuko.kernel.boot` or `mmuko.module.gui.orion`. The tag is transport metadata, not executable logic.

## 7. Module system

The module registry is local to Cluster B. A descriptor carries an ABI version, name, ontology tag, `start`, and `render`. The module context drives Orion across resumptions (`start` once, then one `render` per iteration) on its own stack. The kernel never calls Orion and never includes `<mmuko/module.h>`.

## 8. Freestanding core

The context core, the ABI bindings, the scheduler, topology, the adapter codec, the channel and the kernel boot phases use no libc. `make freestanding-check` builds them with `-ffreestanding -fno-builtin -fno-pic -nostdinc` and checks their undefined symbols. Hosted pieces are limited to the POSIX process layer, the stack allocator and `printf` logging, so the same core can later move under a bare-metal platform layer.

## 9. No NSIGII implementation

No NSIGII state algebra, trinary composition, firmware hand-off or enzyme lifecycle logic is included.
