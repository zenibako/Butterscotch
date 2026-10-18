# Silent audio tail implementation and validation

This implements the candidate from [the text-voice investigation](2026-10-18-silent-text-voice-investigation.md), following the [physical battle-text measurements](2026-10-18-pocket-battle-text-results.md). It advances proven zero output without shortening a sound or changing playing queries, decoder state, gain ramps, voice stealing, or the Memory format.

## Retained behavior

Cached sounds derive a suffix boundary from their packed bytes. The fast path requires zero predictor/index and interpolation endpoints, a matching live cached allocation, supported pitch/phase, and a chunk strictly before the sound end. Streamed/looping sounds, uncertain metadata, and end transitions use the original scalar mixer. Optional metadata allocation failure also uses that mixer.

Successful cache loads derive metadata, eviction invalidates it, and snapshot apply recomputes it from each replacement allocation without allocating. Snapshot preparation does not mutate live metadata. No metadata pointers or suffix boundaries enter serialized state. The original scalar body is unchanged apart from its name.

## Empirical validation

- The full standalone snapshot suite passes with ASan/UBSan, including new cache eviction/reload, truncated-read, metadata allocation failure, failed prepare, replacement apply, pause/resume, seek, group gain, and repeated file-backed snapshot continuation tests. Leak detection is disabled on this host.
- Actual production dispatch versus scalar fallback agrees for every PCM value and complete voice bytes across 65,552 Deltarune and 144,362 Undertale real-pack chunks. These runs exercise 1,565,644 and 2,785,180 bulk voice pairs respectively. Quiet codes with a nonzero predictor correctly fall back. A scalar-only mutant fails the required-work-removal assertion.
- Fresh full-game PC comparisons use baseline audio from a12208a and the production overlay. Deltarune at fixed 30 Hz and 100 ms matches all 8,672,000 / 26,016,000 PCM bytes, 13 raw frames per model, and cumulative normalized voice-state hashes across 1,355 updates. Undertale Speed and Accuracy each match all 17,324,800 PCM bytes, 28 frames, and voice-state hashes across 2,707 updates. The fixed-delta overrides and hash observers exist only in private comparison copies and are not shipped.
- Deltarune full-game runs retain the same pre-existing stb_ds signed-shift and rvalue conversion UBSan reports as baseline, with no ASan errors. Undertale comparisons report neither. These full-game runs are not claimed globally UBSan-clean.
- All five live diagnostic modes pass; wrong-scene and wrong-route controls reject. Normal/profile agree on 54 raw frames; five complete reports occupy 19,618 bytes of the 24 KiB budget. Both games pass port lint; focused frame statistics and diagnostic packaging tests pass.

In the observation reveal model the new counter reports 14,168 of 18,206 total voice pairs per frame bulk-advanced at 30 Hz, or 46,949 of 54,203 at 100 ms. These include all active voices and are work counts, not Pocket timing or frame-rate claims. The flashing-tile windows record no bulk pairs; this change does not address their separate workload.

Private evidence is under `.superpowers/audio-silent-implementation-20261018/` and `.superpowers/audio-voice-discovery-20261018/production-validation/`; real game assets stay private. The subsequent [physical Pocket run](2026-10-18-silent-tail-pocket-results.md) measured observation reveal work falling from 100.131 to 63.219 ms/frame. A physical Memory roundtrip and sleep/wake check remain unverified despite passing host restore tests.

## Pocket candidate

The signed implementation commit is `7acc6547544b500a1b756dc011cc281c8617c981`. Both ordinary game ELFs and the separate focused diagnostic build successfully with native Memories enabled, the verified GCC 13.2 container, and the immutable SDK. Native snapshot symbols, VM entry address 0x4000, save-slot fsync syscall 82, and diagnostic save namespace are verified. The native mixer has no software wide multiplication/division helper calls. All 1,126 SDK files remain byte-identical after builds.

Artifact: `out/audio-silent-7acc654-20261018/butterscotch-audio-silent-7acc654-20261018-update.zip` (6,899,204 bytes).

SHA256: `7d70991da8b7941658747908cdf482ad5d215f62481d17b2467a36d08478afb5`.

ZIP CRC and all 34 payload hashes pass readback. Compared with the previous writer/castle update, only the two ordinary game ELFs change; the 11 additional files are the new Tiles diagnostic and its launchers. Firmware, bitstream, loader, configs and sleep setting remain byte-identical. The matched runtime remains 902a8adc. Existing runtime timing/transfer limitations are recorded in the sidecar proof; no new FPGA or firmware build is claimed. No game assets or saves are included. Build inputs, payload hashes, installation instructions and test hashes accompany the ZIP.

Extract at the SD root, merging directories and replacing included files. Test normal save/Quit/reopen, Memory capture/restore, and sleep/wake. For physical before/after data, run Deltarune Tiles 1–5 through PASS, Quit after each, and return `deltarune_tiles_1.sav`. The benchmark uses a separate diagnostic save namespace. A prior baseline diagnostic can be compared by build ID within the retained reports. The candidate has since completed the linked physical benchmark; physical Memory restore and sleep/wake validation remain outstanding.
