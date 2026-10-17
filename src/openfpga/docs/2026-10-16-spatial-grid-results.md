# Spatial-grid synchronization — 2026-10-16

Retain guarded synchronization as a local candidate for a small measured spade
scene saving. The first production comparison is 0.960% faster there and flat
in dodge; a same-ELF control attributes 0.634% to avoiding the full scan in its
diagnostic build. This is a bounded simulated result, not a Pocket speedup or
a whole-game performance claim. Defer `instance_exists` changes.

This follow-up to the [collision dispatch measurements](2026-10-15-collision-work-results.md)
targets grid maintenance in the recreated spade chase and benchmark dodge window.
The earlier [optimization design](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md)
requires measured work and preserved behavior before retaining a candidate.

Baseline: merged main `c87feec80ddbe48b1905ab111c8bac47f0b79da8`.
Measured candidate: `b6a12f630558843ce39a20e5096d8723b5a9e38e`.
Final equivalent source: `c5db3bb1d3bcfdc755f44310988bcecf59a37be7`.
The candidate revisions are local. Physical Pocket behavior and performance
remain unmeasured for this change.

## Counting screen

A private WAD17 desktop adapter counted actual removal loops and
`instance_exists` object-bucket copies, without per-operation clocks. Accuracy
and Speed have identical counts in the selected frames and complete runs.

| Completed frame | Removal calls | Tracked member probes | Full-scan cells | Full-scan member probes | Additional matches |
| --- | ---: | ---: | ---: | ---: | ---: |
| Spades 490 | 14 | 55 | 4,466 | 1,797 | 0 |
| Spades 491 | 14 | 53 | 4,466 | 1,804 | 0 |
| Spades 492 | 15 | 49 | 4,785 | 1,901 | 0 |
| Dodge 3323 | 4 | 24 | 1,408 | 1,672 | 0 |
| Dodge 3324 | 5 | 38 | 1,760 | 2,096 | 0 |
| Dodge 3325 | 6 | 24 | 2,112 | 2,511 | 0 |

All selected spade removals are ordinary synchronization. Dodge 3324 includes
one destruction removal (352 cells, 419 full-scan member probes); the remaining
selected removals are synchronization. Over the full benchmark process through
frame 3326, ordinary synchronization makes 1,787 removal calls and visits
518,664 full-scan cells. The recreated spade process through frame 493 makes
4,093 ordinary synchronization calls and visits 1,305,667 full-scan cells.
These are code-work counts, not CPU cycles or measured bus traffic.

No additional references were found by the full scan after tracked removal in
either measured process. This does not prove that stale-pointer recovery is
unnecessary: upstream #471 added that recovery for lifecycle failures outside
these routes. The candidate retains it.

Defer the `instance_exists` copy optimization. Spades copies only 2/2/3 pointers
in the selected frames; dodge copies 4/6/5. The entire benchmark route copies
2,797 entries and the recreated spade process copies 966. Each nonempty object
query finds an active member on its first probe. This screen does not establish
that copying is material in other workloads.

The baseline counting binary uses `-O1 -g0`, seed 7, uncapped/no-flip dummy SDL,
and a runtime counter toggle in the same binary. All eight runs complete; all
24 selected counter-on/off frame pairs have byte-identical RGB555 and matching
scene counts and event-count/FNV64 digests. Digests cover event entries and
collision callbacks, rather than complete opcode traces. This controls the
counter toggle within the adapter; it does not compare an entirely uninstrumented
binary. Wall-clock-driven desktop PCM is excluded.

The ordinary route samples field 2800–2802, attack meters 3120–3122 and dodge
3323–3325. Spades is a controlled recreation, not supplied player progress:
room 43 at input frame 30, globals on frame 29
`darkzone=1,char[1]=0,char[2]=0,plot=60,interact=0`, and two-frame Z presses
40,55,...,475. Frames 490–492 contain nine active spades, 9/8/8 visible spades
and zero battle controllers.

## Initial simulated screen

