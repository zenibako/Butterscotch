# Bounded character lookup — 2026-10-17

The candidate removes the full input-length scan from `string_char_at`, keeping
its prefix scan, unchanged decoder and owned output. No string cache is added.
Retain the local change based on verified traversal removal and exact output
checks; physical Pocket benefit remains unmeasured.

This follows the [sequential design](../../../docs/superpowers/specs/2026-10-17-sequential-optimization-design.md)
and the [earlier string borrowing results](2026-10-15-string-read-results.md).
Baseline is `8cfb502830115794cd0e6b39043683bf00937610`; candidate source is
`007ca69c5ae30d5ae651df1d4eb155fffb0c4ea1`.

## Fresh measurements and scope

Counters in an exact private baseline export rank `string_length`, `string_copy`,
`string_char_at` and `string_ord_at` in actual dialogue and battle routes.
`char_at` dominates the measured traversal; `ord_at` is not reached. Counters
observe logical byte advances, not machine loads or elapsed time. Counter-on/off
pixels and event records match at the four selected scenes.

| Route, char_at only | Calls | Baseline full-length bytes | Candidate local-window bytes | Baseline / candidate prefix bytes |
| --- | ---: | ---: | ---: | ---: |
| Undertale through 2606 | 96,577 | 5,692,864 | 378,094 | 2,386,784 / 2,419,469 |
| Deltarune through battle 3047 | 34,157 | 1,108,711 | 134,039 | 401,615 / 405,182 |

Including unchanged decode spans, total observed logical traversal falls from
8,176,012 to 2,893,927 bytes in Undertale and 1,544,349 to 573,244 in Deltarune.
Out-of-range indices now traverse the prefix to NUL instead of rejecting after
the full-length scan; that explains the small prefix increase. They remain O(n),
and libc length scanning may be faster for that particular path. These counts
are work evidence, not hardware speed measurements.

The implementation advances one byte and following continuation bytes per
index, preserving the existing grouping even for malformed UTF-8. At the target,
it bounds the unchanged decoder to at most four non-NUL bytes. Existing conversion,
borrowed-input lifetime, empty result, decoder truncation and independently owned
result contracts remain. No other builtin or shared text helper changes.

## Verification

Both WAD16 and WAD17 pass 10,823 behavior/ownership checks under ASan/UBSan,
including 10,648 byte-sequence oracle cases, literal malformed/truncated UTF-8,
embedded NULs, index boundaries, non-string conversions and output ownership.
Alignment/function checks are excluded and leak detection disabled. The baseline
passes characterization but fails seven intended work bounds at 2,048 length
bytes each; candidate length work is zero in those probes. The controller reran
both final fixtures successfully. Both builtin fixtures and the staged-SDK
snapshot suite also pass.

Actual Undertale introduction/attack text and Deltarune opening/battle-menu
scenes match in all 48 baseline/candidate raw RGB555 comparisons across both
modes and counter settings. Sixteen counter-on/off scene controls match too.
Observed completed-frame event count/hash sequences agree. Undertale frame 2606
exits before the completion hook, so it has pixel and cumulative work evidence
without a completed-frame event record.

Normalized text state diagnostics match for Deltarune. Undertale differs only
in wall-time-derived `idle_time`, also variable in baseline controls. These
diagnostics are not a structural serialized-state proof. Captures have no ASan
failures, but both revisions encounter the existing vendored `stb_ds.h:1132`
signed-shift UBSan diagnostic. The whole capture suite is therefore not claimed
sanitizer-clean; focused string and snapshot checks have no such diagnostics.

Independent review reports spec PASS, quality PASS and zero actionable findings.
The exact paired GCC13 device compiler accepts the frozen builtin translation
unit; `string_char_at` has a 48-byte static stack record. Full native linking
remains part of final combined verification.

## Replay provenance and limits

The normal Flowey 2400–2402 and dodge 3323–3325 replay windows have no calls to
this builtin. A new isolated workflow adds only a Deltarune dialogue window at
1002–1004, where the desktop probe observes 30 calls/frame. All 1,631 frozen
input hashes remain verified, and existing simulator artifacts are copied with
exact hash checks. No original harness case or input is edited.

Private source, commands, binaries, raw captures and analysis remain accessible
under `src/openfpga/out/sequence-text-20261017/`. Completed host evidence was
moved to `/Volumes/1TB M.2/pocket-optimization-evidence-20261017/` with complete
byte/link verification and original-path symlinks. Replay/native evidence is in
`.superpowers/sdd/2026-10-17-sequential-pocket-optimizations/`.
Production source SHA-256 is
`d65b9d2e1336a5fcb1e2266d8cbf5ef81fefaaddc70b24809758291e92841f18`.
No physical Pocket or whole-game performance claim is made.

## Affected fixed-audio screen

Both committed revisions pass repeated QEMU capture, positive same-ELF RTL
replay and rejected corrupted-state controls for dialogue 1002–1004. All 24
cross-repeat RGB555/PCM comparisons are byte-identical with 800 pairs/frame.
Mean time changes from 33.115770 to 32.893880 simulated ms/frame (-0.670%).
This small difference remains inconclusive for hardware and is sensitive to
code layout. The measured scan removal and focused semantics support retention;
no Pocket speedup is established.

Configuration is GCC16.2, port flags, alignment64, natives enabled, BRAM
interpreter, cached framebuffer, modeled clock-read cost1680 and quiet traffic.
This differs from the paired GCC13 native device build. Exact cycles/configs
and validation are in `text-dialogue-accuracy-comparison.json`. Shared-game
coverage is the two-game fixture and actual desktop text/battle matrix; this
fixed-audio replay covers only Deltarune Accuracy dialogue.
