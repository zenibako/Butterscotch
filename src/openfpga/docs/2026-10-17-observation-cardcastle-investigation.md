# Observation text and Card Castle investigation — 2026-10-17

The shared battle narration/observation writer is a substantial CPU workload. The confirmed Card
Castle outdoor approach is much closer to the frame budget in controlled warm
replays; its physical slowdown is not yet attributed. No production fix or new
shipping package is part of this investigation.

This follows the [character traversal work](2026-10-17-text-traversal-results.md)
and [readable battle names](2026-10-17-readable-battle-names-results.md). The user
reports Speed-mode slowdown while observation letters appear and identifies the
outdoor castle entrance, where movement stays slower throughout the room. The
delivered program source is `0b07bc4`; current
baseline `fd5e895` differs only in its delivery documentation.

## Scene identification and controls

Battle narration and later enemy observations use the general `obj_writer`,
created by `scr_battletext`. The target timing windows measure the initial
encounter narration, "A necklace of Rudinns blocks your path." A separate desktop
route advances through a battle turn and reproduces "Rudinn is glimmering
plainly." The later observation uses the same writer path but has not been
separately timed on the target CPU. This writer is separate from party-name
sprite hinting and from the small `obj_menuwriter`.

The confirmed outside room is **104, `room_forest_castlefront`**, with the original
`bg_cardcastle_outside` background. Room 46, `room_castle_front`, is Ralsei's
castle and is what the pre-existing harness `castle` case measures. Rooms 113
and 114 are indoor passages/halls. Earlier name-based identification of 113 was
rejected after inspecting actual captures and original background identity.

Private scene recreations set party/dark-zone globals before entering. The
outside sustained-work case sets `plot=165`: the original
`obj_castlefront_event_Create_0` checks `plot>=165` and destroys its introductory
event. This is a controlled post-intro recreation, not an imported player save
or a measurement of the first Lancer dialogue. Walking north for 80 frames moves
the player from (300,1080) to (300,760), stopping before the castle doorway.

A before/after HUD desktop comparison preserves all VM-operation totals over
60 ten-frame observation intervals and 20 ten-frame outdoor intervals. Outdoor
pixels match completely at the captured final frame. At its captured final frame,
encounter narration differs only in the party-name HUD region; the narration
pixels remain unchanged. This supports unchanged game work and
output, but is not a native before/after timing comparison or proof that all
hardware timing is identical.

## Target replay measurements

All numbers below are **simulated**, with three consecutive frames per window.
The isolated os25 workflow uses the exact signed source, GCC 16.2, port flags,
natives enabled, alignment, BRAM interpreter, cached framebuffer, modeled clock
read cost 1680, fixed 800 stereo pairs/frame and quiet traffic. This is not the
paired GCC 13.2 shipping build and does not model physical SD latency, display
contention, interrupts or hardware audio timing.

| Window | Frames | Mean simulated ms/frame |
| --- | --- | ---: |
| Encounter narration letters appearing | 437–439 | 72.5424 |
| Same battle, completed sentence | 480–482 | 76.2562 |
| Outdoor approach while walking/scrolling | 100–102 | 30.3153 |
| Outdoor facade after stopping | 200–202 | 25.3995 |

The two exterior windows have different camera positions as well as movement
states; their difference does not isolate the cost of camera motion. The data's
GEN8 default is 30 FPS (33.3 ms/frame), but these samples are not predictions of
physical Pocket FPS or headroom. In particular, cold texture reads and first
entry into the actual player's scene remain separate.

Every case passes repeated QEMU checkpoint/output capture, same-ELF RTL replay
and deliberately corrupted-state rejection. The validated report covers four
cases and twelve sampled frames. Timing builds omit VM opcode tracing; VM-work
comparisons above come from independent desktop script profiling.

## What the writer actually does

The exact 12,780-byte writer Draw event loops from character 1 to `pos` every
frame, including after reveal is complete. In this plain encounter narration, it draws
individual characters with both ordinary and colored/shadow text calls. A full
sentence produces about 49 `draw_text` calls (including ordinary HUD text), 41
`draw_text_color`, 44 `string_char_at`, and 166 `random` calls per frame. Its Draw
event executes 5,443 VM operations per completed-sentence frame in the measured
route. The existing bounded `char_at` change removes a length scan but leaves
this repeated script work intact. The separately captured later observation
executes 4,147 writer Draw VM operations per completed-sentence frame, with 40
`draw_text` calls including the HUD, 32 `draw_text_color`, 32 `string_char_at`, and
130 `random` calls per frame. These are desktop script-work counts, not target
timing measurements.

A passive target observer preserves the typing case's exact frame cycles and
output oracles. Its enabled, disabled and corrupted-oracle controls pass.
Attribution by **retired instructions**, not elapsed function time, ranks
`swrDrawSpriteInternal` at 24.09%, `mixAndWrite` at 17.06%, `executeLoop` at 15.30%
and `memcpy` at 12.11%. Interpreter helpers add work outside `executeLoop` itself.

The completed sentence costs more than the partially revealed one in this
recreation. Therefore a special cost of revealing the next letter or its sound
has not been established as the sole cause. The visible-prefix redraw continues
and grows with sentence length. A sound-only counterfactual was not run.

Unimplemented fog warnings were checked and excluded as a writer-specific
explanation: they occur in flashing hero/enemy draws, not per observation letter.
A native writer must preserve RNG advancement even when `shake=0`; removing its
`random(0)` calls would change subsequent game behavior.

## Exterior direction and next work

The scrolling exterior's passive profile ranks `executeLoop` at 16.11% of retired
instructions, `memcpy` at 14.35%, `swrRunsDrawOnto` at 13.34%,
`swrDrawSpriteTiledRows` at 13.19%, and `mixAndWrite` at 11.84%. Thus tile/background
composition and copying are concrete candidates here. These instruction shares
are not additive estimates of speedup or exclusive CPU time.

The strongest next text experiment is a guarded native path for the writer's
common plain observation style, retaining formatting/animation fallbacks, exact
state mutations, alarm/input behavior and RNG advancement. General string-copy
or allocator changes alone are not justified as a cure by this profile. For the
exterior, first evaluate avoidable tile/background passes or composition work
with exact-pixel counterfactuals. Do not drop visual layers based only on their
instruction share. The user confirms sustained movement slowdown throughout the
room, which makes recurring scene work relevant; these warm captures still do
not establish the cause of the physical slowdown.

Private commands, captured images, exact disassemblies, failed setup cases,
input manifests, replay/negative controls and observer results are preserved in
`.superpowers/observation-castle-20261017/` and the separate private harness workflow
`experiments/workflows/observation-cardcastle-20261017-r1/`. Frozen inputs, existing
cases, original game files, SDK/runtime, VM and prior shipping ZIP are preserved.
Desktop full-game routes retain the documented vendored stb_ds signed-shift
UBSan diagnostic. No physical Pocket verification, new source optimization,
benchmark firmware release, push or SD write is claimed.
