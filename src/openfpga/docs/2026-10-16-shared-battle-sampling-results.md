# Shared battle speech and UI sampling — 2026-10-16

This extends the [doubled-font fix](2026-10-16-font-sampling-results.md) on
baseline `bbebcfa`. The shared renderer now samples selected enemy speech,
party/player names, HP labels, and battle buttons for both games. Game-specific
asset selection and active-battle detection live in the existing OfGame
callbacks in game_undertale.c and game_deltarune.c, as required by of_game.h.
The existing battle experiment supplied the renderer delta a24d7ee..aa0e123.

Opaque buttons and transparent white HUD glyphs mix center sampling and a 2x2
average. Selected black speech glyphs use the same helper with stronger partial
coverage, preserving thin strokes. Doubled fonts retain their point sampler.
Only immutable, exact half-size Speed draws with normal blending on the main
framebuffer qualify; other transforms, colors and modes keep ordinary output.
Shared texture buffers and half caches are not modified. Renderer-owned asset
flags are rebuilt on install and freed on destruction. Battle activation is
recomputed before rendering, so no snapshot-format change is needed.

## Verification against bbebcfa

Baseline binaries were preserved from the previous verified source tree
(6803ccfb44e767f4ee427132e8bc78a27e130b3f, identical before/after its rebase).
Candidate desktop builds use the same SDK, WAD16/17 and flags:
`-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined
-fno-sanitize=alignment`. Captures use seed 7, fresh disposable asset-link
directories, volatile benchmark saves, SDL dummy video/audio, and disabled
pacing/flips. Game data and original progress files remain unchanged.

66 captures cover 26 baseline/candidate pairs and 14 skipped-frame comparisons.
All 13 Accuracy pairs are pixel-identical. Six Speed controls are identical:
Deltarune opening, field, sign, Power and Item menus; Undertale overworld
dialogue. All 14 candidate skip2 comparisons match ordinary captures exactly.

| Speed battle scene | Frame | Changed pixels |
|---|---:|---:|
| Deltarune narration, party HUD/buttons | 3050 | 631 |
| Deltarune enemy speech and HUD | 3200 | 420 |
| Deltarune attack meters/HUD | 3120 | 337 |
| Deltarune dodge/HUD | 3323 | 337 |
| Undertale Flowey battle HUD | 2400 | 108 |
| Undertale Flowey enemy speech | 2180 | 683 |
| Undertale controlled Froggit encounter | 2450 | 768 |

The existing bench routes supply Flowey and the Dark World battle. The Froggit
case truncates flowey.script before frame 2400 and jumps at 2400 to room_battle
(306), retaining the initialized battle group 3. It is a controlled recreation,
not imported player progress. It visibly includes player name A and all four
buttons. Runtime diagnostics confirm fnt_curs for Undertale's battle status;
Deltarune's three party-name sprites are explicitly selected and fixture-tested.

Speech and UI changes were visually inspected. Pixel masks confirm unchanged
world regions outside the selected UI in the two speech scenes, the Froggit
scene and the Dark World narration scene, including its raised active-party
HUD. Existing doubled narration remains unchanged.

The baseline's thin-black-stroke probe fails as expected (def7 versus c631).
Both game configurations pass 1260 coverage/isolation cases with ASan/UBSan
halt-on-error; 166 doubled-font and 499 sprite-clipping cases also pass. The
battle fixture covers partial coverage, tint/alpha, odd edges, clipping, cache
isolation, transparent HUD dispatch, missing assets, and inactive/destroyed
battle controllers. Lint and independent code review found no remaining issue.
CI runs the battle fixture for each game using separate object directories.

Fresh-process save/restore of Undertale speech (2150 to 2180) matches exactly.
Deltarune's battle UI also matches after restore (3180 to 3200); its character
animation differs by 197 pixels at (20,22)-(31,116) in both baseline and
candidate. The difference images are byte-identical, so full-frame Deltarune
restore identity is not claimed. This existing animation difference is outside
the speech/UI change. Full game replays also retain the known stb_ds.h:1132
signed-shift UBSan warning in both versions; no whole-game sanitizer-clean
claim is made.

Both native Memories/sleep Pocket ELFs build against staged runtime 92202ab;
executeLoop remains at 0x4000 in BRAM. Actual Pocket appearance and FPS remain
for device testing. No SD-card copy or progress-save replacement was performed.

Private evidence lives in `src/openfpga/out/battle-ui-validation/`: preserved
baseline/candidate binaries, capture.py, capture-results.json, comparison.json,
validation.json, restore-results.json, captures, probes/build logs, and native
ELFs under device/.obj/. Copyrighted assets and captures remain ignored.
