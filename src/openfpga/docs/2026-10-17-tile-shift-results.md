# Guarded native tile shifting — 2026-10-17

The measured candidate replaces only the Deltarune tile-shift script's VM
execution. It preserves existing layer lookup and every builtin call, rather
than adding another index. Retain the candidate: both modes preserve the sampled raw outputs and reduce
simulated time. Physical Pocket benefit remains unmeasured.

This continues the [sequential design](../../../docs/superpowers/specs/2026-10-17-sequential-optimization-design.md)
and the [previous layer measurements](2026-10-16-layer-lookup-vm-results.md).
Baseline production is `667feb80bb4f87ae4776efc2109d71deadcf7200` (the following
`48d28e8` changes documentation only). Candidate source is
`29ba0433b74768a644941baaf335eec1e52ef291`.

## Selection and behavior

Fresh spade frames 490–492 in both modes perform 1,200 lookups: 960 hint hits,
240 scans and 31,080 element comparisons. One interpreted tile-shift call
executes 11,210 VM operations and 1,225 direct builtin calls per frame. These
counts favor removing interpreter work without changing lookup ownership or
invalidation. Counter-on/off pixels and scene/event records agree in all six
sampled frames.

The native registration requires the existing script length 608 and hash
`7ba14ffb5cf975f3`. It accepts finite numeric arguments and a finite nonnegative
epsilon, resolves all nine required builtin functions before side effects, and
otherwise declines to the interpreter. It never declines after mutation begins.
The callable script has only local variables; its global initialization wrapper
remains interpreted. Existing array ownership, duplicate first-match lookup,
undefined depth handling, epsilon comparisons, and X-before-Y reads/adds/writes
remain. No persistent cache, ID state, SDK or snapshot format changes.

## Focused validation

The fixture executes supplied real game bytecode and real builtins; no private
bytecode is committed. It compares ordered builtin arguments/results, coordinates,
caller locals/arguments/stack/self/event state, return values and literal expected
coordinates. Wrong length/hash and missing builtins exercise interpreted fallback.
Cases cover empty/duplicate layers and elements, dead instances, null tiles,
integer/real inputs, extreme depths, custom epsilon and unsupported inputs.

The old source passes behavior characterization and fails eight intended work
bounds. The candidate passes 44 Deltarune checks under ASan/UBSan and one Undertale
configuration-exclusion check. Eligible calls enter the interpreter zero times;
fallback calls enter once. Alignment/function sanitizers are excluded and leak
detection disabled. A separate opt-in legacy unsigned-ID probe encounters
pre-existing float-to-int undefined behavior in the interpreter too; it is
excluded from the clean claim.

A new boundary case caught an initial discrepancy: layer ID 16777217 rounds to
16777216 in the builtin array and depth lookup returns undefined. The candidate
now preserves the interpreter's inequality to numeric depth, including builtin
call order. This regression passes. Independent task review reports no actionable
findings. The controller reran both final fixture binaries and the staged-SDK
snapshot suite successfully.

Actual gameplay native-on/off controls preserve all 12 candidate sampled frames'
pixels and scene/event records against the corresponding baseline. Native-on
removes the 11,210 tile VM operations while preserving all lookup counts. These
are work counts, not Pocket timing results.

The exact paired GCC13 device compiler accepts the source with original native
flags. Its stack-usage diagnostic records 224 bytes for `tileLayerShift`; this
is not a whole-program peak estimate. Full native linking is a final combined
verification gate.

## Evidence and limits

Private commands, logs, raw captures, counter controls and fixture binaries are
preserved in `src/openfpga/out/sequence-tile-20261017/` and
`.superpowers/sdd/2026-10-17-sequential-pocket-optimizations/`. The production
source SHA-256 is `8ce7668a9fbcf26699ed6aba46ace1e33904ca003521e3535f77dcbda5c71f71`.
Original assets and the paired SDK/runtime are unchanged. No physical Pocket
test, whole-game coverage or hardware speedup is claimed.

## Simulated replay screen

Fresh committed baseline/candidate exports pass repeated captures, positive RTL
replay and corrupted-state rejection in Accuracy mode. All 24 cross-repeat raw
RGB555/PCM comparisons are byte-identical, with 800 stereo pairs/frame. Spades
490–492 mean simulated time changes from 163.238437 to 135.109450 ms/frame
(-17.232%). This is a simulated CPU/SDRAM result, not physical Pocket FPS.

Configuration: GCC16.2 port build, alignment64, natives enabled, BRAM interpreter,
cached framebuffer, modeled clock-read cost1680, fixed audio and quiet traffic.
The paired native device build uses GCC13 instead; full source/config provenance
and exact cycles are in `tile-spades-accuracy-comparison.json`. Native execution
intentionally removes VM opcodes, so cross-revision VM trace identity is not an
acceptance requirement; ordered actual builtin/state fixtures remain required.

Speed mode independently passes the same repeated capture, positive replay,
corrupted-state and 24 raw-byte gates. Its mean changes from 82.233883 to
53.085020 simulated ms/frame (-35.446%). Across both modes, all four cases and
12 sampled frames validate, with 48 cross-repeat byte comparisons. These
figures are overlapping VM work removal, not additive to earlier layer savings.
The Deltarune-only native does not alter Undertale production code; its real
WAD16 fixture checks configuration exclusion. Real ordered builtin/state traces
and native-on/off gameplay controls provide the affected trace coverage. No
additional cross-revision opcode-identity claim is made.
