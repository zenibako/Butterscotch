# Reusing translated tile pictures

The Deltarune Chapter 1 spade chase moves a 240-tile layer by (0, -20) room
pixels each frame while retaining the same appearance and relative layout.
A desktop diagnostic checked all 3,600 tile transitions over frames 480–495.
The existing absolute-position cache key rebuilt the picture on each frame.

The change keys the picture by tile appearance and relative effective
positions, retaining fractional phase, tile order, source rectangles, dimensions
and tint. Each queued draw has independent position metadata, while cached
pixels remain immutable. Translated block coverage is conservative, all four
source edges are clipped, and flattened compositions include placement in their
keys. Existing blend/scale and half-size integer/even-position eligibility
checks remain. A reader guard prevents sprite-grid texture eviction from freeing
pictures being replayed. The original tile classifier is unchanged.

## Simulated comparison

These are recorded experiment results, not a new measurement of the extracted
PR branch. Baseline `a7157b6703b67d8e657550da3ac4e628d863b316` and candidate
`f24a52fcc6f02194da1e5ef4ec18d320295a19d4` use independent snapshots with the
same game/scene setup, GCC 16.2.0, port flags, native replacements, BRAM
interpreter, 64-byte alignment, uncached framebuffer, a 1,680-cycle clock
charge, fixed audio and quiet traffic. Means cover three complete frames and
are **simulated at 100 MHz**.

| Window | Mode | Base ms/frame | Reuse ms/frame | Change |
| --- | --- | ---: | ---: | ---: |
| Deltarune spades, 490–492 | Speed | 171.031710 | 120.094703 | -29.7822% |
| Deltarune spades, 490–492 | Accuracy | 482.658940 | 273.900720 | -43.2517% |
| Deltarune Castle Front, 150–152 | Speed | 24.670783 | 25.093257 | +1.7124% |
| Undertale Flowey, 2400–2402 | Speed | 17.869660 | 17.875983 | +0.0354% |

All 16 cases (two revisions × four scene/mode combinations × traced/timing)
pass repeated QEMU capture and same-ELF RTL output/state checks. All 16
corrupted-state controls reject their wrong oracles. All 16 cross-revision and
traced/timing comparisons agree on pixels, PCM and sample-pair counts; 192
direct raw channel comparisons across both repeats agree. Pointer-bearing state
is compared only within the same ELF. Original frozen input hashes still match.

Private evidence is preserved in the harness's `build/tile-motion-r1/`, with
exact case paths, configurations, source/ELF/checkpoint hashes and comparison
results. Validated `results.json` SHA-256:
`20437e7705639b01dc711a8102fffd8e3f134101ddbf7bd1a7f11f2839c938b5`.

The experiment pair shares additional font/contrast work outside this PR.
Main `8d3cb19662e83088b01df4ccacae781582a9d180` has the same pre-change tile
cache implementation; the extracted renderer patch is the same patch tested
above. Measurements retain their original source revisions rather than being
relabeled as results for the new PR commit. Fresh fixture checks on the main-based
PR are described below.

The harness substitutes allocator, file/kernel/clock/audio services, uses cold
RTL caches for each short window and frozen standalone 128-pixel texture packs.
The Pocket uses embedded 64-pixel packs, real storage, interrupts and live audio/
display scheduling. These samples do not establish whole-game timing or a
universal hardware calibration factor. Trace and timing builds are separate;
not every timing build is entirely free of profiling instrumentation.

## Pocket comparison

Device reports identify openfpgaOS 0.9.0, core variant 0 and CPU 100 MHz.
The spade recreation fixes seed 7, solo Kris, room 43 and diagnostics off.
Its report uses `6373a84` for reuse (renderer identical to `f24a52f`) and an
independent `a7157b6` baseline export. The last Castle reuse rerun was stationary,
matching the stationary base; an earlier reuse run with user movement is
excluded from the controlled comparison.

