# Test results

Validated 2026-10-08 in WSL Ubuntu on x86-64: GCC 15.2.0, binutils 2.46, GNU Make, `aarch64-linux-gnu-gcc` 15.2.0, and `qemu-aarch64` 10.2.1. Every C and assembly file builds with `-std=c11 -Wall -Wextra -Wpedantic -Werror`.

## x86-64 System V (native)

- `make`: PASS, no diagnostics
- `make test`: PASS
  - adapter codec, CRC corruption rejection, in-domain channel (identity, bounds, corruption, close), fd transport: PASS
  - process-tree and bipartite context-topology validation (valid graph plus 7 rejection cases): PASS
  - entry ABI bindings via `<mmuko/kernel>` / `<mmuko/module>`, boundary contract checks: PASS
  - context ABI layout (static and runtime): PASS
  - resumption: logged order `kernel 0, module 0, kernel 1, module 1, kernel 2, module 2`; each entry function entered exactly once; `&counter` identical across resumptions; local buffers intact: PASS
  - independent stacks: every sampled SP is 16-byte aligned and inside its own context's stack, never the program stack: PASS
  - lifecycle: NEW/READY/RUNNING/SUSPENDED/FAULTED/TERMINATED transitions, ownership refusal, FAULTED/TERMINATED rejection, canary breach rejection, re-creation: PASS
  - register preservation via assembly probes (`rbx rbp r12–r15`, MXCSR, x87 CW, both directions): PASS
  - scheduler: round robin, mid-run module fault with kernel untouched, budget exhaustion, control-plane startup fault, stack-canary breach, invalid-topology refusal: PASS
  - separation, source and link level: PASS
  - required symbols (`main`, `mmuko_program_main`, `mmuko_kernel_main`, `mmuko_module_main`, `mmuko_context_init`, `mmuko_context_switch`, `mmuko_context_yield`, `mmuko_kernel_context_entry`, `mmuko_module_context_entry`): PASS
  - integration: the full scheduler/kernel/module sequence in order, one entry per main, two resumptions each, a P2P message both ways, no faults: PASS
- `make fault-test`: PASS
  - `MMUKO_FAULT_INJECT=module`: module generation 0 FAULTED (75), re-created as generation 1, kernel entered once and never faulted
  - `MMUKO_FAULT_INJECT=kernel`: mirror image
  - `MMUKO_FAULT_INJECT=domain`: SIGKILL inside the module context, domain exits with code 137, supervisor restarts the domain, generation 1 completes
- `make freestanding-check`: PASS. 9 objects; no undefined symbols outside `mmuko_*`, including no `memcpy` or `memset`.

## AArch64 AAPCS64 (cross-built, run under qemu-aarch64)

- `make aarch64-test` (runs `test`, `fault-test` and `freestanding-check` for AArch64): PASS. This includes register preservation for `x19–x29`, `d8–d15` and FPCR.

## Build variants (x86-64, `make test fault-test`)

- `-O0`: PASS
- `-O3 -fsanitize=undefined -fno-sanitize-recover=all`: PASS
- `-O2 -fno-omit-frame-pointer -fstack-protector-all`: PASS

## Mutation checks (each defect must fail `make context-test`)

| Injected defect | Result |
|---|---|
| x86-64: `r15` not restored | killed (crash) |
| x86-64: MXCSR not restored | killed (register-preservation mask) |
| x86-64: initial stack misaligned by 8 | killed (alignment check) |
| resume re-enters `entry` from the top instead of resuming | killed (trace order / entry count) |
| FAULTED contexts accepted by resume | killed (trap) |
| AArch64: `d8/d9` not restored | killed (register-preservation mask) |
| AArch64: `x27/x28` not restored | killed (register-preservation mask) |
| AArch64: FPCR not restored | killed (register-preservation mask) |

The unmodified tree passes the same harness. Separation-test violations were also injected (kernel including `<mmuko/module>`, module including the arch layout header, kernel including `scheduler.h`, and a kernel object calling a module symbol without any include). All four were rejected.

## Not run

- Clang was not installed in this environment, so the earlier Clang runs were not repeated.
- Native AArch64 hardware: the AArch64 path was exercised only under qemu-user.
