# Deltarune audio shadow implementation plan

> **For agentic workers:** Use the `executing-plans` skill to implement this plan task by task. Steps use checkboxes for tracking. Delegate only if the user authorizes it.

**Goal:** Remove per-sample software remainders from normal audio shadow maintenance without changing playback or restored queued audio.

**Architecture:** A private helper records the accepted stereo prefix into contiguous ring spans. The existing caller acceptance guard remains. Keep the 64-bit total, initial ring-index calculation and a rare scalar fallback for counter overflow; add no serialized state.

**Tech Stack:** C, existing software mixer, ASan/UBSan fixtures, RV32 GCC 16.2.0, QEMU and os25 Verilator replay.

**Spec:** [Shared design and verification protocol](../specs/2026-10-15-deltarune-optimization-design.md).

## Global constraints

All shared-design constraints apply. In particular preserve arbitrary positive
capacity, stereo bytes, the 64-bit accepted total, failed/partial-write handling,
snapshot format and post-restore append. Mixing/voice advancement still covers
the full requested chunk even when the backend accepts less.

### Task 1: Specify the ring contract and implement contiguous copies

**Files:** Modify `src/openfpga/platform/of_audio_system.c#mixAndWrite` and
`tests/of_audio_snapshot_test.c`. The existing test includes the real audio
implementation and can call its private helpers. Keep the scalar reference in
the test as an independent oracle.

**Interfaces:** Add private
`static void rememberAcceptedPcm(OfAudioSystem *ut, const int16_t *pcm, int pairs)`.
It consumes a valid positive capacity and an already-validated accepted count;
it updates only `snapshotPcm` and `snapshotWritten`. Keep the call under
`ut->snapshotPcm && accepted > 0 && accepted <= chunk`.

- [x] Add a scalar oracle and compare the entire ring and total after each append:

```c
static void referenceAppend(int16_t *ring, int capacity, uint64_t *written,
                            const int16_t *pcm, int pairs) {
    for (int i = 0; i < pairs; ++i) {
        size_t at = (size_t)((*written)++ % (uint64_t)capacity) * 2;
        ring[at] = pcm[i * 2]; ring[at + 1] = pcm[i * 2 + 1];
    }
}
```

  Cover capacities 1/3/7/16/131072, boundary/no-boundary crossings, multiple
  wraps, zero pairs, asymmetric nonzero L/R samples, totals above `UINT32_MAX`,
  and totals immediately before `UINT64_MAX`. Compare untouched ring cells too.
- [x] Extend the existing backend stub to support chunks up to 256 pairs and
  configurable returns: full, partial, zero, negative, and greater than chunk.
  Exercise `mixAndWrite`, asserting that only the accepted prefix changes the
  shadow/counter and no invalid return changes either. Keep existing restore
  assertions and add an append after `ofAudioApplySnapshot`.
- [x] Run the new helper tests before implementing it; expect the missing-helper
  compile failure. Existing behavioral cases should pass against the scalar
  path; after implementation, deliberately corrupt one copied channel in a
  temporary test build and require the oracle to reject it.
- [x] Implement the helper using this structure; the overflow path deliberately
  retains the old scalar semantics for non-power-of-two capacities:

```c
/* Preconditions: snapshotPcm exists, queueCapacity > 0, pairs >= 0. */
if (ut->snapshotWritten > UINT64_MAX - (uint64_t)pairs) {
    /* Use the original scalar append here, including uint64 wrap. */
    for (int i = 0; i < pairs; ++i) {
        size_t at = (size_t)(ut->snapshotWritten++ %
                            (uint64_t)ut->queueCapacity) * 2;
        ut->snapshotPcm[at] = pcm[i * 2];
        ut->snapshotPcm[at + 1] = pcm[i * 2 + 1];
    }
    return;
}
size_t capacity = (size_t)ut->queueCapacity;
size_t at = (size_t)(ut->snapshotWritten % (uint64_t)capacity);
while (pairs > 0) {
    size_t count = capacity - at;
    if (count > (size_t)pairs) count = (size_t)pairs;
    memcpy(ut->snapshotPcm + at * 2, pcm, count * 2 * sizeof(*pcm));
    ut->snapshotWritten += (uint64_t)count;
    pcm += count * 2;
    pairs -= (int)count;
    at = 0;
}
```

- [x] Run `SDK_ROOT=/Users/chanderson/Projects/undertale-pocket/openfpgaSDK make -C src/openfpga snapshot-tests`.
  Expect the new ring checks and existing queued-audio/decoder/restore checks to
  pass under ASan/UBSan. The SDK path is read-only.
- [x] Run `make -C src/openfpga GAME=deltarune lint` and the Undertale equivalent.
  Review the diff for unchanged mixer arithmetic, caller guards and wire format.
- [x] Warm signing with `gpg-warm-agent`; commit only the audio implementation
  and tests as `perf(audio): copy accepted PCM shadow in ring spans`.

### Task 2: Establish removed work and measure the complete-frame effect

**Files:** Harness private snapshots/cases; create
`src/openfpga/docs/2026-10-15-audio-shadow-results.md` when executing, using the
actual execution date if different. No harness pins or production SDK edits.

**Interfaces:** Inputs are the exact Task 1 parent/candidate commit IDs. Outputs
are validated case paths, raw-channel comparisons and a retain/reject decision.

- [x] Export the exact parent and candidate and run the shared protocol's first
  Deltarune dodge Accuracy/timing comparison with matching flags.
- [x] Disassemble `mixAndWrite` and `rememberAcceptedPcm` from the actual candidate
  ELF using `riscv64-elf-objdump -d`. Require no `__umoddi3` in the normal per-pair
  loop; it may remain once per accepted chunk and in the rare overflow fallback.
  An 800-pair frame split into 256/256/256/32 should need four normal index
  calculations instead of 800. Do not equate that count with a measured speedup.
- [x] If worthwhile, complete the shared two-game/mode/window output matrix,
  negative controls and scanout checks. Run normal and restored desktop audio
  comparisons as described in the repository's save-state verification guide.
- [x] Record elapsed cycles, outputs, memory change, compiler evidence and
  device limits in the dated result. Do not describe fixed 800-pair simulation
  as real slow-frame audio consumption; use a separately labeled realtime model
  only for additional timing sensitivity, never for cross-build PCM acceptance.
- [x] Commit the report as `docs: record audio shadow optimization evidence`.
  If software results justify a Pocket test, prepare a separately identifiable
  base/candidate pair; request no SD trip until all local checks are complete.

Execution note: the user authorized parallel implementation on 2026-10-15.
Each topic was implemented and independently reviewed in its own worktree.
The coordinator integrated the code as `2ee841c` and selected one shared
32-ELF baseline/combined replay matrix after separate audio and string dodge
screens. Timing outside those isolated screens describes the combined change.
All local implementation and measurement deliverables are complete. The shared
32-ELF matrix, eight contention replays and frozen-input verification pass.
Physical device validation remains separate and no device benefit is claimed.

Audio verification ruling: the repository save-state guide says whole-game
desktop BS_AUDIO_DUMP advances with wall time and cannot establish PCM identity
across uncapped frame runs. Use the deterministic decoder/queue/mixer snapshot
suite for ordinary and restored audio semantics (including restore-then-append),
and fixed-audio QEMU/RTL for cross-build raw PCM. No desktop whole-game PCM
identity or device listening result is claimed.

Final results: [combined outcome and verification limits](../../../src/openfpga/docs/2026-10-15-deltarune-optimization-results.md).