Both windows use Accuracy timing builds, GCC 16.2.0, port flags with 64-byte
function/loop alignment, native replacements enabled, BRAM interpreter, cached
framebuffer, modeled 1,680-cycle clock reads, fixed 800-pair audio and quiet
traffic. Times are **simulated at 100 MHz**, not physical Pocket measurements.
Timing ELFs omit VM hooks and in-loop PCM hashing; the exact flags remain in
case metadata.

| Frame | Baseline cycles | Candidate cycles |
| --- | ---: | ---: |
| Dodge 3323 | 10,344,914 | 10,353,635 |
| Dodge 3324 | 11,803,751 | 11,775,957 |
| Dodge 3325 | 10,184,140 | 10,222,593 |
| Spades 490 | 17,687,373 | 17,511,122 |
| Spades 491 | 17,620,825 | 17,444,403 |
| Spades 492 | 17,778,295 | 17,621,420 |

Dodge mean: 107.776017 to 107.840617 simulated ms/frame, **0.060% slower**.
Spades mean: 176.954977 to 175.256483 simulated ms/frame, **0.960% faster**.
The dodge result establishes no gain. The small spade difference needs further
attribution because prior same-source experiments showed code-layout effects.

All four successful ELFs pass repeated QEMU capture, same-ELF positive RTL
state/pixel/memory oracles and deliberately wrong-state negative controls.
Independent direct byte comparisons match RGB555 and PCM across both revisions
for all six selected frames; pair arrays are `[800,800,800]` per window.
Timing RTL excludes its in-loop PCM hash field; QEMU PCM bytes and RTL pair
counts remain checked. Pointer-bearing state is only compared within an ELF.
No trace, Speed, diagnostic scanout or full shared-game matrix is claimed by
this initial screen.

The original spade build attempts fail in an older scene adapter that refers to
the removed `SWRenderer.contrastEnabled` field. Both failures remain preserved.
A separate private workflow patches only that adapter: retain room, active and
visible spades, controller, mode and profiling assertions; remove the deleted
feature check and print `contrast_removed=1`. No gameplay or output/state oracle
changes. Both successful spade cases use this same recorded adapter and new
case identities. Dodge uses the original adapter, within-window comparisons
have identical configurations.

## Same-ELF attribution control

A private diagnostic build of the candidate adds a volatile runtime flag at
`removeTrackedForSync` entry. The flag either permits the existing proof or
returns false immediately, forcing the unchanged public full scan. Both settings
retain candidate metadata, insertion logic, layout and the same executable.
This isolates the scan mechanism within that build; it does not replace the
production baseline/candidate comparison or measure the net cost of all metadata.

The private host adapter reads exactly one ASCII `0`/`1` byte using raw Linux
syscalls before game initialization, rejects missing/invalid/extra input, and
logs its selection. Capture records the file hash and verifies the actual flag
in each checkpoint using its ELF symbol address. RTL restores that checkpoint.
Only the explicit runtime-control configuration field differs between cases;
the off case reuses the on case's exact compiled ELF, with original compiler
argv and permitted configuration delta recorded. The control remains private;
no production toggle or diagnostic global is committed.

The control passes both repeated QEMU captures, both positive RTL replays and
both negative controls. Complete QEMU oracle rows, including pointer-bearing
game-state hashes, are equal between settings. Independent raw RGB555/PCM and
pair arrays match across settings and both repeats.

| Spades frame | Full-scan control cycles | Trusted control cycles |
| --- | ---: | ---: |
| 490 | 17,490,513 | 17,374,241 |
| 491 | 17,417,566 | 17,302,679 |
| 492 | 17,585,501 | 17,483,690 |

Mean: **174.978600 to 173.868700 simulated ms/frame**, saving **1.109900 ms
(0.634306%)** in this diagnostic ELF. This establishes a small scan-path saving
in the sampled spade scene. Its size differs from the cross-ELF result, so the
latter's entire 0.960% difference must not be attributed to grid work. Absolute
times from this instrumented ELF should not be compared to production ELFs.

## Focused shared-game follow-up

