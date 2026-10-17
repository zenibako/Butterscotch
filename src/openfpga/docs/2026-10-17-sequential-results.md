# Sequential optimization results — 2026-10-17

Three local CPU changes are retained. The loading candidate is deferred because
it trades fewer requested bytes for more refill/seek operations without a
measured target loading benefit.
Physical Pocket benefit remains unmeasured.

This executes the [approved design](../../../docs/superpowers/specs/2026-10-17-sequential-optimization-design.md)
from merged main `cd929134b36ad07eb6aaa778dc81f842ff510cc4`.

| Topic | Decision | Evidence |
| --- | --- | --- |
| Precise collision preparation | Retain `667feb8` | Repeated mask/transform setup removed; dodge simulated Accuracy -1.547%, Speed -3.140%. |
| Guarded native tile shifting | Retain `29ba043` | 11,210 VM operations removed per sampled spade frame; simulated Accuracy -17.232%, Speed -35.446%. |
| Bounded character lookup | Retain `007ca69` | Full-string length scans removed from char_at; affected dialogue simulated -0.670%, too small for a hardware conclusion. |
| Lazy texture metadata streaming | Defer | Modeled requested bytes fall, but refills rise from 4 to 26 UT / 8 DR; target latency/parser cost remains unmeasured. |

These are separate windows and consecutive source comparisons, not additive
speedups or an end-to-end device measurement. The small collision/text changes
remain layout-sensitive; work removal and focused semantics support retention.

Details: [collision](2026-10-17-precise-collision-results.md),
[tile shifting](2026-10-17-tile-shift-results.md),
[text](2026-10-17-text-traversal-results.md), and
[loading](2026-10-17-loading-investigation-results.md).

Across the topic replay matrix, 14 cases / 42 sampled frames pass repeated
captures, positive same-ELF RTL checks and corrupted-state rejection. Seven
source pairs pass 168 direct cross-repeat RGB555/PCM byte comparisons with 800
stereo pairs/frame. The two collision trace pairs also preserve VM/event digests;
native tile execution intentionally changes VM work. Desktop text/battle routes
add 48 raw pixel comparisons across both games and modes. Captured desktop routes
have the documented existing stb_ds UBSan diagnostic; focused fixtures have
separate sanitizer results.

## Final verification and build

Independent whole-branch review reports PASS with zero actionable findings.
On the final production tree, both games pass the precise collision (5,781
checks), collision-order, tile-shift (44 DR / one UT exclusion), string-read
(10,823 checks) and builtin-compatibility fixtures. These final fixture logs
have no ASan/UBSan diagnostics with the documented exclusions. The snapshot
suite passes, including restore, progress, audio and packaging checks.

Both native ELFs build from clean signed revision `a748749dd2baa47a1c0fd5f814fc9e362531611e`
with the paired GCC13.2 container and unchanged staged SDK. The native build
uses `SAVESTATES=1`; snapshot entry points, executeLoop at 0x4000 and fsync syscall 82
are verified in both binaries. Runtime remains pinned to `902a8adc` with the
externally rebuilt matching firmware/FPGA/bootloader pair. No new FPGA fit or
timing-closure claim is made.

| Native ELF | SHA-256 |
| --- | --- |
| undertale | `b9536dd6d0a52ac716a1c7798f6821a4fb595214a963d4d438a909f7603bccd7` |
| deltarune | `320b76ad780f8441554693470fa697024a9f9067abfca81a3a438cfb940a25e3` |

The local 23-file candidate update is
`out/pocket-sequence-a748749-20261017/butterscotch-sequence-a748749-20261017-update.zip`
(5806567 bytes), SHA-256
`46cc6919349bcaaca568e61cefb534e04c2b8d8d2e5ec9ee0ade7315f2bac777`. Memories and sleep remain enabled, including
the Deltarune Sleep Test instance. No game data or saves are included. ZIP CRC
and every member's complete bytes are verified against the assembled sources.

All 1,126 staged SDK files and both original data.win hashes remain unchanged.
Source commits carry valid GPG signatures (local trust is unknown). Exact
compiler image, commands, native hashes, runtime provenance, ZIP membership and
checks are in the private build-inputs/proof JSON beside the archive, plus
`final-regressions.json`, `final-integrity.json` and `final-review.md` in the
plan evidence directory. This final report commit changes documentation only;
the packaged source revision above remains the exact build identity.


Original assets and paired SDK/runtime are preserved. Completed host evidence
for tile/text lives on the external drive with original-path symlinks and full
byte/link relocation proofs. All source/capture/rejected-candidate evidence is
retained. No image pruning, VM reconfiguration, project cleanup, push or SD write
was performed.

Physical Pocket checks remain: normal save → Analogue Quit → reopen/Continue,
Memories capture/restore, sleep/wake, and scene performance/longer gameplay.
