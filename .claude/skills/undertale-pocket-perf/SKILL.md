---
name: undertale-pocket-perf
description: Measure and diagnose performance of the Undertale port on the Analogue Pocket — frame rate, load time, SD read speed, texture-load freezes, audio dropouts. Use this whenever the user reports something slow, choppy or frozen on the device, sends a benchmark table or overlay screenshot, asks whether a change helped, asks about os20/os25 or openfpgaOS versions, or before starting any optimisation work in src/openfpga. Also use it when you are about to explain why something is slow: this skill records which explanations have already been tested and ruled out.
---

# Performance work on the Pocket

## Reading the log off the card

Prefer the log file to screenshots: it is the whole log, not the last 21
lines. It is written to the game's spare save slot when the core halts
(benchmark end, fatal error), from the menu's "Save log" and on Select +
Y with the debug buttons on, and reaches
the card when the user leaves the core through the Analogue menu:

```bash
tr -d '\0' < /Volumes/Pocket/Saves/butterscotch/common/deltarune_1.sav   # or undertale_1.sav
```

Check the first line (`Butterscotch log, <game>`) and the timestamps: the
file stays on the card until the next dump overwrites it, so an old one can
be mistaken for a new run. A benchmark's report ends with ms-per-frame and
calls-per-frame tables by section; start there before theorising about
where a section's time goes.

## Speed and accuracy

L switches between two settings, and the core starts in **speed** (the
desktop build starts in accuracy, because frame comparisons are made against
what the game asks for; `UT_SMOOTH=1` gives it speed). The user's rule,
2026-10-09: accuracy is the target. A shortcut that changes the picture or
drops frames belongs under speed only, and whenever an optimisation brings
accuracy up to speed's frame rate in some domain, remove speed's shortcut
there so the two settings stop differing in it. Before adding a new
speed-only shortcut, look for an exact optimisation first; after an exact
one lands, check which speed shortcuts it has made unnecessary. `--bench`
measures accuracy and `--bench-smooth` speed, so the pair shows where the
two have reached parity.

## Measure first; this project has paid for guessing

During the port, three confident explanations for a slow load were each
wrong, and each cost the user a card trip to disprove:

1. "The file is opened with a buffer, so big reads are served as small
   ones." A buffered 1 MB read measured as fast as an unbuffered one.
2. "Reading into heap memory is slower than into static memory." Reading
   into a heap block measured just as fast.
3. Both rested on a benchmark that re-read the same 2 MB, so every pass
   after the first was a cache hit. The real cause was that first-time
   reads are slow at every request size.

The pattern to avoid: a plausible story, a fix built on it, and a request
for the user to test the fix. The pattern that worked: add a measurement
that separates the candidate causes, have the user run it once, then fix
what the numbers point at. When you do offer an explanation before
measuring, label it as a guess and say what number would confirm it.

Check that a test measures what you think it does. If two readings that
"should" agree don't, suspect the test before the system.

## What is known (measured on the user's Pocket, v0.7 runtime, os25)

- **CPU:** roughly 100–170x slower than this Mac per frame of game work.
  Desktop work of 0.3 ms per frame is about 31 ms on the device.
- **SD reads:** about **1.1 MB/s for data read the first time**, the same
  for 4 KB, 64 KB and 1 MB requests. Repeat reads of the same region come
  back at about 13 MB/s from a cache below the app. Nothing about how a
  read is issued changes the cold speed; only reading fewer bytes helps.
- **Where the 1.1 MB/s comes from** (benchmark, 2026-10-08, same on v0.7
  and v0.9; OS source read at openfpgaOS 8318f25): `fread` is served from
  the OS's 4 MB cache in 32 KB blocks, one host command per block, with
  two copies on the way. A single non-blocking read (`of_file_read_async`)
  takes 6 / 13 / 23 / 42 ms for 4 / 16 / 32 / 64 KB: about 3.5 ms fixed
  plus 0.6 ms per KB, so the host delivers about 1.7 MB/s and that is the
  ceiling. A megabyte in 64 KB commands runs at 1.5 MB/s into the OS's
  staging memory and 1.3 MB/s into ordinary memory. The per-command round
  trip is NOT the bottleneck (a guess that the benchmark refuted). The
  OS's file idle hook is deliberately not called during an app's reads on
  the Pocket; feed audio from the app.
