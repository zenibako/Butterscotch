# Precise collision preparation — 2026-10-17

Retain the local preparation change on the strength of verified work removal,
focused semantics and bounded replay coverage. No physical Pocket speedup is
established; small simulated timing differences remain inconclusive for hardware.

The [measured opportunity](2026-10-16-next-cpu-opportunities.md#3-repeated-setup-in-precise-pair-scans)
identified repeated transform and mask setup during precise pair scans. This
implements the first topic of the [sequential design](../../../docs/superpowers/specs/2026-10-17-sequential-optimization-design.md).

Baseline source is merged main `cd929134b36ad07eb6aaa778dc81f842ff510cc4`.
Candidate source is `667feb80bb4f87ae4776efc2109d71deadcf7200`.

## Change and preserved behavior

The precise pair loop prepares each sprite side on first use, keeping scale
validity, angle classification, rotation coefficients and converted origins in
stack-local state. Mask bounds are prepared only after full-sprite acceptance;
mask selection and stride are prepared only after crop acceptance. No state
crosses a scan, callback, mutation or snapshot boundary.

The standalone point helper is unchanged. Per-pixel subtraction, rotation
expressions, division, float-to-int truncation, compatibility sampling,
A-before-B short circuit and first-hit return remain. The existing nonprecise
and SAT paths precede the new descriptors and are unchanged. There is no
allocation, persistent cache, counter or ordinary-build diagnostic hook.

## Focused verification

Both WAD16 and WAD17 pass 5,781 behavior checks under ASan/UBSan, with alignment
and function sanitizers excluded and Apple leak detection disabled. Tests include
literal hit/miss expectations and a scalar differential oracle, signed/near-zero
scales, tiny/negative/rotated angles, fractional positions, origins, mask frames,
negative crop offsets, 9-bit row stride, degenerate and missing-mask cases,
lazy invalid storage, early hits and both sampling modes.

The same final fixture compiled against an exported baseline header passes its
behavior-only checks and fails five setup bounds. The candidate passes all
setup bounds. These count real calls through fixture-only function/math
instrumentation, rather than elapsed time or source text.

| Fixture scan | Baseline mask classifications / abs / cos | Candidate |
| --- | --- | --- |
| Full 16-pixel miss | 34 / 96 / 0 | 2 / 6 / 0 |
| First-point hit | 4 / 6 / 0 | 2 / 6 / 0 |
| Rotated miss | 29 / 90 / 30 | 2 / 6 / 2 |
| A misses, B stays cold | 16 / 48 / 16 | 2 / 3 / 1 |
| Degenerate A | 2 / 16 / 0 | 2 / 1 / 0 |
| AABB rejection | 0 / 0 / 0 | 0 / 0 / 0 |

GNU99 double-real WAD16 passes the same fixture. Clang GNU++98 accepts the
production header. Both games' existing collision dispatcher fixtures pass
under ASan/UBSan, including ordering, mutation visibility and nested arenas.
The staged-SDK snapshot suite passes codecs, progress, audio, completion,
restore and packaging checks. Independent task review finds no actionable
production or fixture defects.

## Initial simulated screen

Both source revisions pass repeated QEMU capture, same-ELF positive RTL replay,
and rejected corrupted-state controls. Each mode independently passes all 24
cross-repeat direct RGB555/PCM byte comparisons and keeps 800 pairs/frame.

| Dodge window / mode | Baseline simulated ms/frame | Candidate simulated ms/frame | Change |
| --- | ---: | ---: | ---: |
| Accuracy | 106.268583 | 104.624450 | -1.547% |
| Speed | 71.157340 | 68.922803 | -3.140% |

These are means of frames 3323–3325. The small cross-ELF timing differences are
subject to code-layout sensitivity and do not establish Pocket benefit. The
fixture independently establishes removed work; its instrumentation times are
not used as gameplay measurements.

Both affected translation units (`runner.c` and `vm_builtins.c`) also compile
with the exact staged native GCC13 container and original device-build flags.
The extra `-fstack-usage` diagnostic records a 288-byte pair function and
64-byte prepared-point helper in each inspected emitted copy. Corresponding
baseline prologues are 240 and 48 bytes. The inspected call path therefore
adds approximately 64 bytes of stack; this is not a whole-program peak estimate.
The emitted helper retains two per-point float divisions and guards frame
selection/remainder/stride behind its readiness flag. Full native linking is
part of the final combined verification.

## Shared-game follow-up

All eight selected cases pass repeated captures, positive replay and corrupted
state rejection: both revisions for dodge Accuracy timing, dodge Speed timing,
dodge Accuracy trace and Undertale Flowey Accuracy trace. The 24 sampled frames
pass all 96 cross-repeat raw pixel/PCM byte comparisons, with 800 pairs/frame.
Both traced pairs also preserve VM operation/event counts and trace digests.
Flowey trace means are 24.710267 and 24.637723 simulated ms; instrumented trace
times are not used as ordinary-build performance results.

This is a focused shared-game matrix, not whole-game coverage. It adds no
scanout test and no physical Pocket run. The final evidence index is
`collision-case-index.json`, with maintained aggregate validation in
`collision-validated-results.json`, in the private evidence directory below.

## Replay provenance and limits

Private evidence is retained under
`.superpowers/sdd/2026-10-17-sequential-pocket-optimizations/`; focused fixture
logs and binaries are under `src/openfpga/out/sequence-collision-20261017/`.
The existing isolated os25 harness workflow uses fresh source snapshots for
both exact commits. Exported collision headers are byte-checked against Git;
all 1,631 original frozen-input hashes pass. No private assets are published.

The screen uses GCC 16.2, port flags, 64-byte alignment, native replacements
enabled, BRAM interpreter, cached framebuffer, modeled 1,680-cycle clock reads,
fixed 800 stereo pairs/frame and quiet traffic. These are simulated workloads;
the delivered native build uses its separately paired GCC13/SDK toolchain.
Neither model establishes physical SD, interrupts, display/audio scheduling,
complete-game behavior or Pocket FPS. Existing SDK/runtime and game assets
remain unchanged. The candidate adds bounded transient stack use; full native
linking and final combined checks are performed after the remaining topics.
