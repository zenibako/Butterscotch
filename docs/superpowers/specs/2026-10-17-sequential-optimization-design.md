# Sequential Pocket optimization investigation

The user authorized all four remaining directions in sequence: precise collision
preparation, tile shifting, dialogue/string traversal, then loading hitches.
Start from merged `cd929134b36ad07eb6aaa778dc81f842ff510cc4`. Finish and review
each topic before starting the next; an evidence-backed rejection is a valid
result, and speculative work must not become a default optimization.

## Existing decisions

- [The existing optimization design](2026-10-15-deltarune-optimization-design.md)
  requires preserved Accuracy output, PCM, ownership, event order and snapshots,
  measured work removal, and separate simulated/device claims.
- [Next CPU opportunities](../../../src/openfpga/docs/2026-10-16-next-cpu-opportunities.md)
  measures repeated precise-scan setup and tile-script calls. Its layer hint and
  argument clearing recommendations are already implemented; do not repeat them.
- [Layer/VM results](../../../src/openfpga/docs/2026-10-16-layer-lookup-vm-results.md)
  record the retained eight-byte Runner hint and limited three-frame evidence.
- [Collision measurements](../../../src/openfpga/docs/2026-10-15-collision-work-results.md)
  reject target sorting/caching for the sampled workloads. A pair-local precise
  descriptor does not cache across callbacks or broaden that earlier scope.
- [String results](../../../src/openfpga/docs/2026-10-15-string-read-results.md)
  already remove read-only input copies. Remaining UTF-8 traversal needs counting.
- The repository performance skill records cold reads, prior failed pool/layout
  experiments, clock-read overhead and the need to read fewer bytes for loading.

## 1. Precise collision preparation

Confine new state to the existing synchronous precise pair scan in
`src/collision.h`. Prepare each side lazily, after the same control-flow gates as
today. Reuse constant scale validity, angle classification, rotation coefficients,
converted origins and mask metadata only where evaluation has been reached.
Preserve the existing per-point divisions and float expression order: reciprocal
multiplication or incremental coordinates are outside this change.

Keep `Collision_pointInInstance` behavior and standalone/shape callers unchanged.
Pair sampling retains compatibility mode, negative scales, truncation, full sprite
and cropped-mask bounds, A-before-B short circuit, and the first-hit early return.
No allocation, persistent descriptor, snapshot field, dependency, or SDK change.

An asset-free fixture compares pair results against a deliberately scalar oracle
using the unchanged point helper across scaled/rotated/cropped/frame-mask inputs.
Include zero/near-zero scales, tiny and negative angles, null sprites/masks,
fractional positions, signed scales, edge bounds and both compatibility modes.
A test-only observation hook, if necessary, counts preparation rather than wall
time; ordinary builds contain no counters or externally visible diagnostics.

## 2. Tile shifting

Count remaining searches and script/builtin calls on the retained predecessor.
Choose the smallest supported candidate: a full first-match ID index if remaining
searches dominate, or a hash/length-guarded native `gml_Script_tile_layer_shift`
if interpreter dispatch dominates. This script is not the legacy
`builtin_tile_layer_shift`. Preserve operation order, duplicates, missing IDs,
mutation/restore invalidation and script-visible variables. Avoid combining both
approaches before attribution. Retain a linear/interpreted fallback.

## 3. Dialogue and strings

Count actual codepoint/byte traversal in the existing field, dialogue and battle
routes before selecting a change. Prefer synchronous borrowed-input operations
and bounded scans without persistent state. Preserve malformed UTF-8 behavior,
codepoint indexing, result ownership, embedded NUL handling and conversions.
Do not create broad string caches without measured reuse and an ownership design.

## 4. Loading hitches

Measure retained startup/texture bytes and decode work separately. Screen fewer
bytes or fewer redundant reads before proposing a new packed format. Compression
must be lossless, bounded, and tested for corruption/truncation and exact decoded
bytes. Account for RV32 decoder cycles, allocations and the roughly 51 MB app heap;
host throughput alone cannot establish a Pocket win. Never rewrite the user's
original game assets. A proposed format requiring new asset packs stays opt-in
until compatibility and on-device cost are established.

## Verification and resource limits

- Preserve Accuracy pixels, same-mode Speed pixels, PCM/pairs, event order,
  ownership, normal saves, Memories, sleep and snapshot wire format/eligibility.
- Test shared code with Undertale/WAD16 and Deltarune/WAD17; use focused
  ASan/UBSan fixtures, recorded exclusions and relevant snapshot checks.
- Use fresh named baseline/candidate Git exports, explicit compiler/configuration,
  repeated captures, positive and corrupted-oracle RTL checks and independent
  cross-revision raw-channel comparisons. Do not treat desktop duration as FPS.
- Screen one relevant timing window first. Expand when the screen and semantic
  checks justify it. Keep same-source/layout sensitivity visible for small gains.
- Use the verified isolated app worktree and read-only staged SDK/runtime. Keep
  maximum concurrent compiler processes at two, and serialize topic execution.
- Check disk before new cases. Preserve current/old evidence, projects, SDKs,
  original assets and local changes. No Docker pruning, VM changes, deletion of
  prior experiments, SD writes, push, release or PR is authorized by this task.
- Commit each topic separately with conventional GPG-signed messages. Report
  retained/rejected candidates, exact validation and remaining Pocket checks.