- **Memory fill/copy:** tens of MB/s. A 640x480 16-bit frame is 614 KB, so
  every extra full-screen clear or copy costs on the order of 15 ms.
- **Heap:** about 51 MB for everything (game data, texture cache, audio).
- **Division is slow**; avoid per-pixel divides in the renderer.
- **OS version and CPU variant:** v0.9 on os25 is identical to v0.7 on
  os25 to within 0.1 ms. v0.9 on os20 is only 2–13% faster, and showed a
  garbage screen when the port used its 640x480 16-bit mode. Neither is a
  route to more speed.

Benchmark history, ms of work per frame (33.3 is full speed):

| Section | Start | + draw into display buffer | + skip redundant clear |
|---|---|---|---|
| Intro, menu, naming | 45.2 | 39.3 | 37.1 |
| First room 320x240 | 38.8 | 33.5 | 31.2 |
| Flowey dialogue 320x240 | 52.9 | 44.6 | 40.9 |
| Flowey battle 640x480 | 86.7 | 60.6 | 46.3 |

The battle at 320x240 with 2x2 smoothing (L button) runs at 28.3 ms. The
user chose crisp 640x480 as the default knowing that trade.

Load time went 31.6 s → 21.0 s (stop reading the 12 MB texture chunk that
is only needed lazily) → about 19 s (stop reading the string chunk twice).
What remains is about 11 s of unavoidable cold reading of roughly 12 MB,
1.4 s of parsing and a few seconds of VM start-up and first textures. The
startup chunks compress about 4.4:1 with zlib, which is the remaining
lever; the decompressor would have to be much faster than inflate to pay
off on this CPU.

Texture page loads take about 1.3 s for a 1024x2048 page: a cold read of
the run-length-encoded page plus the decode. That is the "brief freeze"
when a dialogue box or new room first appears.

### Reads and multiplies both cost; the compiler does not help (2026-10-09)

*Measured:* a full-screen translucent fill at 320x240 takes 15 to 19 ms even
when it only reads each pixel and compares it with the last (no arithmetic,
no write): about 245 ns a pixel just to read the framebuffer. A fill that
only writes is about 2 ms. Copying a cached tile picture to the screen costs
about 110 ns a pixel. `-O3` and LTO changed nothing (within 2%).

*Measured, and it overturned two conclusions in a row:* a tiled pass that
put down about 6,000 scattered pixels (one per row per copy of a tile of
thin lines) took 6 ms. Taking the buffer reads out of it (the colour under
each pixel known) left it at 6.4 ms. Taking the blend's arithmetic out of it
as well (the last answer kept) left it at 5.9 ms. Two divisions a pixel, by
contrast, made it 15 ms, so divisions are dear. *Inferred, not yet confirmed
on the device:* what is left is the store itself: each pixel is on a cache
line of its own, and a store to a line not in the cache costs about a
microsecond, where a fill of consecutive pixels costs about 27 ns each. The
change made on that inference writes such a pass together with the clear
under it, row by row, so that its pixels land on lines the fill has just
brought in (`swrTiledHold`).

