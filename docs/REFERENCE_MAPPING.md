# Reference-material mapping

This implementation was developed against all supplied source bundles, with each bundle used for the part of the design it materially informs.

## `mmuko-boot-main`

Used for the MMUKO computational boot model, naming, the existing `mmuko_program_main` concept, and the distinction between hosted C and freestanding boot semantics. `src/kernel/mmuko_boot.c` carries forward the cubit-ring, superposition, frame-of-reference, nonlinear-resolution, and rotation-check phases. Like the reference `kernel.c`, it has no libc dependency. The context core, scheduler and data plane follow the same freestanding discipline (`make freestanding-check`).

`mmuko.psc` was reviewed only to identify and exclude NSIGII-specific logic. None of its trinary/enzyme/NSIGII handoff behaviour is implemented.

## `mmuko-structural-abi-snapshot-main`

Used for the principle that a calling/wire boundary is a structural contract. The two assembly entry bindings preserve the C function contract, and the peer adapter uses fixed-width big-endian fields plus CRC rather than native structs, pointers, stack frames, or machine byte order.

The execution-context switch applies the same idea to calling conventions. One architecture-neutral contract (`include/mmuko/context.h`) has one binding per ABI: x86-64 System V and AArch64 AAPCS64. Each binding saves exactly its ABI's callee-saved set, and both are held to identical behavioral tests: resumption order, single entry, alignment, and register preservation. Machine layouts stay in the arch directory, as the snapshot project keeps register maps out of its contract.

## `dop-adapter-main`

Used for the data-oriented adapter boundary and the separation of declarative state/data from the concrete execution/rendering concern. In this repository, transport messages are data records; the kernel TUI behaviour and module/GUI behaviour remain outside those records.

## `plp-main`

Used narrowly for context-bearing data, frame-of-reference, tree discipline, and peer-oriented coherence concepts. The MMUKO wire contract therefore carries a stable ontology/context tag. Application-specific PLP identity, happiness, art-scale, GDPR, and scoring logic is not imported into the OS runtime.

## Resulting invariant

Reference concepts are adapted only where they support the requested OS feature. They do not collapse the kernel and GUI into one subsystem, and they do not introduce NSIGII logic.
