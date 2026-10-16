# Doubled font sampling verification — 2026-10-16

The Speed-mode renderer now point-samples immutable, non-antialiased regular
fonts verified to contain doubled pixels, only at exact positive half scale.
This carries the isolated `c00b831` sampling change into baseline `465caa5`.
The [resolution documentation](../README.md#resolution) describes the existing
Speed/Accuracy tradeoff; this change retains it for other fonts and sprites.

Classification examines every glyph and accepts either 2x2 pixel phase on each
axis. It is cached on the immutable Font across texture reloads. Odd glyph
extents, mirrored text, rotation, other scales, sprite fonts, mutable textures,
and antialiased or mixed fonts keep the generic path. The sampling choice is
per call; no global filter flag is temporarily changed. Font resources are not
serialized by runner snapshots, so the snapshot format is unchanged.

## Baseline comparison

Fresh baseline and candidate desktop builds used both WAD 16/17, the same SDK,
and `-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined
-fno-sanitize=alignment`. Both games used seed 7, SDL dummy video/audio, no
frame pacing or flips, fresh disposable asset-link directories, and volatile
benchmark saves. Deltarune's benchmark enters room_field2 at frame 2600; these
are recreated scenes, not imported player progress.

54 captures produced 22 baseline/candidate pairs and ten additional skipped
frame comparisons. All 11 Accuracy pairs are pixel-identical. Speed field,
opening, Undertale dialogue and Undertale battle are also pixel-identical.

| Deltarune Speed scene | Frame | Changed pixels |
|---|---:|---:|
| Sign | 2950 | 1062 |
| Power menu | 2900 | 1613 |
| Item menu | 2900 | 268 |
| Battle narration | 3050 | 648 |
| Enemy turn / speech bubble | 3200 | 80 |
| Attack meters | 3120 | 66 |
| Dodge | 3323 | 78 |

Sign and menu changes were visually inspected: gray fringes are removed.
Pixels above the sign box are identical. Rudinn's speech bubble rectangle
(166,95)-(246,145) is byte-identical; its scene changes are outside the bubble.
All ten candidate captures with every second frame drawn match their ordinary
captures exactly, covering both modes and both games.

The sign script overrides the bench input with
`2720:R*80,2810:U*40,2860:Z`. Power uses
`2800:C,2810:R,2820:R,2830:Z`; Items uses `2800:C,2820:Z`.
Undertale uses the existing verification skill's flowey.script at 1300/2400.

## Checks and limits

The baseline fails the shifted doubled-glyph regression as expected. The
candidate passes 166 asset-free font cases and 499 sprite-clipping cases under
ASan/UBSan with halt-on-error; lint is clean. Font cases cover phase, clipping
with a visible-ink/outside-viewport oracle, tint/alpha, warmed caches, texture
data reload, and generic fallback cases. CI now runs the font fixture.

Full scene replays retain the existing stb_ds.h:1132 signed-shift UBSan warning
in both baseline and candidate. The scene runs were not halt-on-warning runs;
no sanitizer-clean claim is made for the game as a whole.

Both Undertale and Deltarune Pocket ELFs build with the host RV32 toolchain.
Actual Pocket display behavior, FPS and SD/runtime timing remain untested.
No core installation was performed.

Private evidence is retained under `src/openfpga/out/font-sampling-validation/`:
baseline/candidate binaries and logs, capture.py, capture-results.json,
comparison.json, validation.json, captures/, and device/.obj/*/app.elf.
Game data and captures remain ignored and are not included in this commit.
