# Exact HUD button cache on current main

Ports only the bounded cache from local experiment `e7472ca2` onto merged main
`757a079a14694ad40433839094ba2b0d3b504dfb`, addressing
[Pocket issue #7](https://github.com/zenibako/butterscotch-pocket/issues/7).
The [optimization constraints](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md)
require unchanged pixels, PCM, ownership and Memory formats. Old-history source
was not merged, and its prior measurements were not reused as current results.

## Scope

Immutable source rectangles no larger than 32x32 retain exact hybrid-filter
intermediates for white, alpha-256 battle-button drawing. Clipped source bounds,
phase and odd edges are part of the key. Destination blending and current HUD
sharpening are unchanged. Other sampling, fonts, tint/opacity and mutable textures
keep their previous paths. Total requested storage, including entry metadata,
is bounded to 32 KiB across textures/renderers. Allocation failure or a full
budget falls back without evicting the source under construction.

Source destruction/eviction frees entries; copied surfaces cannot inherit them.
No snapshot wire data, eligibility, game identity, asset pack, SDK or runtime
changes. The sentry optimization and legacy Memory identity reader remain intact.

## Current game and lifecycle verification

- Fresh baseline and candidate desktop builds use the current game data and
  packs, explicit fingerprints, production natives, Memories and no focus build.
  A private fixed 1/30-second audio-update adapter is identical in both builds;
  production audio source is unchanged.
- Deltarune's room-52 encounter/observation route matches all 971 captured images
  per run in both modes (970 numbered game images plus the duplicate final
  capture), and all 6,233,600 PCM bytes / 1,558,400 stereo pairs per run. Speed
  also passes a separately instrumented repeat. The final scene contains
  "Rudinn is glimmering plainly."
- That diagnostic Speed run records 8,520 eligible requests, 8,506 hits and 14
  entries, with peak requested host storage 26,320 bytes. All seven freshly
  cropped 31x32 icon inputs directly match the retained independent raster
  oracle's bytes, despite the installed packs having been rebuilt.
- Undertale intro/field/Flowey routes match all 25 periodic game captures per
  mode and all 16,678,400 PCM bytes / 4,169,600 pairs per run. The final halt
  screenshot contains variable benchmark timing text, is preserved, and is
  excluded from game-pixel comparisons. This is regression coverage, not a
  demonstration of cache hits on Undertale's actual buttons.
- Both games pass ASan/UBSan battle contrast (1,368 cases), clipped sprites
  (499) and font sampling (166). Alignment/function checks and leak detection
  are excluded. Warm renderer-state restoration, exact cold rebuilding and
  real page eviction followed by synthetic source recreation are covered.
- The focused allocation/channel fixture passes normally, under `NDEBUG` and
  sanitizers. Reuse-disabled and doubled-budget controls fail as intended.
  Its initial runtime red uses a disclosed test-only scalar API adapter;
  baseline production has no derivative API. It is not a production pixel oracle.
- The full staged-SDK snapshot suite and lint pass. Unsupported 8/32-bit and
  dithered-16-bit cache getters decline without allocation. Existing 8-bit and
  dithered whole-drawing compile errors reproduce on both revisions; no unrelated
  renderer-format repair is included.
- Both production Pocket ELFs build with GCC 13.2.0, Memories enabled and focus
  disabled. Snapshot symbols, `executeLoop` at `0x4000`, and fsync syscall 82
  are verified. Existing runtime/bitstream/loader remain a matched set.

## Memory continuation and retained limitation

In both modes, Deltarune dialogue and Undertale field Memories captured by the
baseline load in a fresh candidate, match uninterrupted reference pixels, and
recapture/reload with the same output. The battle renderer fixture independently
verifies warm derivative reuse across renderer-state application and cold rebuild.

The room-52 battle Memory at frame 850 loads, but its frame-970 continuation loses
character sprites relative to uninterrupted play on **both baseline and candidate**,
in both modes. Their restored pictures match one another exactly, including a
candidate recapture/consecutive restore. This reproduces the older experimental
battle-restore limitation in a current route; it is not a passing battle Memory
test or a failure introduced by this cache. No physical Pocket restore or
power-button sleep result is claimed for this candidate.

## Current isolated RV32 screen

The actual current scalar/cached raster sources are rebuilt with pinned GCC
13.2.0 for os25 at 100 MHz, using the seven freshly verified icon sources and
15 placements. Fixed framebuffer/source/heap addresses, 64-byte function/loop
alignment and a private bounded allocator control the comparison.

| Direct 15-icon pass | Baseline | Cache | Reduction |
| --- | ---: | ---: | ---: |
| Warm | 9.24344 ms | 1.24475 ms | 86.53% |
| Derivative absent, including construction | 9.44623 ms | 4.36464 ms | 53.79% |

Both quiet repeats have identical cycle rows. All 153,600 framebuffer bytes match
an independent scalar oracle and each other. A corrupted expected hash is rejected;
the same-layout candidate with only its getter patched to return NULL preserves
pixels and returns warm cost to 9.24207 ms. Diagnostic scanout preserves raw output
and warm timing. Memory oracles pass. The seven cropped fixtures request 14,560
bytes including allocation alignment; production atlas lifetime/layout differs.

These are **simulated direct-raster measurements**, excluding scripts, wrappers,
frame reconstruction, real allocator behavior, OS/SD and presentation/audio
scheduling. The warm three-pass set reuses the framebuffer; absent derivatives
do not imply cold CPU caches. No whole-frame or physical Pocket FPS gain is
established.

Private sources, failed probes, commands, raw comparisons, native ELFs and
benchmark manifests remain ignored under `out/hud-cache-current-20261019/`.
No original assets, prior evidence, SDK, Docker images, VM settings or SD card
were modified. The independent review's initial restore-test oracle issue was
reproduced and corrected with independently calculated scalar expectations.
