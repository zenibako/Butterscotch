# Sequential Pocket optimizations implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `subagent-driven-development` to implement and review one topic at a time. Do not implement topics in parallel.

**Goal:** Evaluate and implement supported collision, tile-shift, text, and load-time optimizations in the user's requested order.

**Architecture:** Keep existing fallbacks and synchronous ownership. Measure each
retained predecessor before selecting the next local change; do not manufacture
a cache or packed format when the measured cost does not justify it.

**Tech Stack:** C runner and legacy C++ compatibility, desktop sanitizer fixtures,
RV32 GCC, staged openfpgaSDK, independent os25 replay.

**Spec:** [Sequential design](../specs/2026-10-17-sequential-optimization-design.md).

## Global Constraints

- Preserve Accuracy pixels, same-mode Speed pixels, PCM/pairs, event order, ownership, normal saves, Memories, sleep and snapshot wire format/eligibility.
- Preserve existing projects, evidence, original assets and SDK/runtime; no VM changes, pruning, SD writes, publishing or remote pushes.
- Execute topics serially; use at most two compiler processes and check disk before cases.
- Test both WAD16 and WAD17 for shared code. Label all RTL timing simulated.
- Require positive/negative replay controls and cross-revision raw-output comparison for retained gameplay changes.
- Make separate conventional, GPG-signed topic commits, with reports for rejected candidates.

### Task 1: Prepare precise pair scans

**Files:** Modify `src/collision.h`; create `src/openfpga/tests/collision-precise.c`; modify `src/openfpga/Makefile` and `src/openfpga/README.md` to expose the fixture; create `src/openfpga/docs/2026-10-17-precise-collision-results.md` after measurement.

**Interfaces:** Consume existing `Collision_pointInInstance(Sprite*, Instance*, GMLReal, GMLReal)` as unchanged scalar behavior, and `Collision_instancesOverlapPrecise(Runner*, Instance*, Instance*, InstanceBBox, InstanceBBox)` as the public pair interface. Produce pair-local preparation only; no persistent state or snapshot interface changes.

- [x] Add the real pair-vs-scalar differential fixture described by the spec; establish baseline behavior in both games before changing the production loop.
- [x] Add a meaningful work-bound check that fails baseline repeated mask setup (test-only observation; no timing reads), then observe the intended failure.
- [x] Add lazy stack-local preparation and replace only the pair loop's point evaluations. Keep division/arithmetic/casts and all short-circuit gates.
- [x] Run the fixture with ASan/UBSan for both WADs, existing collision-order and snapshot checks; inspect legacy compilation and RV32 emitted code.
- [x] Review the exact task diff. Fix findings before timing. Controller handles commits after verification; implementer leaves production/test changes ready for review.
- [x] Export baseline/candidate committed revisions. Screen Deltarune dodge Accuracy timing with exact existing configuration, repeated captures and positive/negative controls. Compare raw RGB555/PCM/pairs independently.
- [x] For a retained candidate, add Speed dodge and affected traced/shared-game checks. Record cost/memory, inconclusive effects and limitations. Keep or reject based on evidence; sign the topic and report commits.

### Task 2: Reduce remaining tile-shift work

**Files:** Measurement in private exports; select `src/runner.c`/`runner.h`/existing structural writers for an index, OR `src/openfpga/platform/game_deltarune.c` for a guarded native script. Tests extend the layer fixture or a new native-script fixture; dated report under `src/openfpga/docs/`.

**Interfaces:** Consume Task 1's retained commit as baseline. Preserve `Runner_findLayerElementById`, its invalidation contract, existing native registration length/hash guards, and interpreted fallback.

- [x] Count post-merge first searches/comparisons, native/interpreted script calls and work in spades 490–492 on the retained predecessor. Keep counter-on/off output controls.
- [x] Record the measured choice and concrete local design in the ledger before implementation. If neither direction has enough work to justify complexity, record rejection with counts and commit its report.
- [x] For an index, test ordered duplicates/misses and every writer/restore invalidation; for a native script, compare real script state/outputs with `BS_NO_NATIVE=1`, including wrong hash/length fallback. Add a failing work-bound test before production edits.
- [x] Implement only the selected candidate, validate semantics with both affected game configurations and sanitizers, and review before timing.
- [x] Screen spades Accuracy, then Speed and affected trace/shared-game regressions if retained. Preserve raw outputs and independent source provenance. Record and sign the result before Task 3.

