# Corrected flashing-tile benchmark validation

The user identified the pictured first checkerboard corridor as the intended
slow room. The old diagnostic sampled outdoor room 104; its battle-text results
remain useful, but its castle windows do not cover this scene.

Source: signed commit `483518c5d8335c0ea854de19c780db668673d104`. Target:
**room 65 `room_field_checkers4`**, with `obj_ob_gen` and `obj_ob_checkertile`.
Original room order and tile/generator bytecode establish the floor's active-tile
collision → white flash → return-to-x340 mechanism. A desktop probe using the
actual battle-prefixed route verified this before selecting the final input.

The corrected route moves down 10frames at1040, then right 150frames at1065.
It measures walking1070–1139, the complete hazard/reset cycle1140–1239, and
standing1250–1349. Frozen reset-animation frames are retained in the reset window.
Room identity, unique player/generator, live tiles, movement, tile phases, white
flash, and a real whitecon1→2/backward displacement to x340 gate acceptance.

## Verified

- All five modes pass all seven windows. Each accepts70 walking,100 reset-cycle
  and100 standing samples. Reset cycle:59 lit/41 dark frames,39 white-flash frames,
  and one verified return in each mode. These are functional host checks, not
  physical performance measurements.
- Overriding the target with the old room 104 fails. Keeping room 65 but walking
  straight past the hazards without the downward step also fails the reset guard.
- Normal gameplay/save-log and previous `deltarune_focus_1.sav` sentinels remain
  byte-identical for all positive and negative runs. No persistent gameplay
  archive is created. New slots use `deltarune_tiles_0.sav` through `_9.sav`.
- All five unique complete reports survive through the shared new log file
  (17,631 bytes). Every one of seven Profile windows retains eight script entries
  and a complete total. Normal/Profile match at54 raw-frame checkpoints.
- Statistics fixture, packaging tests, lint and whitespace checks pass.
  Independent source and empirical reviews found no blocker. Full-game runs
  retain the pre-existing vendored stb_ds signed-shift UBSan diagnostic; they
  have no AddressSanitizer error and are not claimed fully sanitizer-clean.
- Native GCC 13.2 build succeeded using the same pinned image/SDK and paired
  runtime as the previous diagnostic. Snapshot symbols, executeLoop 0x4000,
  fsync syscall 82/ecall and diagnostic namespace were verified in the linked ELF.
- ZIP CRC and exact payload readback pass. Its 11 paths are disjoint from both
  the normal update ZIP and the earlier Focus ZIP. It contains no saves, game
  assets, firmware or FPGA bitstream. All 1,126 SDK files remain unchanged after
  building, along with original game data and both previous ZIPs. Memories/sleep
  remain provided by the unchanged installed runtime; fresh launches are required.

Artifact: `src/openfpga/out/card-castle-tiles-483518c-20261018/butterscotch-deltarune-tiles-483518c-20261018.zip`

ZIP SHA256: `7ab329575fab5752712239d50b6bb4d4a4a36e18a692b36b8a85f7a990597b15`

ELF SHA256: `37dc774da8d0750646447186c71e38e824f78b476f77e9f590beb9f504b39e63`

The output directory includes INSTALL.txt, SHA256SUMS, build-inputs.json and
verification.json. Private evidence and runnable checks are under
`.superpowers/card-castle-focus-20261018/`. Preflight found 3.4 GiB internal free,
156 GiB external free, no running Docker containers, 3,488 MiB VM memory available,
and 79 GiB free on Docker's actual backing filesystem. No images or VM settings
were changed. Physical Pocket results and further optimization remain pending.
