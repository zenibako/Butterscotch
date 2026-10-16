# Deltarune CPU optimization results — 2026-10-15

Two local changes remove established redundant work: audio shadow maintenance
copies accepted PCM in ring spans, and three read-only string builtins borrow
existing input strings. Collision measurements do not justify a target cache or
maintained sorted populations. The combined replay matrix and contention checks pass. Retain these local
candidates on the strength of proven work removal and compatibility checks;
a physical Pocket benefit remains unmeasured.

This implements the [shared design](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md)
and its three linked plans. The existing
[tile-cache results](2026-10-14-tile-cache-reuse-results.md) motivated further
CPU investigation, but do not identify the cost of these particular operations.
No Pocket performance improvement has been established by this work.

## Source and attribution

The common baseline is `a6ea5c0532298a14e896a719f3bbb1461ca39141`.
Implementation parent `f9d5442a315972456ceae7623d57ff947f896d9e` adds planning
documents only; its `src` and `vendor` content equals that baseline. Integrated
code under test is `2ee841c27a94c7dfe854f1ab426cae462119efb3`.
Subsequent report and plan-status commits do not change that code.

| Topic | Independent implementation | Integrated commit | Evidence |
| --- | --- | --- | --- |
| Audio shadow | `b596d43cc24f00d0d8e3eb5efce32a634631798b` | `f21750c` | [Audio report](2026-10-15-audio-shadow-results.md) |
| String reads | `8540044dc32cdc3101b4fd5ec7ec9a0a5b334956` | `518d7fe` | [String report](2026-10-15-string-read-results.md) |
| Collision diagnostics and fixtures | `55d9d22d84557920974b56a6708edfa83540568c` | `2ee841c` | [Collision decision](2026-10-15-collision-work-results.md) |

The user authorized parallel work. Each topic used a separate worktree and
independent specification/quality review before integration. Audio and strings
have separate initial dodge timing screens. The full matrix compares the
combined candidate with the common baseline; its timings cannot be attributed
to either change separately. Collision instrumentation is compiled out of the
ordinary timing builds.

## Established work removal and collision decision

Audio's normal path replaces one software 64-bit remainder per accepted stereo
pair with one initial remainder per accepted chunk and contiguous copies. For
800 pairs split 256/256/256/32, that is four index calculations instead of 800.
Actual RV32 disassembly confirms the change. The original scalar behavior
remains at uint64 counter overflow, preserving arbitrary positive capacities.
Queue acceptance, mixer arithmetic, snapshot format and persistent memory stay
unchanged.

Strings retain independently owned results and non-string conversions. The
measured Deltarune route reaches 37,834 existing-string calls to the changed
builtins, removing that many input allocations; Undertale reaches 113,084.
These are whole diagnostic-route counts, not calls within the three-frame
timing windows. Both games, modes and native settings agree on those counts.
UTF-8 scans and result allocations still happen.

Collision samples sort at most two targets. Dodge has four host qsort
comparisons per sampled frame; Flowey sorts five singleton lists; the recreated
spade chase has no sorting through this dispatcher. Exact population sequences
also change during dodge. Keep the current dispatcher and defer both sorting
and bbox caches. These samples do not measure other collision APIs or prove
where the remaining Step time is spent.

## Semantic and host verification

The integrated snapshot suite, both games' port lint, and fresh ASan/UBSan
string-read, collision-work, builtin-compatibility and profiler fixtures pass.
The string fixture checks 148 behaviors/ownership cases per game with zero
input-allocation failures. Audio checks 210 appends against a scalar oracle and
20 backend acceptance cases, including overflow and restore-then-append.
Collision's separately reviewed fixtures run with diagnostics on and off for
both WAD versions, exercising callback mutation, order and nested arenas.
Final independent code review reports no actionable findings.

The host string study has 21 selected pixel comparisons; collision has 30
diagnostic-on/off and 15 same-ELF repeat pixel frames, plus exact event/list
streams. These are selected scene checks. Desktop PCM depends on wall time
and differs even on same-ELF repeats, so it is excluded from cross-build audio
acceptance. Flowey's `obj_time.idle_time` also varies in same-ELF repeats.
Pointer/time-bearing host dumps are not full-state equality oracles.