| Speed sample | Base ms/frame | Reuse ms/frame | Change |
| --- | ---: | ---: | ---: |
| Spades 490–492 | 174.0 | 95.3 | -45.23% |
| Spades 493–519 | 177.0 | 97.5 | -44.92% |
| Castle Front 150–152 | 20.6 | 20.3 | -1.46% |
| Castle Front 153–332 | 21.0 | 20.5 | -2.38% |

Spade drawing drops from 123.4 to 45.5 ms/frame in the exact sample, and from
123.9 to 45.0 ms/frame in the longer sample. Step remains approximately
43–46 ms/frame. The complete 519-frame route drops from 66.9 to 38.8 seconds.
Reported tile/sprite-part calls drop from 243 to 3 per measured frame.
Stationary Castle drawing drops from 15.2 to 14.5 ms/frame in the longer sample;
step stays close at 4.3/4.4 ms. The small simulated Castle regression is not
observed in this stationary device pair.

Castle uses seed 7, Kris/Susie, no input, globals set at frame 29 and a jump to
room 46 at frame 30. Both renderer revisions receive the same benchmark adapter.
The adapter is recorded at `bf46f7e`; it does not change the renderer. Benchmark
reports append to `deltarune_1.sav`, with volatile normal saves. Exit through the
Analogue menu flushes the log to the card. All runs finish and halt normally.

There is one independent controlled base/reuse pair per scene; duplicate
identical retained base reports count once. Run-to-run variance is unmeasured.
Report titles identify the chosen program but do not independently hash the
loaded ELF/bitstream. Device Accuracy, Undertale performance and broader normal
play are not measured here. Raw game assets and logs remain private.

Source log SHA-256 values (spades; final Castle log):
- `ee22d27f8efb3e2f64f724ad07abc91fc292f258e132b762ac33485c2697b683`
- `cea3335617f597d5954e97e132799df3b50dc3589c3d177db5ee2d57d9e6f6b5`

## Regression protection on the extracted PR

`make tile-motion-fixture` builds an asset-free test against the real renderer.
It compares warm translated pictures with independently composed cold pictures
and counts renderer allocations. Per game it checks 368 frames spanning opaque,
empty and mixed tiles, all block-placement phases, full/half size, negative
views, nonzero ports, held clears, duplicate placements and queue-capacity flush,
fractional/layer offsets, content/order/source invalidation, independently
moving flattened layers and forced eviction during below/above-grid replay.
The original renderer is rejected for repeated allocations where one is required;
the pressure case reproduced a heap-use-after-free before the reader guard.

On the main-based PR, this fixture passes 368 frames for each game under
ASan/UBSan. Existing clipping (499 cases) and profiler accounting/toggle fixtures
also pass for each game, and both games' port lint is clean. An independent
control compiled the same fixture with main's renderer and the same other
objects: it rejects 40 picture builds where reuse requires one. The renderer
also passes a C++98 syntax check. The normal SDL2 software-renderer CMake
build passes with warnings treated as errors (audio and video backends disabled).
The new placement array has the repository's `MAYBE_UNUSED` annotation because
32-bit builds do not enter the 16-bit cached-picture path; this annotation is
the only implementation difference from the recorded experiment patch. Fresh
Undertale and Deltarune Pocket ELFs build successfully at entry `0x10400000`;
these main-based ELFs have not been rerun on hardware.
The fixture includes the renderer implementation directly; its target excludes
the separate renderer object when linking. Desktop frame/PCM identity and RTL
claims above retain their recorded experiment revisions.

Example (from `src/openfpga`, use a fresh object directory for changed flags):

```sh
make GAME=deltarune PC_BIN=out/tile-test PC_OBJ_DIR=out/tile-test-obj \
  PC_EXTRA_CFLAGS="-g -O1 -fsanitize=address,undefined -fno-sanitize=alignment,function" \
  tile-motion-fixture
DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib ASAN_OPTIONS=detect_leaks=0 \
  out/tile-test_tile_motion_fixture
```

Use `GAME=undertale` with its own object/binary paths to check the other game.
The macOS loader path is needed for SDL compatibility; it is not part of the
Pocket build. This PR contains no font/audio changes or device benchmark adapter.
