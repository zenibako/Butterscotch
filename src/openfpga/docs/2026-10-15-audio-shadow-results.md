# Audio shadow copying evidence, 2026-10-15

The accepted-output shadow now copies stereo PCM in contiguous ring spans.
The ordinary path computes a 64-bit remainder once per accepted chunk; the
previous path computed it for every pair. Snapshot format, decoder/mixer
arithmetic, queue acceptance checks and the 64-bit accepted-pair total are
unchanged. The original scalar path remains for the counter-overflow boundary,
where an arbitrary non-power-of-two capacity would otherwise change indexing.

## Exact source and configuration

- Actual implementation parent: `f9d5442a315972456ceae7623d57ff947f896d9e`.
- Independent candidate: `b596d43cc24f00d0d8e3eb5efce32a634631798b`.
- Integrated equivalent: `f21750c`; the audio source/test content is unchanged.
- Screen baseline: `a6ea5c0532298a14e896a719f3bbb1461ca39141`. The parent
  adds planning documents only: `git diff a6ea5c0 f9d5442 -- src vendor` is empty.
  The baseline retains its original revision label.

The screen uses Deltarune Chapter 1 dodge frames 3323–3325, Accuracy, timing
build, GCC 16.2.0, port flags, 64-byte function/loop alignment, native replacements
enabled, BRAM interpreter, cached framebuffer, modeled 1,680-cycle clock reads,
fixed 800-pair audio consumption, and quiet traffic. All timings below are
**simulated at 100 MHz**, not measured on a Pocket.

## First isolated screen

| Frame | Baseline cycles | Audio candidate cycles | Difference |
|---|---:|---:|---:|
| 3323 | 10,419,810 | 10,368,619 | -51,191 |
| 3324 | 11,817,712 | 11,788,408 | -29,304 |
| 3325 | 10,249,457 | 10,211,699 | -37,758 |
| Mean simulated ms/frame | 108.289930 | 107.895753 | -0.394177 (-0.364%) |

All three candidate frames pass the same-ELF RTL state/pixel/memory oracles.
Both QEMU captures agree. Cross-revision comparison agrees on raw RGB555,
PCM and pair counts; an additional direct byte comparison passes for all
12 raw channel files (two repeats × three frames × pixels/PCM). The candidate's
corrupted-state control rejects the deliberately wrong first-frame oracle.
Pointer-bearing state is not compared across binaries.

The small timing difference does not establish a device speedup. It lies within
the project's documented sensitivity to code layout. The removed per-pair
software remainder is established independently by source and actual RV32
disassembly. For one accepted 800-pair batch split as 256/256/256/32, the normal
algorithm needs four index remainders instead of 800; this operation count is
not a frame-rate prediction.

## Compiler, memory and semantic checks

The baseline actual ELF calls `__umoddi3` inside the per-pair shadow loop.
GCC inlines the candidate helper into `mixAndWrite`. Its disassembly has one
remainder before the normal span-copy loop and a separate remainder site in the
rare scalar overflow branch. The latter must remain for exact uint64 wrap
semantics. No new persistent cursor, heap buffer or serialized field was added.

GNU size reports identical totals for these exact two timing ELFs: text
1,331,184 bytes, data 41,004 bytes, BSS 969,976 bytes. This is whole-ELF section
size evidence for the screen, not a measurement of production peak heap use.

ASan/UBSan tests compare 210 appends against an independent scalar oracle across
capacities 1/3/7/16/131072, untouched cells, asymmetric stereo channels, zero
appends, multiple wraps, totals above UINT32_MAX, and UINT64_MAX crossings.
Twenty mixer acceptance cases cover full/partial/zero/negative/oversized backend
returns, and restore-then-append is checked. A temporary mutant that corrupts
one right-channel bit is rejected. The snapshot suite and both games' port
lint pass. Independent specification and quality review reports no findings.

## Private evidence and further validation

`$HARNESS_ROOT` denotes the verified private os25 harness checkout. Exact
machine-specific locations remain in the private case manifests. The following
paths are relative to that root; exact cases are under `experiments/cases/`:

```text
opt-main-a6ea5c0-20261015-deltarune-dodge-accuracy-timing-0b2a2747dd136fbd
opt-audio-b596d43-r1-deltarune-dodge-accuracy-timing-cad4bdb64834b508
```

Validating comparison and report outputs are
`build/optimization-discovery-20261015/audio-dodge-accuracy-comparison.json`
and `audio-dodge-accuracy-results-validated.json`. The latter was regenerated
after both negative controls completed; the earlier `audio-dodge-accuracy-results.json`
is retained but predates the candidate's negative result. Actual candidate disassembly is
`audio-candidate-mixAndWrite.disassembly.txt` in the same directory; baseline
disassembly is `build/audio-shadow-main-a6ea5c0.disassembly.txt`.

The shared Accuracy/Speed, trace/timing, two-game matrix passes on integrated
code revision `2ee841c`: 32 selected executables, repeated captures, 32 rejected
negative controls, exact raw-channel comparisons and eight timing-ELF diagnostic
scanout replays. It also contains string borrowing and macro-off collision
diagnostics. Its timing results describe the bundle and cannot be relabeled as
independent audio measurements. Timing RTL excludes the in-loop PCM-hash field;
trace RTL checks it, and raw QEMU PCM is compared for both build kinds.

Retain the local span-copy candidate: the independent scalar oracle, actual
compiler work reduction and combined compatibility matrix support its limited
complexity. The isolated -0.364% screen does not establish a Pocket speedup.
The [combined results](2026-10-15-deltarune-optimization-results.md) record exact
inventory/evidence hashes, the same-source timing control, memory observations,
contention results and device limits. Normal/restored queued-audio semantics
are checked deterministically by the snapshot suite; whole-game desktop PCM
is wall-clock-driven, as documented in the save-state verification guide.
Physical storage, interrupts, live audio/display scheduling and Pocket
base/candidate/base testing remain separate. No core or save file was deployed.
