# Focused benchmark validation

**Scene correction:** the user subsequently identified the pictured flashing-tile
checkerboard corridor as the intended room. This historical build sampled the
outdoor castle entrance instead. Its battle-text results remain applicable; its
castle windows do not cover the intended slowdown. See the corrected
[route instructions](2026-10-18-focused-pocket-benchmark.md).

The diagnostic source is signed commit
`4388a476ab538ff208bc6dc948b754868bc07216`. This is a measurement package;
physical Pocket results and the next optimization remain pending.

## Verified locally

- All five fresh-process desktop modes reached the expected scenes and passed.
  Each accepted 38 encounter-reveal, 30 encounter-full, 29 observation-reveal,
  50 observation-full, 70 castle-walk and 90 castle-still samples.
- A wrong-room control failed with process exit 1 and insufficient-scene messages.
- Normal progress/log sentinels remained byte-identical; no diagnostic gameplay
  save archive was created. Diagnostic logs used `deltarune_focus_1.sav`.
- Five sequential runs carried the actual diagnostic log file forward. All five
  unique reports survived (14,162 bytes), including eight script entries and a
  complete total for each of the six Profile windows. Identical copies in the
  logger's retained tail are expected.
- Normal and Profile produced identical raw PPM bytes at 48 frame checkpoints.
  This validates the scripted route with profiling enabled; it does not measure
  Pocket rendering cost or establish audio-output equivalence.
- The same-frame statistics fixture passed with ASan/UBSan. Packaging tests,
  lint, whitespace validation and the standalone snapshot suite passed.
- Full-game desktop routes had no AddressSanitizer errors. They still emit the
  existing vendored `stb_ds.h:1132` signed-left-shift UBSan diagnostic; the full
  application is not claimed sanitizer-clean.
- Independent review found and verified fixes for profiler-log truncation and
  uncounted per-frame draw-description truncation. Follow-up found no blocker.

## Native artifact

Built Deltarune only, with `SAVESTATES=1`, `FOCUS_BENCH=1` and the source revision
embedded in every report. The pinned compiler was GCC 13.2.0 from image
`sha256:1a19d2cf8ec69ca00835cebcffd224492a28fa59efa65ecc977b3e1c724b1d19`.
The existing paired runtime is `902a8adc11309c50c8954af2864ddf893c8b1b0c`.
Snapshot symbols, `executeLoop` at `0x4000`, diagnostic save/log strings and
fsync syscall 82 through an `ecall` were checked in the linked ELF.

The ZIP has 11 additive entries and shares no paths with the previous
writer/castle update ZIP. ZIP CRC and exact payload readback passed. No assets,
saves, OS, loader or FPGA files are included. All 1,126 SDK files remained
unchanged after building, as did both original game data files and the previous
update ZIP. Existing Memories/sleep support is retained by the installed runtime;
the diagnostic should be freshly launched, not resumed from a Memory.

Artifacts are under `src/openfpga/out/focused-benchmark-4388a47-20261018/`:

- `butterscotch-deltarune-focus-4388a47-20261018.zip` (1,082,564 bytes)
- SHA256 `697f160e14f77873f1f2647e40b246421913cbd9532fde149baa54c09a9f9191`
- `INSTALL.txt`, `SHA256SUMS`, `build-inputs.json`, `verification.json`
- ELF SHA256 `3b44083e5402867be0e52ef3d455063fa073024f12794d445340d668c70e227c`

Build preflight found 5.8 GiB free internally, 156 GiB externally, no running
Docker containers, 3,486 MiB VM memory available, and 79 GiB free on Docker's
actual `/dev/vdb1` backing filesystem. No images were imported or deleted, and no
VM was moved or restarted. Private logs and commands are retained under
`.superpowers/focused-benchmark-20261018/`.

See [run instructions and interpretation](2026-10-18-focused-pocket-benchmark.md).
No physical device timing or slowdown improvement is claimed by this package.
