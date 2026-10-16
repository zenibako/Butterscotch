# Deltarune optimization design and execution order

Status: proposed implementation scope; this change contains planning documents only.
Source baseline: `a6ea5c0532298a14e896a719f3bbb1461ca39141`.

Reduce avoidable CPU work while preserving the current game's pixels, audio,
save-state contents and event ordering. Start with audio shadow maintenance,
then remove temporary copies in three string builtins. Investigate collision
work separately before selecting a collision optimization.

## Existing decisions and evidence

- [Pocket performance guidance](../../../.claude/skills/butterscotch-pocket-perf/SKILL.md)
  makes Accuracy the target, requires measurements before explanations, and
  records failed layout experiments. Small simulated differences can come from
  code layout; deterministic replay does not establish a hardware speedup.
- [Translated tile picture results](../../../src/openfpga/docs/2026-10-14-tile-cache-reuse-results.md)
  establish an existing renderer improvement. Spade Step work still took roughly
  43–46 ms/frame in that device comparison; these older results motivate CPU
  investigation but do not identify current-main bottlenecks.
- [Save-state design](2026-10-12-undertale-savestates-design.md) requires exact
  decoder/resampler state, ownership preservation, and rebuilding derived
  indices. Its original game eligibility limits have since changed; current
  code and [save-state verification](../../../.claude/skills/butterscotch-pocket-verify/references/save-states.md)
  govern present eligibility. These optimizations must not change it.
- Current `mixAndWrite` calls software `__umoddi3` once per accepted stereo
  pair. This was verified in the actual RV32 ELF, not just a desktop profile.
  `string_copy`, `string_char_at` and `string_ord_at` duplicate existing string
  inputs through `RValue_toString`. Collision dispatch snapshots and sorts a
  target population for each self/event. Their frame-time savings are unmeasured.

## Separate workstreams

1. [Audio shadow copying plan](../plans/2026-10-15-deltarune-audio-shadow.md):
   keep the current acceptance checks and maintain the same ring contents using
   contiguous stereo copies. Compute the first ring index once per accepted
   chunk, retaining a scalar fallback for the uint64 counter-overflow boundary.
   No new persistent cursor or snapshot format is needed.
2. [String read plan](../plans/2026-10-15-deltarune-string-reads.md): borrow
   existing string input for the duration of a synchronous builtin call. Own a
   temporary only when a non-string requires conversion. Results retain their
   existing ownership; `RValue_toString` itself keeps its allocation contract.
3. [Collision measurement plan](../plans/2026-10-15-deltarune-collision-measurement.md):
   count sorts, target lengths, comparisons and bounding-box calculations, and
   characterize mutation and callback order. This deliverable is instrumentation,
   regression coverage and a measured decision. It does not authorize a cache
   design without evidence about its benefit and invalidation requirements.

Audio is first because the redundant operation is compiler-confirmed and its
replacement is local. String changes are a separate commit/topic. A collision
cache has greater semantic risk. VM local/argument pooling is deferred because
its cost has not been measured and previous allocation experiments were weak.

## Global constraints

- Preserve raw pixels, PCM samples and pair counts, value ownership, snapshot
  format/eligibility, event order, and callback mutation visibility.
- Preserve the 64-bit accepted-pair total and arbitrary positive ring capacities;
  do not assume the ring capacity is a power of two.
- Keep os25, CPU parameters, SDK, asset packs, native replacements and game
  identifiers unchanged. No picture-changing Speed shortcut is in scope.
- Shared paths require Undertale and Deltarune verification. Accuracy is primary;
  Speed is a separate compatibility/performance check.
- Profile using counters rather than per-operation clock reads. Diagnostic
  builds and ordinary timing builds are separate; never compare their timings.
- Commit each topic separately with GPG signing. Publishing, opening PRs,
  copying to SD, and device installation are outside this plan's execution.

## Shared verification and evidence

Read the repository's build/performance/verification skills and the harness's
`AGENTS.md`, `bench/README.md` and `fullframe/README.md` at execution time.
The established local harness is
`/Users/chanderson/Projects/undertale-pocket/openfpga-os25-opt-20261015`.
Run `python3 run.py verify` there and check `df -h .` before creating cases.
Only about 0.5 GiB was free during discovery. Arrange more space if required;
do not delete historical cases, alter pins, or use the progress checkout's
three mismatched frozen assets.

The private discovery index is
`build/optimization-discovery-20261015/findings.json` under that harness. It
records the exact source, ELF, configuration and case path. The current-main
Accuracy/timing dodge case passed repeated QEMU captures, raw pixel/PCM repeat
identity, three-frame quiet RTL replay and corrupted-state rejection. It is
not a candidate comparison or a whole-game performance result.

For each implementation, export its exact parent and candidate revisions under
fresh snapshot names. Use the launcher's printed case paths. Compare matching
compiler/alignment, native setting, BRAM placement, framebuffer model, clock
cost, audio model, scene and build kind. The discovery settings were GCC 16.2.0,
port profile, 64-byte alignment, natives enabled, BRAM, cached framebuffer,
1,680-cycle modeled clock reads, fixed audio, and quiet traffic.

Screen first with Deltarune dodge timing. If useful, validate each retained
candidate with trace and timing builds in Deltarune field/menu/dodge and
Undertale Flowey, in Accuracy and Speed. Each executable needs repeat captures,
same-ELF RTL oracles, a rejected corrupted-state control, and traced/timing
output agreement. Cross-revision comparisons independently check raw RGB555,
raw PCM and pair counts; compare pointer-bearing state only within one ELF.
Replay the exact retained dodge and Flowey timing ELFs with diagnostic scanout
to check sensitivity to contention. Keep all failures and source/ELF hashes.

Use the maintained launcher, for example from the harness root:

```sh
python3 experiments.py snapshot --name audio-parent-a6ea5c0-r1 --revision a6ea5c0 --repo /Users/chanderson/Projects/undertale-pocket/butterscotch-pr20-fixes
python3 experiments.py run --snapshot audio-parent-a6ea5c0-r1 --game deltarune --window dodge --mode accuracy --kind timing --compiler /opt/homebrew/bin/riscv64-elf-gcc --alignment --native enabled --variant bram --framebuffer cached --clock-cost 1680 --audio fixed
```

For the candidate use its committed revision and a new snapshot name. Read
`experiments.py compare --help`, `report --help`, and `replay --help` for the
exact output/validation commands; supply the case paths printed by the runs.
Do not reuse an incomplete case under a new interpretation.

A candidate passes correctness only with exact outputs and all applicable
semantic fixtures. Reduced operation counts establish removed work, not an FPS
gain. Report simulated cycle changes by window; a 1–3% difference alone is
inconclusive. If the simulated gain is small, require isolated-operation
evidence and a separate Pocket base/candidate/base comparison before claiming
a device benefit. Prepare that comparison only after software checks pass;
deployment remains a separate user instruction.

Record retained/rejected outcomes in a dated `src/openfpga/docs/` report with
revision/configuration, exact cases, per-frame cycles, raw-channel comparisons,
negative controls, memory cost and remaining device questions. Keep private
assets and raw logs out of Git. Retain a candidate only when its evidence
supports the added complexity; an inconclusive result is a valid outcome.