*Measured:* with the drawing left out altogether (`--draw-every 100000`,
the "no drawing" benchmark entry) a Deltarune battle frame still takes 46 to
50 ms: draw-event scripts about 24 to 30, step 7 to 21, sound 6 to 12. So
battles cannot reach full speed by drawing less or skipping frames; the
field (24 ms undrawn) can. Making the code smaller does not help: `-Os` (768 KB of
code against 1.27 MB) is 16 to 22% slower and unaligned code 2 to 4% slower.
That does not settle whether fetching code from SDRAM is what holds the
scripts back: `-Os` also inlines less, and neither build moved the hot code
out of SDRAM. The direct test was the interpreter's loop (8.9 KB) in the
app's 14 KB of uncached block RAM (`of_fastram.h`, `-DUT_FAST_LOOP`).
*Measured:* 2 to 4% of an undrawn frame in every section (battle rows 46.7,
47.2, 47.5 became 45.3, 46.2, 46.4). Kept as the default, but it shows the
loop itself is not where a script operation's time goes; the desktop
profile, which put more than half in the loop, does not carry over.
A pool for script call frames in place of calloc and free
gained 0.2 to 0.3 ms, within noise, and was dropped. With the script
profiler on and nothing drawn, scripts are 35 to 39 ms of a battle frame;
plain scripts run at about 1.9 microseconds an instruction and `scr_charbox`
at 4.8, about 10 ms a frame, so its time is not all interpretation. The
profiler now times built-in functions under their own names, with calls per
frame, to say what the rest is.
*Measured the same day:* the collision-threshold fix took 3.4 ms off the
dodging section's step time; the other three double-precision fixes showed
nothing.

*Found by reading the device build's disassembly, effect not yet measured:*
places that fall into software double precision on this single-precision
FPU. `riscv64-elf-objdump -dlr` on an object built with `-g`, looking for
calls to `__*df*` and `__*di*` helpers, names the source lines. The ones on
a per-frame path were the collision thresholds (three compares for every
pixel a precise test looks at), the image_index sum for instances that are
not animating, double constants pushed by scripts, and `div`.

So, before optimising a draw, find out which it is. The benchmark report's
"costliest draws" list (`sw_call_notes.h`) names the calls and times parts
of them; three guesses at the tiled background were wrong before that list
existed, and one after. What has worked: not drawing at all (held layers
dropped under an opaque full-screen fill), fewer passes over the screen, and
no division per pixel where a running value will do.

## Findings from play sessions (2026-10-07)

- **Stacked fade overlays.** Undertale's door script creates a full-screen
  fade object on every frame the player overlaps the door: once if the
  player is at the doorway's edge (a wall pushes them back out), 13 times
  through the middle. This is the game's behaviour, not a port bug, and it
  reproduces on desktop (`650:R*26` against `650:R*30` in the door replay).
  Each full-screen blend is about 37 ms. Butterscotch now folds consecutive
  identical solid overlays into one blend (`swrOverlayFlush`).
- **Sound gaps.** Slow frames showed a few ms of mixing across multi-second
  loads, so the OS file idle hook was not feeding audio on the v0.7
  runtime (*inferred from those timings*). `platformBusyTick` now feeds the
  queue between read pieces and during drawing; the user confirmed the
  cutouts stopped.
- **Tile-heavy rooms.** The ruins room outside Toriel's house drew 437
  tiles for 26 ms and ran at 44-48 ms of work per frame. With the tile-run
  cache (`drawTileRun`) the user measured about 25 ms.
- **What a slow frame's draw time contains.** The held overlay and the
  tile picture are drawn outside or under different counters than before:
  an overlay's blend lands in whichever call flushes it, often none of the
  five kinds, so the kinds can add up to less than the draw phase.
- **Texture memory (2026-10-08).** The Pocket could not allocate the 8 MB
  that page 23 (2048x2048, the monsters) needs: battles showed no enemies,
  and each failed attempt threw out every other page, 1.3-2 s per frame.
  With pages loaded whole there was room for roughly 20-25 MB of them, so
  entering and leaving a battle re-read about 8 s of pages. The pack is now
  tiled and the renderer keeps one texture per TPAG item
  (`swrTextureForItem`); on desktop the same walk into a battle holds 2 MB.
  A page that fails to load is not retried for 150 frames.
- **Screenshots stall file access for 2-4 s.** fread waits it out. A
  direct read issued during one never completes, so those time out after
  1.5 s and fall back to fread for that piece.
