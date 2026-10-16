# String read optimization: host evidence, 2026-10-15

Status: host workload, scene validation and bounded integrated replay matrix
complete. Retain the local candidate; physical Pocket benefit is unmeasured.

## Revisions and scope

- Actual parent: `f9d5442a315972456ceae7623d57ff947f896d9e`.
- Reviewed candidate: `8540044dc32cdc3101b4fd5ec7ec9a0a5b334956`.
- This topic borrows existing strings in `string_copy`, `string_char_at`, and
  `string_ord_at`. Non-string conversions and result ownership retain their
  original allocation contracts. No persistent production counters or APIs were
  added. This candidate's parent contains planning, not the independent audio
  optimization; no audio change was folded into this comparison.

The [shared design](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md)
and [string plan](../../../docs/superpowers/plans/2026-10-15-deltarune-string-reads.md)
require separate evidence for allocations, workload relevance, and simulated or
device cost. The existing build/verification/performance skills governed the
host runs. Task 1's independent specification and quality reviews passed before
these measurements.

## Allocation evidence and actual workload

Task 1's real-builtin fixture passed 148 calls per game under ASan/UBSan. Its
existing-string probes changed from one input `strdup` per call to zero; numeric
conversion probes remain at one. Original allocation assertions failed on 92
unnecessary-copy cases per game, then passed on the candidate. These fixture
results establish removed allocation work, not a frame-time improvement.

A temporary candidate-only diagnostic counted builtin entries, actual input
acquisitions, existing-string inputs, input byte lengths, and per-frame totals.
It prints counters only at process exit. It adds no clock reads. Frame rows
contain the runner frame and room index; length histograms preserve exact lengths
through 4096 bytes (4097 is the overflow bin, unused here). No string content is
logged. All measured acquisitions below were existing strings, with no skipped
inputs or non-string conversions. Byte totals exclude the terminating NUL.

| Complete benchmark | Builtin | Calls / removed input allocations | Input bytes | Input length range |
|---|---|---:|---:|---:|
| Deltarune | `string_copy` | 1 | 2 | 2–2 |
| Deltarune | `string_char_at` | 37,833 | 1,225,989 | 4–43 |
| Deltarune | `string_ord_at` | 0 | 0 | not reached |
| Undertale | `string_copy` | 16,507 | 338,707 | 4–76 |
| Undertale | `string_char_at` | 96,577 | 5,692,864 | 1–76 |
| Undertale | `string_ord_at` | 0 | 0 | not reached |

Each complete route ran in Accuracy (`--bench`) and Speed (`--bench-smooth`),
with native stand-ins enabled and with `BS_NO_NATIVE=1`: eight completed runs.
Counts, input bytes, exact length histograms, and active-frame ranges agree
across all four settings for each game. Deltarune's one `string_copy` occurs at
runner frame 0; `string_char_at` is reached on 2025 runner frames, from 1 through
3555. Undertale reaches `string_copy` on 296 runner frames (0–2594), and
`string_char_at` on 1539 (6–2606). `string_ord_at` is absent in every sample;
its gameplay benefit is unmeasured.

The measured full-route allocation counts imply 37,834 avoided input allocations
in Deltarune and 113,084 in Undertale. Including NUL bytes, their old input copies
would have copied 1,263,825 and 6,144,655 bytes respectively. This follows from
Task 1's one-copy-per-existing-string result and the observed acquisition counts;
it is not an allocator trace or a measurement of all allocations in the game.
Required result allocations remain. The largest measured input temporary avoided
is 44 bytes in Deltarune and 77 in Undertale. The production change adds no
persistent heap storage; total runtime memory use and RV32 stack cost were not
measured here.

## Dialogue and menu routes

Clean parent and candidate captures used private read-only asset links, fresh
run directories, seed 7, volatile progress (`--bench`), the existing scripted
routes, `BS_UNCAPPED=1`, `BS_NOFLIP=1`, and SDL dummy video/audio. All comparisons
used the repository's `ppmtool.py cmp` on P6 pixels derived from RGB555; each
5-bit channel maps injectively to its 8-bit PPM channel.

| Scene | Presented-frame target | Picture | Parent/candidate, native on | Parent/candidate, native off |
|---|---:|---|---|---|
| Deltarune opening text, “TRULY EXCELLENT.” | 1000 | 320×240 | identical | identical |
| Deltarune battle menu, Fight/Act icons and Rudinn targets | 3040 | 640×480 | identical | identical |
| Undertale Flowey, “Howdy!” | 1300 | 320×240 | identical | identical |
| Undertale first-room ITEM/STAT menu | 930 | 320×240 | identical | identical |