Six additional unpatched production ELFs cover Undertale Flowey Accuracy trace,
Deltarune spades Accuracy trace and spades Speed timing, each at both revisions.
All six pass repeated QEMU capture, positive RTL replay and negative controls.
Independent raw RGB555/PCM bytes match across revisions and repeats; pair arrays
are `[800,800,800]`. Trace comparisons also match all VM operation/event/digest
fields exactly. Each revision's spades Accuracy trace and timing raw channels
agree.

| Window / mode / kind | Baseline mean simulated ms | Candidate mean simulated ms |
| --- | ---: | ---: |
| flowey / accuracy / trace | 25.211873 | 24.686253 |
| spades / accuracy / trace | 201.732640 | 196.707637 |
| spades / speed / timing | 94.117267 | 93.270353 |

Speed spades saves **0.846913 simulated ms/frame (0.899849%)** in the production
timing comparison. Trace times retain VM instrumentation and are recorded for
reproduction; they are not used as default-build speedup estimates. Flowey
VM operations are 1,181/1,183/1,198 with 17 events per frame. Spades operations
are 14,443/14,455/14,526 with 32/32/34 events; both event-trace digests match
across revisions.

The final validating report contains ten production cases, all positive/negative
controls passing. The separate same-ELF control adds two diagnostic cases using
one ELF. This remains a focused screen, not the full Accuracy/Speed,
trace/timing matrix for both games. Field/attack-window captures, diagnostic
scanout, production peak memory and physical Pocket testing remain unmeasured
for this change. No deployment or push is part of this result.

## Guarded synchronization

Only ordinary synchronization may remove from recorded cells without scanning
the whole grid. It requires a trusted membership record, matching nonzero grid
generation and recorded coordinate count, valid coordinates, and exactly one
matching reference in every tracked cell. Any detectable mismatch invalidates
trust and runs the original full recovery scan, including after a partial
tracked removal. The synchronization then clears tracking before reinsertion.

Insertion is the sole production writer of grid membership and appends its
matching coordinate in the same loop. A successful insertion establishes trust.
Cache/lifecycle clears, room persistence, destruction and snapshot restoration
invalidate it. This is an inductive paired-writer invariant: arbitrary untracked
insertion with otherwise correct tracking cannot be detected by bounded removal.
Public removal remains unconditional and recovers that case. It also retains
stable neighbor ordering and the legacy no-match cache/dirty behavior.

Two 32-bit generation words avoid grid-address reuse and support legacy
compilers. Generation exhaustion permanently disables trust for subsequent grids.
The cost is 16 bytes per instance, eight bytes per grid and eight global bytes;
peak live memory has not been measured. The four instance words sit in the
existing snapshot-omitted cache gap. Actual-header host/RV32 float/double layout
probes preserve all three serialized scalar lengths, `InstancePlain` size and
alignment. No snapshot format/version change is needed.

## Focused validation

- Baseline real-grid fixture: nine behavior checks pass; the bounded-work check
  fails as expected with 4,231 array reads for a one-cell move in a 65×65 grid.
- Candidate ASan/UBSan WAD16 and WAD17: 14 behavior checks, zero bounded-work
  failures, eight array reads for that move. Coverage includes lost/partial,
  invalid/repeated coordinates, duplicate/missing entries, grid replacement,
  inactivity/reactivation, empty membership, generation exhaustion, public
  recovery and stable neighbor order through public and trusted paths.
- Both games' builtin compatibility and collision ordering/mutation/nested-arena
  fixtures pass under the same sanitizers. Alignment/function sanitizer checks
  retain the existing fixture exclusions; leak detection is disabled.
- Full snapshot suite, both port lint configurations and Release desktop
  CMake `BACKEND=noop`, audio/physics disabled, software renderer enabled,
  `WERROR=ON` build pass.
- Changed core `instance.c`, `runner.c`, `spatial_grid.c` pass GNU C++98 syntax
  checks with CI's `-Wall -Wvla -Werror` flags. The Pocket-only snapshot source
  is excluded from desktop CMake/C++ CI and retains its pre-existing C++
  goto/initialization incompatibility; this was confirmed on the baseline.
- A fresh independent reviewer found no blocking correctness issues. Both
  nonblocking suggestions were addressed: qualify the proof's recovery scope
  and test stable neighbor order through trusted synchronization.