- **Still open.** One frame of 150-250 ms on each room change (room load
  19-75 ms plus the first draw), and one unexplained 699 ms music read
  seen before reads were cut to 16 KB pieces.
- **The log overlay costs about 60 ms a frame** (average work 107 ms with
  it on against 44 ms off in the same room). Read the Select counters with
  the log off.

## Instruments

**Slow-frame log lines.** Any frame over 150 ms of work logs three lines
that fit the overlay's 53 columns:

```
slow 555: step 300 draw 200 out 20 snd 10
  load: rm 0 tx 0 fx 0 mix 12 mus 0
  draw: s5/380 p1/9 t12/40 b1/3 r0/0
```

Phases first (game code, drawing, presenting, audio update), then jobs
inside them (room, texture and sound-effect loads, audio mixing, streamed
music reads), then calls/ms per kind of draw call (sprites, sprite parts,
text, tiled backgrounds, rectangles; `-DSW_DRAW_PROFILE`). Writing a log
line to stdout costs about 20 ms on the Pocket, so the console is switched
off after the first frame; keep logging out of per-frame paths regardless. For a hitch, ask the user to press R straight afterwards and
screenshot the log. Two traps when reading these: a Pocket screenshot
freezes the core for 2-4 s and appears as one huge frame, attributed to
whatever was running (a 3 s "music read" or "texture load" right after a
screenshot is the screenshot), and the log overlay only shows the first 53 characters of a
line, so anything longer is invisible on the device.

**Script times** (the menu, or Select + X with the debug buttons on;
`UT_PROFILE=n` on desktop).
The slow-frame line says "step 300"; this says which scripts the step
went to. It is Butterscotch's GML profiler (`--profile-gml-scripts`),
reported every 60 frames in a form that fits the log overlay:

```
scripts 6.4 ms 5210 ops /frame (41, 60 fr)
  2.1  1830   1.0 obj_mainchara_Step_0
  1.2  7218   3.0 obj_base_writer_Draw_0
  ...
```

First line: all game code together per frame, how many scripts ran, the
window. Then the heaviest twenty: ms per frame, VM instructions per frame,
calls per frame, name (without `gml_Object_`/`gml_Script_`). Reading it:
- Built-in functions are entries of their own since 2026-10-09 (0 ops),
  so a time is an entry's own: neither the scripts nor the built-ins it
  calls. A drawing built-in's time is the renderer's.
- ms divided by ops is a script's cost per instruction, now without its
  built-ins; look for those by name further down the list.
- Desktop times are near zero and mean nothing; the ops column is the
  same on both, so the desktop can rank scripts by instructions for a
  scene before asking for a device run.
- Timing every script call costs time itself. Take frame-time numbers
  with it off. The cost of having it compiled in but off (a test per
  instruction) has not been measured on the device.
- The report is up to 21 log lines every two seconds, more than the
  overlay shows: read it from the log file, not a screenshot.
When asking the user for it: go to the scene, Select, "Show script times in log" (the
log comes up with it), close the menu, wait a few seconds, screenshot.

**Opcode ranking** (desktop only: `make ops`, then run `undertale_pc_ops`
with `UT_EXIT_FRAME=n`, usually with `UT_PLAYBACK` or `UT_SCRIPT`). Prints
how often each bytecode instruction ran, split by operand types, when it
leaves the main loop. Counts are the same as on the device. Use it before
hand-tuning the interpreter, to see which instruction and type
combinations a scene is made of; it does not say what each costs.

**Benchmark** (`--bench` in the OS config's `ARGS=`; `make compare` adds a
"Benchmark" entry to each core). It plays a fixed input script with a
fixed seed, no frame pacing and saves disabled, then draws a report:

```
OS 0.7.0, core variant 0, CPU 100 MHz
load to first frame: 19.1 s
slowest chunks: CODE 4.8s ROOM 3.8s SPRT 1.8s ...
phases: alloc 574 ms, read 10799 ms, parse 1384 ms, free ...
ms per frame:          work   total
intro, menu, naming    36.8   37.9
...
SD read, KB/s, 1 MB each from music.bin:
cold: 4K 1091, 64K 1102, 1M 1115
same 64K region again: 13441
```

