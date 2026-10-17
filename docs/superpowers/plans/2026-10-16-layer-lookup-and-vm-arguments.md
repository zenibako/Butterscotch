# Layer lookup and VM arguments implementation plan

> **For agentic workers:** Use `subagent-driven-development` to execute the two independent tasks in parallel, with separate reviews and signed commits.

**Goal:** Reduce repeated layer-element searches and redundant VM argument initialization without changing game behavior.

**Architecture:** Retain the existing layer-element linear search as fallback. A Runner-local last-success hint stores indices, validates the current slot, and is invalidated before structural mutations and restore. Separately remove the two stack-argument initializers whose live slots are already fully assigned.

**Tech stack:** Existing C runner, legacy C++ compatibility, desktop ASan/UBSan fixtures, GCC RV32, standalone os25 replay.

**Spec:** [Measured opportunities and constraints](../../../src/openfpga/docs/2026-10-16-next-cpu-opportunities.md), within the [existing optimization design](../specs/2026-10-15-deltarune-optimization-design.md). The user authorized both implementations in parallel after a baseline build.

## Global constraints

- Preserve pixels, PCM/pair counts, ownership, event order and mutation visibility.
- Preserve first-match/missing-ID and `outLayer` behavior, including duplicate IDs.
- Preserve snapshot format and eligibility. Lookup hints are derived state.
- Keep Accuracy fidelity, Speed behavior, native replacements and game identifiers unchanged.
- Keep SDK/OS roots read-only; no card writes, deployment or publication.
- Keep legacy compiler compatibility. No new dependencies or allocation pool.
- Use explicit source revisions, fresh cases and independent raw-channel comparisons.
- Limit concurrent compilation to two processes; each implementation agent gets one.
- Commit topics separately with GPG signing; preserve old evidence and failures.

## Baseline and coordination

- [x] Build both RV32 ELF targets from rebased `50e8379`; retain hashes and executables privately.
- [x] Verify the existing isolated worktree and clean starting state.
- [x] Read architecture/performance decisions and verify the harness's frozen inputs.
- [x] Assign disjoint implementation ownership. Coordinator owns shared build/docs edits and commits.

### Task 1: Layer-element lookup

**Files:** `src/runner.c`, `src/runner.h`, `src/runner_snapshot.c`, structural layer writers in `src/vm_builtins.c`, new `src/openfpga/tests/layer-lookup.c`.

**Interface:** Preserve `Runner_findLayerElementById(Runner*, int32_t, RuntimeLayer**)`. Add a documented invalidation helper for structural writers. Store only a validity flag and layer/element indices; recompute pointers from current arrays.

- [x] Characterize returned pointers, owner output, misses, empty arrays and duplicate-ID order using real lookup and mutation paths.
- [x] Add an operation-count bound that fails for repeated full scans on the baseline.
- [x] Validate hint indices and ID before a hit; otherwise perform the unchanged ordered scan and refresh or clear the hint.
- [x] Invalidate before all live structural writers: room construction/replacement/restart, instance insertion/removal/depth moves, persistent-room detach, layer creation/destruction, element creation/destruction/movement and snapshot restore.
- [x] Test insertion before a cached duplicate, deletion, move, reallocation, replacement, stale/detached hints and actual builtin writer paths. Require both baseline characterization and optimized work-bound checks.
- [x] Verify both games, sanitizers, snapshot behavior and legacy compilation. Review writer coverage independently before keeping the cache.
- [x] Record memory/layout effects and commit the implementation separately.

The hint does not infer validity from repeated IDs alone. An unannounced earlier duplicate could preserve a cached slot's ID while changing the correct first match; explicit invalidation is therefore part of its contract.

### Task 2: VM argument initialization

**Files:** two declarations in `src/vm.c`; new `src/openfpga/tests/vm-call-args.c`.

**Interface:** Preserve all VM public interfaces, argument pointers/counts, allocation and cleanup. The production edit is:

```c
RValue stackArgs[VM_MAX_STACK_ARGS];
```

in both `handleCall` and `handleCallV`, replacing their zero-initialized declarations. Do not change the overflow `safeCalloc`, locals, argument growth, independent ownership or return strengthening.

- [x] Add real-bytecode characterization for CALL/CALLV at zero, 16 and 17+ arguments; check argument order/count and null zero-argument pointer.
- [x] Exercise strings/arrays/methods/structs where supported, nested script calls and event re-entry while arguments remain live.
- [x] Run characterization on baseline before removing initializers; repeat afterward with both games and sanitizers.
- [x] Confirm the baseline RV32's two unconditional 128-byte clears are absent from the actual candidate ELF. Do not use a source-text assertion as a performance test.
- [x] Review ownership and all paths accessing arguments; commit this topic separately.

## Coordinator integration and measurement

- [x] Add and document both fixture targets in `src/openfpga/Makefile` and `README.md` in their respective topic commits.
- [x] Review each exact task patch independently, then review the combined implementation.
- [x] Rebuild both game configurations after shared header changes; run focused fixtures, strict lint, core legacy syntax and CMake warnings-as-errors checks as applicable.
- [x] Create fresh baseline, VM-only, layer-only and combined snapshots. Use a signed baseline-parent measurement commit for layer-only isolation; private nested patch exports silently skipped changes and are excluded. Verify exported source bytes against Git before builds and comparisons.
- [x] Screen Deltarune spades Accuracy timing first. Use the already reviewed private scene adapter that handles removed contrast fields, preserving scene/profiling assertions.
- [x] Require repeated QEMU capture, positive same-ELF RTL replay, rejected negative oracles and independent cross-revision raw RGB555/PCM/pair comparison.
- [x] Validate the retained combined implementation with Speed spades, traced Deltarune dodge/spades and Undertale Flowey coverage; pair trace/timing evidence where available. Record the actual bounded coverage rather than claiming a full matrix.
- [x] If an apparent gain is small, establish removed machine work and use a same-ELF control where necessary; do not infer Pocket FPS.
- [x] Build final RV32 ELFs and write a dated results report with exact revisions, hashes, cases, costs and limitations. Physical Pocket testing remains separate.

## Shared-file preflight

| Tasks | Shared interface or file | Resolution |
| --- | --- | --- |
| Layer lookup / VM arguments | VM fixture consumes Runner headers changed by layer work | Rebuild all affected objects before final verification; distinct source/test ownership. |
| Both tasks / coordinator | Makefile and README fixture targets | Coordinator edits and commits each target with its topic. |
| Both tasks / measurement | Same branch, compiler and storage resources | No agent commits or expensive harness runs; coordinator exports Git objects and limits build concurrency. |
| Layer task | Validated indices versus duplicate-first-match semantics | Invalidate every structural writer; slot validation alone is insufficient. |
| VM task | Existing behavior already passes before performance edit | Characterization passes both revisions; RV32 clear removal supplies independent work evidence. |

Detailed commands, intermediate results and decisions belong to this plan's private execution ledger. Retain historical evidence under its original source identity after rebase.