An extra RV32 `-Werror=sign-compare` check exposed a new signed/unsigned
length comparison. The final source explicitly compares both values as
`size_t`. This preserves equality semantics on supported 32/64-bit targets.
A fresh follow-up review confirms that interpretation. Under the exact measured
RV32 flags, the before/after debug-stripped grid objects are byte-identical;
actual desktop CMake flags also produce byte-identical objects. All other
production files and the instance layout remain unchanged. Replays retain their
original measured revision label rather than being relabeled as the final fix.
The strict RV32 warning check, GNU C++98 grid check, both games' three sanitizer
fixtures and incremental WERROR build pass after the fix.

## Private evidence and reproduction

Let `PORT` be this checkout and `EVIDENCE=$PORT/.superpowers/spatial-query-20261016`.
The private adapter is preserved as `instrumentation.patch`, `sq-private.h`,
`instrument.py`, `build-command.json`, `run-probe.py`, `counts.json` and per-run
commands/results. `restoration.json` confirms all temporary sources matched the
baseline before implementation. The counting binary SHA-256 is
`aed53feb5754464d163d01d7bdcc8fc838861206c5bb66b177d514a8375d5ccc`.

`fixture-red.log`, `check-dr-stable-run.log`, `check-ut-stable-run.log`, the
build/run logs, `check-summary.json`, `ABI-r2/layout.json`, `ABI-after/layout.json`
and `abi-comparison.json` retain focused verification. Rebuild each game using
fresh game-specific object directories:

```sh
make -C src/openfpga -j2 GAME=deltarune WADS=17 \
  PC_BIN=out/grid-dr PC_OBJ_DIR=out/grid-dr-obj \
  PC_EXTRA_CFLAGS='-g -O1 -fsanitize=address,undefined -fno-sanitize=alignment,function' \
  spatial-grid-fixture builtin-compat-fixture collision-work-fixture
```

Run the three `src/openfpga/out/grid-dr_*_fixture` executables with the host SDL
library path and `ASAN_OPTIONS=detect_leaks=0`. For Undertale use `GAME=undertale
WADS=16`, `grid-ut` and a separate `grid-ut-obj`. Private assets, captures and
harness inputs are intentionally excluded from version control.

The initial harness evidence is under `$EVIDENCE/harness-initial/`:
`initial-summary.json`, `validated-results.json`, per-window comparison files,
`compare-initial.py`, command logs, the scene compatibility patch and workflow
manifest. The collector SHA-256 is
`64e1a5ff39e97b0ce437b38be7ea99210919500c39ad8bea79ff2a39b99c8558`.
Let `HARNESS_ROOT` denote the verified private harness checkout; these exact
successful case paths are relative to it:

```text
experiments/cases/spatial-grid-base-c87-20261016-r1-deltarune-dodge-accuracy-timing-c143903ad5889e26
experiments/cases/spatial-grid-candidate-b6a12f6-20261016-r1-deltarune-dodge-accuracy-timing-6510a3057a152038
experiments/workflows/spatial-grid-compat-20261016-r1/experiments/cases/spatial-grid-base-c87-20261016-r1-deltarune-spades-accuracy-timing-a679fead4b7666b8
experiments/workflows/spatial-grid-compat-20261016-r1/experiments/cases/spatial-grid-candidate-b6a12f6-20261016-r1-deltarune-spades-accuracy-timing-d9de9659c0acdff3
```

| Window / revision | ELF SHA-256 |
| --- | --- |
| dodge / baseline | `79fbb7895db4a8524dd7ef7a1fd1f1e35e6762f08b69e0ba4ac7d46cf0e0db49` |
| dodge / candidate | `83e14e1e37fce649e453d3ad19e98ae3478f09e14113d15bc1918106acdd50a1` |
| spades / baseline | `9eea41b08bf4ab44dec4043709da51bd5a87385d67c2a13113c29c7187163722` |
| spades / candidate | `244baf1575325d909758659ce055095a308f2d4acc41810f96331e438dcf3c50` |