"work" excludes the display flip. The report does not identify the build,
so ask which build a screenshot came from if it matters, and remember the
user may be one build behind what you last produced.

Variants: `--bench-smooth` (320x240 with smoothing), `--bench-lowres`
(320x240, point sampled). Edit `platform/ut_bench.c` to add a measurement;
keep the report within 20 lines of 53 characters, which is what fits on
the 320x240 report screen.

**Overlays** (switched on in the menu: press Select, then "Show frame times"
or "Show log overlay"): frame times are four numbers over 30 frames (average work,
worst work and worst frame period in ms, then frames skipped); the menu's
bottom line gives the same numbers in words whether or not the overlay is
on. The log shows the last log lines over the game; R toggles it too with
the debug buttons on. The log overlay itself
costs a lot of frame time, so read the numbers with it off.

**L, speed or accuracy.** Always ask which the user was in; numbers from
the two are not comparable. Speed does three things: 640x480 rooms at
320x240 smoothed, faint mirrored blend layers left out
(`swrMirrorFaintAlpha` 8, against 0 in accuracy), and frame skipping. With
skipping on, "average work" falls because skipped frames are cheap, and
the worst period can go under 33 (a frame shortened to pay back time), so
judge a scene in speed mode by the skipped count: 0 means it holds full
speed drawn, 15 means every other frame is being dropped. To measure what
a scene costs to draw, use accuracy mode or the benchmark, which never
skips. A new fidelity-for-speed trade-off belongs under this toggle (see
the comment above `decideFrameSkip` in `platform/of_platform.c`), not
behind a new button; one that cannot be switched at run time (colour
depth, mono audio, float reals) is a known difference for the README.

**Log lines worth knowing:** every line carries seconds since start.
`DataWin: NAME, n KB` marks each chunk as loading starts on it.
`SWR: Loaded TXTR page N (WxH, pack|PNG), cache K KB` and
`SWR: Unloaded TXTR page N` show the texture cache; pages reloading
repeatedly mean the cache is thrashing. `Video: 640x480, stride 1280`
marks a mode switch. `Audio: playing NAME` marks a streamed track.

## Working through a report from the user

1. Reproduce the scene on desktop if you can (see the
   undertale-pocket-verify skill) and read the log for texture loads,
   room changes and mode switches around the moment they describe. A
   freeze that lines up with `Loaded TXTR page` is a page load.
2. If the slow part is `step`, get script times for the scene: on desktop
   for the ranking by instructions (`UT_PROFILE=60`), and from the user
   (Select + X) for real milliseconds.
3. Profile the desktop build with `sample <pid> 5` while the scene runs.
   The game's own functions are a small share of samples next to the SDL
   display code, so read relative weights among `swr*` and VM functions
   only.
4. If the cause is still unclear, add a measurement to the benchmark or
   the log rather than a fix.
5. When you do change something, verify output is unchanged on desktop,
   then ask for one benchmark run and name the lines you need.

## Asking the user for a run

Card trips are the scarce resource. Batch changes that are independent and
individually verifiable, but keep a risky change (anything that alters how
frames reach the screen, video modes, or the OS runtime) separable so a
failure can be attributed. Say exactly which entry to run on which core and
which lines of the result you need. State predictions before the run
("load should drop to about 19 s") so the result can confirm or refute
them, and say plainly when a result refutes one.

## A clock read is a trap: 17 us (2026-10-10)

`nowNanos()` on the device is `clock_gettime`, an ecall into the kernel:
**16.8 us** per read, measured by the benchmark's "call costs" line.
VexiiRiscv here does not expose `rdcycle`/`rdtime` to user mode (they trap as
illegal instructions), so there is no cheaper clock. Consequences:

- A sprite draw that draws nothing costs 40 us, of which 6.5 us is the call
  and 33.5 us was the two clock reads timing it. A battle makes about 100
  draw calls a frame: 3 to 4 ms of timing, more with the mixing split.