Real-game host sanitizer runs report an existing vendored `stb_ds.h` signed
shift warning; the recreated spade route additionally reports a float-to-int
range warning in unmodified `rvalue.h`. Retained runs have no ASan memory
finding. The focused semantic fixtures pass without runtime sanitizer errors.
Those existing gameplay warnings remain unresolved and are documented in the
topic reports. All private diagnostic source edits were restored to committed
bytes before reporting.

## Simulated timing and final replay gate

The isolated Accuracy dodge screens use the same verified baseline, GCC
16.2.0, 64-byte alignment, native replacements enabled, BRAM interpreter,
cached framebuffer, modeled 1,680-cycle clock reads, fixed 800-pair audio and
quiet traffic. Values are simulated at 100 MHz.

| Isolated screen | Mean simulated ms/frame | Difference from 108.289930 ms baseline |
| --- | ---: | ---: |
| Audio only | 107.895753 | -0.394177 ms (-0.364%) |
| Strings only | 108.705197 | +0.415267 ms (+0.383%) |

Both screens preserve raw RGB555, PCM and pair counts across revisions, pass
same-ELF replay and repeat captures, and reject corrupted-state controls.
Each also has a direct byte comparison of all 12 raw pixel/PCM files across
two captures and three frames. The small cycle differences are inconclusive.
Reduced operation counts establish removed work, not a gameplay speedup.

The complete quiet matrix passes: 32 selected ELFs, 64 QEMU captures,
32 positive same-ELF RTL replays and 32 rejected corrupted-state controls.
It covers baseline and combined candidate × Deltarune field/attack-meters/dodge
and Undertale Flowey × Accuracy/Speed × trace/timing. All 16 cross-revision
and 16 trace/timing comparisons agree on raw RGB555, raw PCM and pair counts.
Direct reads also pass all 768 cross-pair raw byte checks (all four repeat
pairings) and 192 within-case repeat checks. Interrupted attempts and the extra
same-source control are recorded separately, not counted as selected passes.

| Game/window | Mode | Baseline simulated ms | Combined simulated ms | Change |
| --- | --- | ---: | ---: | ---: |
| deltarune field | accuracy | 76.466773 | 75.848473 | -0.809% |
| deltarune field | speed | 42.247337 | 40.748443 | -3.548% |
| deltarune bmenu | accuracy | 74.487737 | 74.167657 | -0.430% |
| deltarune bmenu | speed | 45.939443 | 44.726907 | -2.639% |
| deltarune dodge | accuracy | 108.289930 | 107.828377 | -0.426% |
| deltarune dodge | speed | 63.266710 | 62.003760 | -1.996% |
| undertale flowey | accuracy | 23.777740 | 22.821890 | -4.020% |
| undertale flowey | speed | 12.738590 | 11.916243 | -6.456% |

These are bundled three-frame window measurements. The small Accuracy
field/menu/dodge changes are inconclusive. A same-source Flowey Speed rebuild
also changed timing by approximately -0.78%, despite unchanged source; that
single control establishes same-source timing variation, whose cause is not
identified here. It is consistent with layout/path sensitivity and is not a
statistical bound on it. Even the larger Flowey bundle changes require separate Pocket
base/candidate/base testing before claiming a device benefit.

All eight exact retained timing-ELF diagnostic scanout replays pass:
Deltarune dodge and Undertale Flowey × both modes × both revisions. These add
24 checked frame rows. Mean simulated changes under modeled scanout are
-0.582%/-1.923% for dodge Accuracy/Speed and -3.190%/-6.245% for Flowey.
The effect remains a bundle measurement under modeled contention.

Linked timing ELF text increases by 96–232 bytes across matching cases; data
and BSS remain identical. `memory.json` records all 16 timing size rows. This
is static section-size evidence, not peak heap/stack measurement. The changes
add no ordinary-build persistent cursor, buffer or diagnostic record.

Oracle scope: trace builds check every oracle field. Timing builds exclude
field 3, the in-loop PCM hash, while retaining the fixed audio pair-count
check. Timing opcode/event/trace fields are compiled-out constants and do not
establish dynamic tracing. Enabled same-ELF state, pixels, pairs, cleanup and
memory checks still apply. Cross-revision checks compare raw QEMU RGB555/PCM
and pair counts; pointer-bearing state is not compared across binaries.

## Evidence, storage and limits