Runtime-control evidence is under `$EVIDENCE/runtime-control/`: `source.patch`,
`adapter.patch`, `prepare-control.py`, `run-control.py`, `workflow.json`,
`reuse-provenance.json`, `summary.json`, `validated-results.json` and command
logs. The control ELF SHA-256 is
`10f0cc119238ad0b0df78877709c3d8b4d40e80b905067955bb86765c6a5c989`;
collector SHA-256 is
`be07115d702fa8860e972d86f59cfdeaf8c2ed71168a864fa39a674df5064a77`.
Exact control case paths relative to `HARNESS_ROOT`:

```text
experiments/workflows/spatial-grid-runtime-20261016-r1/experiments/cases/spatial-grid-runtime-b6-20261016-r1-deltarune-spades-accuracy-timing-6fdf61a275c71ddf
experiments/workflows/spatial-grid-runtime-20261016-r1/experiments/cases/spatial-grid-runtime-b6-20261016-r1-deltarune-spades-accuracy-timing-5fe87c3ab1a37d4d
```

Portability-fix evidence is in `$EVIDENCE/portable-count/`, including exact
compiler commands, original/final source, object files, before/after warnings,
final sanitizer runs and `result.json`. The identical stripped RV32 grid object
SHA-256 is `1c7fd3edf9610aac2c7171dbaae8657182ce9fe46978bb40d059f2e644b8e0ac`;
the identical host object SHA-256 is
`a6a44ce3ea6a97e27b3956667e0ab4a98a0e04a643ed2fd5e784be11ec19201b`.

Focused evidence is under `$EVIDENCE/focused/`: `run-focused.py`, `summary.json`,
`validated-results.json`, channel/trace comparisons, disk checks and command
logs. Collector SHA-256 is
`3c180c29aac397d117a6185f39350455377e14400db6033b5f3e92783c7aec73`.
The validating ten-production-case report SHA-256 is
`974b0b755f961ce3abe67faff351055abf5cb8d336979c3402e18b28cdcdfb4f`.
Additional exact case paths relative to `HARNESS_ROOT`:

```text
experiments/cases/spatial-grid-candidate-b6a12f6-20261016-r1-undertale-flowey-accuracy-trace-bca8a0ac6f2cdf97
experiments/cases/spatial-grid-base-c87-20261016-r1-undertale-flowey-accuracy-trace-86474a5a60f1c552
experiments/workflows/spatial-grid-compat-20261016-r1/experiments/cases/spatial-grid-candidate-b6a12f6-20261016-r1-deltarune-spades-accuracy-trace-72a075d5c3caaf45
experiments/workflows/spatial-grid-compat-20261016-r1/experiments/cases/spatial-grid-base-c87-20261016-r1-deltarune-spades-accuracy-trace-031d70d87177879b
experiments/workflows/spatial-grid-compat-20261016-r1/experiments/cases/spatial-grid-candidate-b6a12f6-20261016-r1-deltarune-spades-speed-timing-81164b9fc42014e3
experiments/workflows/spatial-grid-compat-20261016-r1/experiments/cases/spatial-grid-base-c87-20261016-r1-deltarune-spades-speed-timing-52b2f3e2d9fdabb2
```

| Additional case / revision | ELF SHA-256 |
| --- | --- |
| flowey / accuracy / trace / candidate | `be5ad8a8c51d71290b3d0eb00fc3b9b2dba5549d96595a59f120db3a5fbdc717` |
| flowey / accuracy / trace / baseline | `f90ef6cf7c8d105f638106321d980c3b54fd6ac10774704c292cf21e0ba9b858` |
| spades / accuracy / trace / candidate | `86c9484f9efadbc8372c99e8d9ec12174ab21e1bc586917dc223e3c72d6c2bcd` |
| spades / accuracy / trace / baseline | `dab6a8450aa4e6b930420b684214cfa3b48745eafc78b288c0db5aea4f7c5c31` |
| spades / speed / timing / candidate | `0d61bb29f4bdc6f92572a9910477c14c7efd24baf013634d066690ab83fad629` |
| spades / speed / timing / baseline | `511cc077b866306cc7d4c6414d02280cd96262e520d79b2d36fc69ef9ee2404e` |
