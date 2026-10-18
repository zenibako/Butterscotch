# Battle writer and Card Castle fixes — 2026-10-17

Two exact-output optimizations follow the [slowdown investigation](2026-10-17-observation-cardcastle-investigation.md).
The changes are signed commits `aa58882` (writer) and `f3a53f4` (tile composition).
Both games are built and packaged as a Pocket test candidate; this exact package
has not been tested on hardware. Battle work remains above the nominal 30 FPS
budget in the measured windows.

## Changes retained

The Deltarune writer has a native stand-in for its already-formatted, plain white
English battle style: font 5, special 1, no shake, no halt. The event and four
original input callees must match their bytecode fingerprints. Strings are
bounded printable ASCII, with ordinary line breaks and indentation supported;
commands, other fonts/languages/styles and unsupported state fall back before
side effects. The original input scripts and rendering builtins still run.
Draw order, shadows, per-character RNG advancement (including `random(0)` and
trailing empty characters), writer variables, alarms, and the skip path's
array-owner token are preserved. Formatting, reveal alarms and special effects
remain in the interpreter.

Cached tile composition now groups adjacent blocks of the same coverage into
horizontal spans when drawing one picture or a flattened set of layers. Solid
spans use one copy per row, instead of many small copies. Transparent pixels,
clipping, held clears, grids and the original multi-picture route retain their
semantics. This shared renderer change applies to both games and both modes.
The earlier readable party-name HUD remains present.

## Simulated target comparison

Each row is the mean of three consecutive Speed-mode frames. Baseline is
`fd5e895` (same program source as delivered `0b07bc4`); writer windows use
`aa58882`, and exterior windows use `f3a53f4`.

| Window | Frames | Baseline ms | Candidate ms | Work reduction |
| --- | --- | ---: | ---: | ---: |
| Encounter narration revealing | 437–439 | 72.5424 | 63.7985 | 12.05% |
| Same narration completed | 480–482 | 76.2562 | 65.2097 | 14.49% |
| Card Castle walking | 100–102 | 30.3153 | 28.1917 | 7.01% |
| Card Castle standing | 200–202 | 25.3995 | 24.3037 | 4.31% |

The timed narration is "A necklace of Rudinns blocks your path." Later "Rudinn
is glimmering plainly" is separately reproduced and checked on desktop, not a
separate target timing window. Exterior captures use the confirmed room 104,
`room_forest_castlefront`, with controlled post-intro `plot=165`. Walking and
standing windows have different camera positions; their difference does not
isolate movement cost.

The os25 replay uses GCC 16.2, native scripts, alignment, BRAM interpreter,
cached framebuffer, modeled clock-read cost 1680 and fixed 800 stereo sample
pairs per frame. These are simulated CPU/memory measurements, not physical
Pocket FPS predictions. OS/SD latency, interrupts and live audio/display
scheduling are not reproduced. The delivered ELFs use the pinned GCC 13.2 SDK,
so timing is not a measurement of the shipping compiler's exact binary.

## Verification

- Eight validated cases, 24 sampled frames: repeated QEMU checkpoint/output
  agreement, same-ELF RTL replay and corrupted-state rejection all pass.
- Independent cross-build comparisons verify every raw RGB555 and PCM byte,
  plus sample-pair counts, across all four three-frame windows. Pointer-bearing
  VM state hashes are used only within the same ELF/checkpoint.
- The writer oracle compares 110 cases against original private bytecode,
  including typed commands/state, RNG, alarms, owner token, skip controls,
  fingerprint rejection, native-disable behavior and preflight decline checks.
  Review exposed missing SETOWNER and integer skip-position semantics; stronger
  tests failed before the corresponding corrections and pass afterward.
- Tile tests cover 643 compositions, including translated/clipped/partial
  pictures, mixed/empty clear-only passes, transparency and real copy-call
  counts. The old implementation failed the copy-work check. Another 368
  existing translated tile frames preserve the cold-picture reference.
- Desktop comparisons cover 150 Deltarune frames and 50 Undertale frames across
  Speed and Accuracy. Four Deltarune frame-skip checks match unskipped output.
  The real encounter writer reaches the native path (zero Draw VM operations).
- Port lint is clean; the standalone snapshot/save suite passes, including both
  WAD versions, audio restoration, progress archives and normal save flushing.
  A first suite invocation inherited GAME=deltarune through MAKEFLAGS and broke
  an Undertale-specific fixture; running the suite standalone resolves that
  invocation error. Full-game ASan/UBSan routes retain the known vendored
  `stb_ds.h` signed-shift diagnostic; they are not globally sanitizer-clean.
- Both GCC 13.2 Pocket ELFs compile with native save states; snapshot symbols,
  BRAM interpreter address and the fsync syscall path are verified. The SDK's
  1,126 recorded files, original game data and previous ZIP remain unchanged.

A preliminary patch-import pilot silently exported unchanged baseline source
and is excluded from optimization evidence. Final snapshots export signed
commits, and both changed source files were checked against their Git bytes.
Private logs preserve this pilot and the failing tests alongside final results
in `.superpowers/writer-castle-fixes-20261017/`.

## Candidate package

`out/writer-castle-check-f3a53f4-20261017/butterscotch-writer-castle-f3a53f4-20261017-update.zip`
contains both game ELFs, Memories/sleep support and the unchanged paired runtime.
Only the two ELFs differ from the previous update: 21 runtime/configuration
files are byte-identical. All 23 archive entries pass CRC and full byte readback;
no game assets or saves are included.

SHA256: `b22661c185f46518496f0848dc90781af4a169beb3a3c17632cba5f17a02cdf3`.

Extract over an existing Butterscotch SD installation, merging the included
folders. Physical testing should cover observation reveal, movement throughout
the outdoor entrance, ordinary save/Quit/Continue, Memories and sleep/wake.
No push, publication, firmware change or SD write was performed.
