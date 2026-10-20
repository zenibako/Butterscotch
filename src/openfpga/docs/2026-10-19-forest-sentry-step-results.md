# Guarded active Forest sentry Step

Implements [Pocket issue #6](https://github.com/zenibako/butterscotch-pocket/issues/6)
on merged core `b385b300592b03f919b9e46b4b098bbed90a8be6`, following the
[optimization constraints](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md)
and [sentry scene/profile](2026-10-19-forest-sentry-results.md).

## Scope and behavior

The Deltarune event registration requires length 2236 and hash
`2e65f505e4e7b03e`. The native handles finite, bounded active-state values with
an already-created modern bullet array of at most 16 entries. Creation,
warm-up, incompatible bytecode, modified epsilon, missing builtins and
unsupported values retain interpretation. All rejection checks precede the
first script-visible write; execution never falls back after mutation starts.

The player-range check, numeric value types, separate arithmetic rounding,
four sine calls per live bullet, builtin order, array-owner tokens and spatial
dirty setters are preserved. The boss's `with` traversal retains forward order
for one/two active statues and reverse order for larger groups. No sine reuse,
changed collision logic, renderer shortcut, snapshot field, eligibility rule,
SDK or runtime change is included. Cached variable IDs consume 77 bytes before
alignment and reset on VM replacement; there is no new pixel/audio cache.

The real-game room-76 profile reproduces **4,297 interpreted instructions/frame**
across four stable sentry Steps on the parent. The candidate records **zero**
interpreted instructions for those same four stable events. Startup still
interprets. Counts establish removed work, not elapsed Pocket time.

## Correctness and builds

- The real-bytecode fixture passes **28 cases** normally and under ASan/UBSan
  with alignment/function sanitizers excluded and leak detection disabled.
  It compares ordered builtin arguments/results and owner-at-call state,
  variable value types/bits, bullet properties, spatial dirty order, RNG,
  alarms, array aliases and final owner tokens over consecutive Steps.
  Positive/negative/zero rotation, fractional values/counts, inactive/missing
  and repeated bullets, multiple players/statues, disabled/wrong-fingerprint
  natives, unsupported-state rejection and shifted variable IDs in a fresh VM
  are covered. Supported cases require zero interpreter entries. The original
  19-case characterization passed on the parent while its work gate failed
  18 cases, before native implementation.
- Test-first comparison caught integer-literal store coercion and a fused
  multiply/add changing one alpha bit. The implementation preserves REAL loop
  stores and disables contraction within this function for Clang/GCC. The
  production GCC13 RV32 assembly contains no fused multiply/add instructions.
- Eight final sentry captures match the parent in pixels and canonicalized
  semantic state: room 76 frames 44/100/140 and room 89 frame 190 in Accuracy
  and Speed. Active scenes retain 32/11 bullets. Only timestamps and opaque
  array addresses are canonicalized; alias relationships remain compared.
- A private comparison adapter supplies a fixed 1/30-second audio update to
  both builds. Baseline/candidate/repeated-candidate runs match **720 distinct
  captured game frames** across both rooms/modes, plus four duplicate final
  screenshots. All **4,684,800 PCM bytes / 1,171,200 stereo pairs** agree per
  complete four-configuration run. Production audio source is unchanged;
  this is deterministic regression evidence, not live audio scheduling timing.
- Baseline Memories captured at frame 100 restore in fresh candidate processes
  at frame 40 and match uninterrupted frame 190 in both modes, with production
  policy bypass unset. The sleep-fixture route uses globals at 34 and room-76
  jump at 35 to avoid its initial yard jump.
- The complete staged-SDK snapshot suite and platform lint pass. Undertale's
  fixture confirms the Deltarune registration is excluded. Both native ELFs
  build with pinned GCC13.2.0, Memories enabled and focus diagnostics disabled;
  snapshot symbols, `executeLoop` at `0x4000`, and the existing fsync syscall
  path remain. Existing unrelated native-build warnings are retained in logs.
- Independent source and benchmark reviews found two ordering gaps (owner
  changes before sine calls and multi-statue traversal); both were corrected
  before final validation. Final review reports no remaining blocker.

## Isolated os25 benchmark

A new private event fixture runs the actual resolved 2236-byte Step and the
production native header through `VM_executeCode`, using four real builtins.
Metadata/operand indices come from the supplied game. Its minimal VM setup uses
the parsed detected format 2022.8.0.0, asserts modern arrays, and requires native
acceptance outside timing for every tested state. Setup/reset and semantic
hashing are outside the timed intervals. No renderer, audio or OS I/O executes.

One ELF brackets three native event executions with two interpreted batches
from identical initial state. Both paths are warmed first. The same binary and
addresses control source-layout differences. The three states cover eight
bullets with positive rotation/boss range checks, eleven with negative rotation,
and eight with zero rotation/no boss check. These are representative fixture
states, not complete captured game frames.

Quiet os25 at 100 MHz, milliseconds per event; baseline is the bracket mean:

| State | Interpreted | Native | Reduction |
| --- | ---: | ---: | ---: |
| Eight bullets, positive rotation | 4.929 | 2.711 | 45.01% |
| Eleven bullets, negative rotation | 6.917 | 3.847 | 44.38% |
| Eight bullets, zero rotation | 4.287 | 2.313 | 46.04% |

Two independent quiet replays reproduce every cycle/state row exactly. A
diagnostic scanout replay preserves state and reduces event time by 48.7–49.1%.
All SDRAM/AXI memory oracles pass. Deliberately corrupting the comparison hash
is rejected; this checks comparison plumbing rather than injecting a native
implementation defect. Native acceptance is verified separately.

The simulator, startup and musl inputs match 221 retained input hashes; the
maintained harness's 1,631 frozen inputs also verify. Compilation uses pinned
GCC13.2.0, RV32IMAFC with Zicsr/Zifencei, `-O2`, 64-byte SDRAM function/loop
alignment, and the production fast-RAM attribute. `executeLoop` occupies 7,508
bytes at `0x4000`; profiling and function instrumentation are off. The isolated
fixture uses a bounded first-fit allocator rather than the production allocator.
Interpreter event/with-frame allocations occur inside timing, so allocator
costs limit extrapolation. The three-event batches also retain CPU cache warmth.

These are **simulated isolated-event savings, not whole-room or physical Pocket
FPS**. Actual allocator behavior, OS/SD services, presentation/audio scheduling,
interrupts, fitted hardware, Memories and sleep/wake still need Pocket testing.
No update ZIP, SD installation or release publication is produced here.

## Private evidence

Reproduction scripts, source snapshots, original failed attempts, captures,
fixed-clock audio, fixture/build logs, native assembly and simulator manifests
remain ignored under `out/forest-sentry-step-20261019/`. Final evidence uses
`normal-final-fixture.log`, `asan-final-fixture.log`, `candidate-final-*`,
`fixed-comparison.json`, `restore-final/`, `device-builds.json`, and
`rtl/results.json`. The final benchmark includes a correction to its manual VM
format initialization and an allocator compiler-recursion fix; earlier runs
are not performance evidence. Commercial assets/bytecode and raw logs remain
private. Existing projects, original inputs, Docker images and VM settings were
preserved.
