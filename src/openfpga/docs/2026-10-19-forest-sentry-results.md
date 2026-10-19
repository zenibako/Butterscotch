# Forest sentry fill optimization

Baseline: `b20ddaee`, the verified upstream sync. Upstream `origin/main`
(`54e10ec4`) was already included; fetching found no new commits to rebase.
This follows the output-preservation requirements in the
[optimization design](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md).

## Reproduction and work identified

Chapter 1 room 76 (`room_forest_area2`) contains four `obj_darkponman_ow`
statues. Each controls eight `obj_overworldbulletparent` diamond bullets
forming a rotating line. Room 89 (`room_forest_area5`) has one statue and
eleven bullets. These are overworld attacks, distinct from battle lasers.

With seed 7, set globals at input frame 29:
`darkzone=1,char[1]=0,char[2]=0,plot=100,interact=0`; jump at 30 to room 76,
then hold Right for 100 frames starting at 40. Frame 100 contains four
active statues and 32 visible bullets. Room 89 uses the same setup with
Right held for 160 frames; inspect frame 190.

The stable room-76 profile executes about 6,347 VM instructions/frame:
4,297 in the four statue Step events, with 128 `sin` calls. The player uses
rectangle/line collision queries. Statue Draw also darkens the whole screen
with a constant black translucent rectangle; at stable alpha the tile-alpha
update loop is skipped. Host clock readings are too small for a reliable
whole-scene time attribution. These counts identify remaining script work,
not measured Pocket costs.

## Change

The normal RGB555 constant partial-opacity fill now processes two pixels per
aligned word. Each channel uses two independent 16-bit multiplication lanes;
their products plus source contributions cannot exceed `31 * 256`, so the
lanes cannot carry into each other. The existing per-channel rounding and
opacity bit are retained. Repeated pairs reuse their result; unchanged pairs
avoid stores. Scalar ends handle unaligned starts and odd lengths.

Other pixel formats, dithering, blend modes and gradients retain their paths.
There is no new allocation, persistent cache or snapshot field. Both games
use this shared renderer path.

## Validation

- The scalar baseline and candidate match the general `alphaBlend` reference.
  The final fixture passes **120,879,840 pixel/guard comparisons** under
  ASan/UBSan and in the desktop port build. Coverage includes all 65,536
  destination values at each partial alpha for seven source colors, opacity
  edges, clipping, odd/even pitches, later rows, uniform and repeated pairs.
  Deliberately corrupted pixels are rejected.
- Eight sentry captures match pixels and semantic game-state dumps: room 76
  at frames 44, 100 and 140, and room 89 at 190, each in Accuracy and Speed.
  Only timestamps and opaque array addresses are canonicalized; address
  alias relationships remain checked. Active captures assert 32/11 bullets.
- Seven existing Undertale/Deltarune title, menu, field and battle captures
  remain pixel-identical, including battle Speed and Accuracy.
- A baseline Memory captured at frame 100 restores in a fresh candidate
  process at 40 and matches uninterrupted frame 190, in both modes. This
  route uses volatile `--bench-sleep` storage, then sets globals at 34 and
  jumps to room 76 at 35, after the sleep fixture's initial yard jump.
  The reference asserts room 76 and 32 bullets. Production policy bypass is
  unset. An earlier wrong-scene attempt and a desktop build lacking the
  explicit game-data fingerprint were discarded.
- Both native ELFs build with the pinned GCC 13.2.0 image, Memories enabled,
  focus diagnostics disabled, `executeLoop` at `0x4000`, and the existing
  `fsync` syscall path. Assembly confirms aligned `lw`/conditional `sw` per
  pair and three channel multiplies for a changed pair.
- Independent review found a missing old-compiler guard for the alignment
  hint; feature/version detection now preserves that compatibility.

The isolated desktop fill benchmark uses 320x240 buffers, black alpha 133,
three pixel patterns, 100 batches of 32 buffers, and three alternating-order
baseline/candidate repetitions. Setup copies are outside timing. Median
times are 0.058 to 0.046 ms for long color runs and equal adjacent pixels,
and 0.058 to 0.041 ms for varying pixels: **21–29% less host kernel time**.
Checksums agree. This is not a room FPS or Pocket speedup measurement.

Private reproduction scripts, captures, logs, assembly, native ELF hashes
and benchmark results remain under `out/forest-lasers-20261019/`. Final
scene evidence uses `candidate-final-*`, shared captures use
`candidate-final-shared-*`, and Memory evidence uses `restore-final/`.
`verify.py` checks those exact results. Game assets and progress are unchanged.

The stock maintained os25 harness still references pre-rename `ut_*` sources
and lacks this scene; its compiler/QEMU image is not installed on this host.
No RTL timing, deterministic full-game PCM comparison or physical Pocket
timing/sleep result is claimed. The next separate candidate is the statue
Step loop; target timing should determine its priority relative to fills.