- Draw calls and mixing are therefore only timed "in detail"
  (`utPerfDetail` / `swrDrawTimed`): every frame while an overlay or debug
  mode is on, one frame in 8 during a benchmark (`UT_BENCH_DETAIL_EVERY`),
  never in plain play. The benchmark's spr/til/txt/bkg/rct columns, the
  mix/voices/write columns and the costliest-draws list are averages over
  the timed frames; frame times and step/draw/snd/out are of every frame.
- Benchmark numbers from before this change (through build `72eed8e`) carry
  that overhead in every section, and numbers before `946af8c` also carry
  about 0.15 ms per draw call of note formatting.
- Before adding any timing to a hot path, count the reads per frame. The
  script profiler (Select + X, `--scripts`) pays two per script and built-in
  call, about 18 us with its lookup: its times rank things, nothing more.
- `of_audio_free()` is 0.5 us, so the audio queue can be polled freely.

## Why reading the frame buffer is slow (2026-10-10)

*From the OS source, not yet confirmed for the v0.7 runtime on the card:*
`openfpgaOS/src/firmware/os/targets/pocket/target_platform.h` says app frame
buffers live at the **uncached** SDRAM alias (`0x50xxxxxx`) so that pixel
writes do not push the app's data out of the cache; every load from one is
then an AXI round trip, whatever its width. That fits the measured 245 ns a
pixel to read against 27 ns to write. (The newer OS in that clone moves them
to the cached alias with a flush at the flip; the address `of_video_surface()`
returns says which a build has.) What follows from it:

- Anything that reads the frame buffer per pixel is the expensive kind of
  draw: translucent fills and sprites, text edges, a pass over another.
  Work out the colour without reading where the layers under it are known
  (`swrFillHold`, the held clear, `swrTiledHeldWrite`'s row buffer), or put
  the rows together in ordinary memory and copy them out.
- If reads must happen, two pixels per 32-bit load halves them.
- Scattered single-pixel stores are each a round trip too; consecutive
  stores are what is cheap.

## Sprite-path layout experiment closed (2026-10-10)

*Measured on the Pocket, f42a23c, v0.7/os25, Deltarune Speed benchmark:*
the ordinary build's 40x40 sprite probe was 19.87 us outside the view and
73.48 us inside; the adjacent-function build was 19.56 us and 78.34 us.
Battle work changed from 67.8/80.2/62.8 to 67.5/80.0/62.7 ms; field work
stayed at 44.4 ms. The hot build booted and completed. One probe-bearing
run per build, so there is no run-to-run variance estimate.

The proposed criterion was a result well below 19.9 us outside the view;
this layout failed it and is dropped as a performance direction. It does
not establish that instruction-cache misses are absent elsewhere: the
probe is 2000 repeated renderer calls after warming the sprite, bypassing
the GML builtin and VM. No cache-miss counters are measured.

See the [result and exact device-report excerpts](../../../src/openfpga/docs/2026-10-10-sprite-layout-results.md).

## Code layout is not it (2026-10-10)

*Measured, and it overturned a hypothesis:* a small sprite draw costs about
15 us on the Pocket between the renderer's door and finding itself out of
view, for some 150 instructions, and its functions are spread over 900 KB of
a 1.25 MB program with a 32 KB instruction cache. Linking those sixteen
functions side by side (`ld --section-ordering-file`, 14.7 KB in one
stretch) changed nothing: the sprite probe read 19.56 against 19.87 us out
of view and 78.3 against 73.5 in view, and every section's frame time was
within 0.5 ms. So instruction-cache conflicts among a draw's own functions
are not where that time goes. (Together with `-Os`, unaligned code and the
loop in block RAM, that is four layout experiments with nothing to show.)
The benchmark now ends with a staged probe instead (`swrSpriteCostProbe`):
the same draw cut short at six points, each timed over 2000 calls with
audio top-ups off, on a grid's own sprite when one was drawn.