The Undertale menu uses `naming.script` plus `900:C`, overriding only the bench
input script. The other captures use the built-in bench route. Each scene was
visually inspected. Earlier exploratory Deltarune captures at 2960, 3010, 3020,
and 3100 were field/attack scenes and are not counted as menu coverage.

Four additional candidate native-on/off pairs, eight diagnostic/clean pairs,
and one same-candidate Flowey repeat are also pixel-identical: 21 comparison
pairs in total. The diagnostic/clean checks cover the same frame, route, game,
and native setting and show no pixel disturbance from the counters.

Separate diagnostic runs through the capture checkpoints give these cumulative
counts from boot (both native settings agree). They are not counts for only the
single captured frame:

| Checkpoint | `string_copy` calls / bytes | `string_char_at` calls / bytes | `string_ord_at` |
| Deltarune text | 1 / 2 | 13,901 / 452,859 | 0 |
| Deltarune menu | 1 / 2 | 34,028 / 1,103,164 | 0 |
| Undertale dialogue | 16,054 / 318,705 | 13,095 / 808,603 | 0 |
| Undertale menu | 15,849 / 305,581 | 745 / 30,553 | 0 |

The counter's `runner->frameCount` differs from the presentation dump counter.
At these presented targets, the latest changed-builtin counter rows are runner
frames 1002, 3044, 1306, and 935 respectively. Preserve these identities when
reading the logs; they are not directly the device harness's replay windows.

## State, PCM, and sanitizer limits

Raw `BS_DUMP_STATE` text is preserved. It is not byte-identical across parent
and candidate: log timestamps, array addresses, and global hash-table iteration
order vary. A supplementary comparison strips timestamp prefixes, replaces only
`<array:0x…>` addresses, and compares line multisets. Deltarune's multisets match
at both scenes and both native settings. Undertale's only remaining line
mismatch is `idle_time`. A same-candidate Flowey repeat also changes `idle_time`
(182.51 versus 155.54), while the pixels remain identical. This diagnostic is
not a structural serialized-state comparison and does not prove complete state
identity, pointer relationships, event ordering, or save-state correctness.
No volatile progress archive was used as a cross-revision oracle.

Desktop PCM was not captured or compared. Uncapped host audio consumption depends
on wall time. Fixed-audio PCM and accepted-pair equivalence must be established
by the coordinator's QEMU/RTL workflow. Desktop elapsed times from these builds
are not reported as performance results.

Host compiler: Apple clang 21.0.0, arm64-apple-darwin27.0.0. All six accepted
incremental parent/candidate/diagnostic builds and two restoration builds
completed without compiler warnings or errors; they reuse Task 1's ASan/UBSan
objects with `-g -O1 -fsanitize=address,undefined
-fno-sanitize=alignment,function`. This is not a fresh whole-project warning
baseline. Task 1's fresh Undertale build recorded the existing vendored
`stb_image_write.h:776` deprecated-`sprintf` warning.

Deltarune parent, candidate, and diagnostic runs emit the known vendored
`stb_ds.h:1132` UBSan warning for left-shifting 255 by 24 in a signed int. The
verification skill already lists this finding. All processes exit 0, and the
accepted run logs contain no other UBSan runtime-error location or ASan error.
Leak detection is disabled (`ASAN_OPTIONS=detect_leaks=0`). These runs therefore
are not described as completely sanitizer-clean.

## Build controls, cleanup, and reproduction

Worktree: `/Users/chanderson/Projects/undertale-pocket/butterscotch-opt-strings`.
Private evidence root:
`/Users/chanderson/Projects/undertale-pocket/butterscotch-pr20-fixes/.superpowers/sdd/2026-10-15-deltarune-string-reads/task-2-logs`.
Accepted evidence is in its `v2/` directory; scripts are in the root.

The first rapid source-swap build attempt silently reused stale objects for two
variants. Compile logs and `nm` exposed this before acceptance. Every first-attempt
build, counter run, and comparison is excluded, even where its output happened
to match. Their logs remain available. The replacement builds pass
`make -W <absolute-path-to-src/vm_builtins.c>` and each accepted log contains one
explicit compile of that source before linking. `nm` confirms diagnostic hooks
in both diagnostic executables and their absence in the clean restored builds.