### Task 3: Reduce dialogue/string traversal

**Files:** Private counting adapter; selected existing string/text functions in `src/vm_builtins.c` or `src/text_utils.*`; extend `src/openfpga/tests/string-reads.c` or add a focused text fixture; dated report.

**Interfaces:** Consume Task 2's retained revision. Preserve `RValue` input/result ownership, conversion contract, UTF-8 and malformed-input semantics; no persistent string cache.

- [x] Count repeated byte/codepoint traversal on the retained revision in dialogue and battle text, controlling counters and deterministic input. Rank reached routines; do not substitute old profile times for current measurements.
- [x] Record one selected routine/optimization with a concrete design and expected work removal, or reject with measured evidence.
- [x] Add boundary, malformed UTF-8, non-string conversion and ownership cases plus a work-bound check; observe the intended pre-change failure.
- [x] Implement the local scan reduction, run both games' string/builtin/snapshot checks under sanitizers, and review ownership/encoding before timing.
- [x] Run affected real-text pixel/state comparisons and a relevant fixed-audio replay screen where covered. Record missing workload coverage honestly and sign the retained/rejected result before Task 4.

### Task 4: Reduce loading hitches

**Files:** Private startup/chunk/decode analysis; select existing `src/data_win.c` or texture pack reader/packer only after measurement; focused loader/format regression tests and dated report.

**Interfaces:** Consume Task 3's retained revision; retain existing uncompressed/packed asset compatibility and original files. New packed representation, if justified, is opt-in and verified against exact original decoded bytes.

- [x] Measure actual startup/read/decode byte counts and redundant reads; estimate cold-read benefit separately from decode cost and peak memory.
- [x] Record the smallest supported design. Prefer a local redundant-read/decode removal; assess compression with RV32 cycles if packing is necessary. Do not claim host-only compression ratios prove a win.
- [x] Add exact-byte and malformed/truncated input tests and a failing bound for the selected waste before production edits. If no candidate meets storage/CPU/memory constraints, preserve measurements and commit a rejection report.
- [x] Implement and verify lossless output, existing format fallback, failure cleanup, startup/room behavior and unchanged original assets. Review before any optional pack generation.
- [x] Run relevant loader/texture/snapshot regressions and both native builds for any retained shared change. Record the lack of physical SD latency evidence and sign the result.

### Task 5: Final verification and review

**Files:** Complete dated topic reports and a combined result index; no runtime/SDK edits.

- [ ] Independently review the complete retained diff, resolving actionable findings.
- [ ] Run affected aggregate fixtures and the snapshot suite once on the final tree; build Undertale and Deltarune native ELFs with the paired compiler/SDK.
- [ ] Verify clean signed commits, unchanged runtime/asset hashes, no user changes lost, and retained exact evidence. Report completed, rejected and unmeasured scope without claiming Pocket speedups.

## Shared-file preflight

| Tasks | Shared surface | Resolution |
| --- | --- | --- |
| 1 / 2 | Runner includes collision header; both affect Step work | Serialize; capture exact retained predecessor for Task 2. |
| 2 / 3 | Native registration or builtins | Select one tile design from counts; finish its review/commit first. |
| 3 / 4 | Data/strings may share loading ownership | Preserve interfaces and original files; measure Task 4 on retained Task 3. |
| 1–4 / 5 | Makefile, README, reports and source snapshots | Controller owns shared edits/commits and final integration. |
| 1 | Behavior characterization versus optimization | Scalar comparisons pass baseline; separate work bound must fail before change. |
| 2–4 | Candidate cannot be selected honestly before counting | Discovery and evidence-backed selection/rejection are explicit deliverables, not permission to invent an unmeasured optimization. |
| 5 | Shared SDK/runtime must remain immutable | Verify handoff hashes and use fresh outputs; no firmware or FPGA rebuild. |
