# Live background lookup and string search optimization

This follows the [physical flashing-tile results](2026-10-18-silent-tail-pocket-results.md):
room 65 spends substantial time in `__background_get_element`, with repeated
layer-name searches and string conversion allocations. Existing
[tile-shift decisions](2026-10-17-tile-shift-results.md) favor guarded native
translation using live builtins, and the [string-read results](2026-10-15-string-read-results.md)
establish borrowing existing strings while preserving non-string conversion.

## Changes

Signed commit `b6702aa` extends the existing borrowed-string helper to `string_pos`.
Existing needle and haystack strings require zero temporary copies instead of
two. Non-string conversions still allocate and free their own temporaries.
Byte search, one-based codepoint indexing, empty strings and malformed UTF-8
retain the existing semantics. This shared change applies to both games.

Signed commit `b0f96cfb1dda4c6383d3c0cd583053814123e950` adds a guarded Deltarune
native for `gml_Script___background_get_element`, length 1340, VM-normalized hash
`fd927fdac407744c`. It uses live layer/element builtins and removes interpreted
lookup and string helper dispatch. Foreground substring precedence, absolute
slot position, first matching layer and last background element are preserved.
Result types and VARIABLE stack tags match the actual interpreter, including
its INT32 foreground field. Result arrays and current array owner retain token
18385354 for copy-on-write behavior.

Finite REAL/INT32 binds and the default math epsilon are eligible. Unusual
names or unsupported inputs decline; absent backgrounds use the original
creation path before any native semantic mutation. A matched layer with no
background returns the original three -1 values. No names or IDs are cached
between calls, and no serialized state, Memory format, SDK or runtime changes.

## Verification

- Both games pass 10,861 real builtin behavior/ownership checks with ASan/UBSan,
  including new `string_pos` ASCII/UTF-8/malformed/empty/NULL, alias, owned/borrowed,
  numeric-conversion and missing-argument cases. The pre-change source fails
  35 intended allocation assertions; candidate has zero allocation/work failures.
- The real Deltarune bytecode oracle passes 56 checks under ASan/UBSan: typed
  results and owner, duplicate IDs, first-layer/last-element ordering, empty
  layer, dynamic names, fallback creation, custom epsilon, unsupported inputs,
  near-epsilon binds, real move/delete/recreate, caller frame preservation and
  returned-array copy-on-write isolation. A private invalid-registration mutant
  fails 20 work assertions. Eligible native calls enter the interpreter zero times.
- The full standalone snapshot/cache/Memory suite passes. The new fixture and
  shared string fixtures report no ASan/UBSan errors. Leak detection is disabled;
  alignment/function sanitizer exclusions follow existing fixture configuration.
- Fresh full-game comparisons agree on entire PCM, every captured image and
  normalized voice-state hashes: Deltarune Speed at fixed 30 Hz and 100 ms
  (8,672,000/26,016,000 PCM bytes, 13 images each), Undertale Speed and Accuracy
  at 30 Hz (17,324,800 bytes, 28 images each), plus a Deltarune Accuracy route
  model at 30 Hz (8,672,000 bytes, 13 images). The Accuracy route model changes
  only private benchmark mode selection/guard; fixed audio and voice observers
  are also private and do not ship. Deltarune retains the same pre-existing
  stb_ds signed-shift and rvalue conversion UBSan reports as baseline; no ASan
  errors. Undertale comparisons have neither. These runs are not claimed
  globally UBSan-clean.
- An invalid early Undertale comparison reused a test directory and loaded its
  newly written Flowey config. It is excluded. Final directories are newly
  created, empty of saves, and compare against the original hashed baseline.
- All five live diagnostic modes PASS, wrong-room and wrong-route controls reject,
  54 normal/profile raw frames match and five complete reports occupy 19,631
  bytes within the 24 KiB budget. Port lint passes for both games. Independent
  review found no blocker in source or expanded fixtures.

## Work removed in the actual corridor route

| Window | Baseline VM operations/frame | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| Walking |10,622|6,377|40.0%|
| Reset cycle |9,049|4,823|46.7%|
| Standing |8,124|3,898|52.0%|

The background lookup itself changes from 4,245/4,226/4,226 operations to zero,
matching each total reduction. These are operation counts in the guarded route,
not physical timing gains. The previous Pocket work of approximately 63.4 ms
walking and 52.3 ms standing remains the before measurement. Actual timing and
physical Memories/sleep behavior require the next Pocket run.

## Pocket candidate

Both ordinary game programs and the separate Tiles diagnostic build from the
signed combined commit with native Memories enabled. Snapshot symbols, VM entry
0x4000, save-file fsync syscall 82 and diagnostic save namespace are verified.
Native disassembly has no software wide arithmetic or double-precision helper
calls in the changed helpers. All 1,126 SDK files remain byte-identical.

Artifact: `out/background-lookup-b0f96cf-20261018/butterscotch-background-lookup-b0f96cf-20261018-update.zip`.

Bytes: 6,902,811; SHA256 `1596e2369f4599fd31a3a91c3d4494774053cfd712736cba3d6529dcfca2d5c7`.

All 34 archive entries pass CRC and full payload hash readback. Only the three
ELFs differ from the prior silent-tail update; matched os25 firmware/bitstream/
loader and all configs are unchanged. Memories and sleep remain enabled. No
saves or game assets are included. Build inputs, full provenance, instructions
and hash manifest accompany the ZIP. Exact binaries are not physically tested.

Extract at SD root, merge folders and replace included files. Run Deltarune
Tiles 1–5 to PASS, Quit through the Analogue menu after each, and return
`deltarune_tiles_1.sav`. Its reports retain build IDs so the previous 7acc654
measurements can be distinguished. Also check ordinary save/Quit/reopen,
Memories capture/restore, and sleep/wake. No SD installation is performed here.

Private reproducible commands, logs and hashes are under
`.superpowers/background-optimization-20261018/`; disassembly and script
fingerprint discovery are in `.superpowers/background-discovery-20261018/`.
Original game assets stay private.