The independent private harness is
`/Users/chanderson/Projects/undertale-pocket/openfpga-os25-opt-20261015`.
Its 1,631 frozen input checks pass; SDK, private assets and pins were not edited.
Isolated evidence is under `build/optimization-discovery-20261015/`; the shared
matrix and incident log are under `build/optimization-matrix-20261015/`.
Its `inventory.json` records all 32 exact case paths, source revisions, ELF,
snapshot and capture SHA-256 values, and per-frame cycles. `quiet-results.json`
and `scanout-results.json` are maintained validating aggregates. All 32 cases
also have individual reports and original captures. Two interrupted cases
produced no successful RTL result and remain excluded; one extra successful
same-source control is listed separately in `incidents.json`.

Final private evidence hashes:

| File | SHA-256 |
| --- | --- |
| `quiet-results.json` | `a53bc2896ca2f37b848aee972b7c30e5d47822f17b08d5ab83d4c72c793266d6` |
| `scanout-results.json` | `c3557e25fa6628af3de3e9e38199f6aef250e5109b913685262d4b630054988d` |
| `inventory.json` | `7ab1c5b905115ff8b33a882cc6ab3886392a52c4122ceb32873cb1af15db6748` |
| `memory.json` | `71ce3d638c933de43e01cfb53d750f196cc0a1f7b7050620f151ba6509d5445c` |
| `direct-raw-comparisons.json` | `bfedc0d90192fd060ad7018230035ea4d5b8e705824ebc19257e1e5216e51701` |
| `incidents.json` | `f0edcc4be87785adf55e3a4b650064e5cf7b516e140049def412439795346707` |

Per-topic reports name exact cases, hashes and reproduction commands. Private
assets, binaries and raw game captures are excluded from Git.

At the user's request, old ignored objects/debug bundles, duplicate staged
asset packs and obsolete desktop test executables were removed to make room.
Approximately 4 GiB of logical generated files were removed. Every deleted
staged pack was byte-hash-equal to a retained original; active builds, source,
save files, Pocket ELFs, evidence and existing user changes were preserved.
Detailed manifests remain in `.superpowers/sdd/integration-20261015/`.
New private repeat captures may use verified APFS copy-on-write clones to
preserve every byte while reducing physical storage. Representation changes
and interrupted attempts are recorded in the matrix incident log.

The harness models OS services, allocator, storage and virtual clocks on pinned
os25 CPU/memory RTL. It uses cached framebuffers; the device uses an uncached
alias. Diagnostic scanout supplies contention rather than the real scheduler,
and each sample starts with cold CPU caches. Kernel ABI, synthesis and actual
device compiler/layout parity are not established by this matrix.

This work remains on local signed commits. No PR, remote push, main merge,
SD copy or device deployment was performed. Physical storage, live audio and
display scheduling, interrupts and a separate Pocket base/candidate/base
comparison remain necessary before claiming a device benefit.

## Execution decisions and costs

The coordinator's per-plan ledgers retain the full decision history. These
shared decisions explain departures from illustrative plan commands:

| Decision | Reason and practical cost |
| --- | --- |
| Parallel agents in separate worktrees | Explicit user request; required an extra integration review of shared Makefile/README edits. |
| Git worktree CLI fallback | Native tool could not resolve the non-Git workspace parent. Existing checkouts were preserved; isolation had to be checked manually. |
| Screen before the broad matrix | Limited disk and the performance gate; rejected or inconclusive early evidence remains recorded. |
| Reuse the labeled `a6ea5c0` baseline | Actual parent `f9d5442` changes documents only. Source/vendor equality was checked; neither snapshot nor source label was rewritten. |
| Document new fixture targets | Repository build guidance requires discoverable test commands; README additions are scoped to these targets. |
| Filter absolute fixture object paths by suffix | Illustrative relative `up/*.o` filters do not match this Makefile's actual paths; fresh fixture links verify the real implementation is included exactly once. |
| Preserve pending-room suppression and residual scans | Existing event guards suppress callbacks, while dispatch continues scanning. Changing that behavior would exceed this measurement task; fixtures preserve the observed contract. |
| One combined full matrix after isolated screens | Avoids duplicate correctness coverage. Outside the isolated dodge screens, timing cannot be assigned to an individual optimization. |
| Deterministic audio tests instead of desktop PCM equality | The save-state guide documents wall-clock-driven host audio. Queue/mixer restore semantics and fixed-audio replay are checked; audible device behavior remains unverified. |
| Keep private evidence and worktrees | No merge or deployment is requested. Retention consumes disk but preserves failed attempts, exact binaries and reproducibility anchors. |