`prepare-builds-v2.py` retains exact candidate/parent/diagnostic before-copies and
`v2/diagnostic.patch`, compiles six binaries, and restores its own source bytes in
a `finally` block. `restore-objects.py` verifies the restored source against
`git show 8540044:src/vm_builtins.c`, then forcibly recompiles both reused object
directories without diagnostics. No reset/checkout, SDK edit, source edit in
another worktree, asset modification, Docker, or RTL command occurred here.

Reproduction, with the authorized private inputs in place, from this worktree:

```sh
EVIDENCE=/Users/chanderson/Projects/undertale-pocket/butterscotch-pr20-fixes/.superpowers/sdd/2026-10-15-deltarune-string-reads/task-2-logs
python3 "$EVIDENCE/prepare-builds-v2.py"
python3 "$EVIDENCE/run-desktop-v2.py" counters
python3 "$EVIDENCE/run-desktop-v2.py" captures
python3 "$EVIDENCE/diagnostic-scenes.py"
# The one same-candidate repeat is described in the private task report.
python3 "$EVIDENCE/analyze.py"
python3 "$EVIDENCE/restore-objects.py"
```

The run scripts deliberately require new directories (`exist_ok=False`); archive
or select a fresh evidence root before reproducing. `analyze.py` also expects the
repeat directory. Each launch uses a 150-second watchdog. All assets are links
to the pinned harness input tree. The private full report includes exact direct
launch and repeat commands. Logs, raw pixels, text dumps, diffs, histograms,
before-copies, and binaries remain private and are not included in Git.

Candidate source SHA-256 after restoration:
`4f7d48f03fb2e3809ad95399e6bab745d7105cd09c7f32bbe685c4a4f68adc39`.

Accepted capture/diagnostic executable SHA-256 values:

| Binary | SHA-256 |
|---|---|
| `task2-v2-dr-parent` | `1f97871633479ad3caf7c11430628c45dba708c50baae319924755707e1c23ff` |
| `task2-v2-dr-candidate` | `354aae43fe58e892bd443b0bb6716925ee41503e29fc71b231afcf9f438d81dc` |
| `task2-v2-dr-diagnostic` | `9a4c5eb148489bd934b600836e6e5a162ceaca625496adb439c24ca84732bd5f` |
| `task2-v2-ut-parent` | `40dae25128cd3c845823f7cb1cdef75dd63dc8d65859a687a485a5e977eeebf3` |
| `task2-v2-ut-candidate` | `7834c2e346fba3b03a643b3ced833db4ae93894e7a705cec717e06685b95d75b` |
| `task2-v2-ut-diagnostic` | `383c542aad20c192b7cd25bd2fcb5e07624542d1e6d96e06a272dca64ae72410` |

## Harness timing and retain/reject decision

The isolated string candidate `8540044` passes the Deltarune dodge Accuracy
screen against source-equivalent baseline `a6ea5c0`: raw RGB555, PCM and pair
counts match; repeated captures and same-ELF replay pass; the corrupted-state
control is rejected. A direct comparison passes all 12 pixel/PCM files across
two captures and three frames. Mean simulated frame time changes from
108.289930 to 108.705197 ms (+0.383%). This small difference is inconclusive.
Exact cases under the private harness's `experiments/cases/`:

```text
opt-main-a6ea5c0-20261015-deltarune-dodge-accuracy-timing-0b2a2747dd136fbd
opt-strings-8540044-r1-deltarune-dodge-accuracy-timing-2b0fb44070416f5e
```

The validating comparison/report files are
`build/optimization-discovery-20261015/strings-dodge-accuracy-comparison.json`
and `strings-dodge-accuracy-results.json`.

The integrated `2ee841c` candidate then passes the shared 32-ELF matrix across
both games, modes and trace/timing builds, all negative controls, raw RGB555/
PCM/pair comparisons, and eight exact timing-ELF diagnostic scanout replays.
Timing RTL excludes its in-loop PCM hash; trace RTL checks it and both build
kinds have compared raw QEMU PCM. These full-matrix timings describe the bundle,
not an independent string-only gain. The [combined results](2026-10-15-deltarune-optimization-results.md)
record exact cases through the hashed inventory, per-window results, memory,
same-source timing variation, interruptions and remaining device limits.

Retain the local borrowed-input candidate: it removes measured allocations in
real text/menu routes and passes independent ownership/behavior fixtures and
the shared compatibility gate. No gameplay speedup is inferred from synthetic
counts, and the isolated timing difference establishes no benefit. A physical
Pocket base/candidate/base comparison remains necessary before claiming device
improvement. Deployment is outside this work.
