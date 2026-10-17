# Layer lookup and VM argument results — 2026-10-16

Both optimizations are retained. In the three sampled Deltarune spade frames,
the combined implementation reduces simulated time by **5.16% in Accuracy**
and **11.44% in Speed**, with identical raw pixels and PCM in both repeated
captures. These are os25 simulation measurements, not Pocket timings.

The [implementation plan](../../../docs/superpowers/plans/2026-10-16-layer-lookup-and-vm-arguments.md)
follows the [measured opportunities](2026-10-16-next-cpu-opportunities.md) and
the [existing optimization design](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md).
Accuracy fidelity, game scripts, event order and snapshot format are preserved.

## Source identities and timing

| Source | Revision | Spades Accuracy mean | Saved versus baseline |
| --- | --- | ---: | ---: |
| Rebased baseline | `50e8379c6fd7caf90528a99deb0bafcab46fba24` | 174.275870 ms | — |
| VM arguments only | `a3814046a5c075fc2a8a9f27c3f86c3b05c0e835` | 172.627630 ms | 0.9458% |
| Layer lookup only | `86bb46642e0f1d84a1907d9196fc7fe816ddb21b` | 164.118807 ms | 5.8282% |
| Combined retained code | `5deec2e4b09d0d17a7b81ecbc7d098d149e3bcf0` | 165.280683 ms | 5.1615% |

The layer-only revision is a signed, private measurement commit with the baseline
as parent and only the four production layer files changed. The branch retains
separate VM and layer implementation commits. No branch was moved to construct
that isolated measurement.

Speed spades measures **94.349733 → 83.553483 ms**, a **11.4428%** reduction for
the combined code. Speed and Accuracy are separate configurations; their output
is compared only within the same mode. All timing values are means of frames
490–492 at simulated 100 MHz, from timing builds without VM trace instrumentation.
They do not establish whole-game averages or physical-device FPS.

The isolated Accuracy gains do **not** add together. Combined code is slower
than the layer-only sample. These measurements do not establish the cause of
that interaction, and the small VM-only result is not a physical-device claim.
Actual emitted-code checks independently establish the removed VM work.

## Retained implementation

`Runner_findLayerElementById` now remembers the last successful layer and element
indices. It validates both bounds and the current signed element ID before
recomputing pointers from the live arrays. A failed validation uses the original
ordered linear scan; misses clear the hint. This preserves first-match and
`outLayer` behavior, without a pointer cache or extra allocation.

Explicit invalidation covers room construction/replacement, persistent room
detach/reinstall, instance insertion/removal/depth changes, layer and element
creation/destruction/movement, cleanup and snapshot restore. Layer destruction
invalidates after destruction callbacks and immediately before removal, because
a callback can repopulate the hint. Future structural writers must also call the
invalidation helper: bounds and ID validation alone cannot detect insertion of
an earlier duplicate ID. The hint is derived state, excluded from snapshot wire
fields; restore clears it before installing the restored layer array.

The RV32 port-harness DWARF measures `sizeof(Runner)` at **13,832 → 13,840 bytes**,
a net **8-byte** increase. The new fields shift nearby members by 12 bytes and
consume four bytes of prior alignment padding. This size result is specific to
that measured profile; ordinary device ELFs do not expose a Runner type DIE.

The VM change removes only the two `stackArgs` zero initializers in `handleCall`
and `handleCallV`. Every live argument slot is assigned before dispatch, tracing
or cleanup. Zero arguments still pass null; overflow allocation, locals, growth,
ownership and return strengthening are unchanged. Actual baseline/final RV32
ELFs show one unconditional 128-byte clear becoming zero in every applicable
handler: CALL in WAD16, CALL and CALLV in WAD17. Overflow `calloc` calls remain,
and each game's baseline/final VM compiler arguments are identical.

## Focused verification

- Layer fixture: **47 common behavior checks** pass on baseline and candidate
  for each game; candidate adds two malformed-index checks for **49** total.
  Twenty repeated hits execute **5,100 → 0** linear-search loop bodies. Real
  builtin mutation paths, duplicate ordering, reallocation, instance/depth
  changes, persistent-room reinstall, callback repopulation and complete
  snapshot save/restore are exercised. Warm and invalid hint snapshots serialize
  identically under a fixed elapsed-time condition. The extended baseline fixture
  links the original baseline `runner.c` and a no-op invalidation helper against
  matching-layout objects; it is not a pristine baseline program. The gameplay
  comparisons below use clean committed source exports.
- VM fixture: baseline and candidate pass **51 WAD16 / 102 WAD17 cases**, including
  34 / 68 checked calls and nested events. Real CALL/CALLV bytecode covers
  0/1/16/17/33 arguments, builtin/script/unknown dispatch, reference values,
  nested event entry and weak-return strengthening.
- Both fixtures pass ASan/UBSan for both games, with alignment and function
  sanitizers excluded by the fixture build flags. Apple leak detection is
  disabled; SDL dependencies use the host library fallback path. These checks do
  not claim leak, alignment or function-sanitizer coverage.
- Final ordinary SDK RV32 ELF builds pass for both games. CMake Release warnings
  as errors, both strict port lints and GNU++98 syntax checks of `runner.c`,
  `vm.c` and `vm_builtins.c` pass. The snapshot codec is explicitly excluded from
  the generic/legacy CMake target; a direct GNU++98 probe fails with identical
  pre-existing errors on baseline and candidate. Its supported C builds and
  snapshot fixture pass.
- Separate VM, layer writer-coverage and combined code reviews found no
  actionable defects.

## Replay coverage and provenance

The bounded matrix uses four Accuracy spades timing ELFs (baseline, VM-only,
layer-only, combined), plus baseline/combined pairs for Speed spades timing,
Accuracy spades trace, Accuracy dodge trace and Undertale Flowey trace. Each
case requires two deterministic QEMU captures, positive same-ELF RTL replay
and rejection of an intentionally corrupt state oracle. Cross-source checks
compare nonempty raw RGB555 and PCM bytes across both repetitions, with
**800 stereo pairs per frame**. Traced pairs additionally compare the maintained
opcode/event trace fields; pointer-bearing state is compared only within an ELF.
Spades trace/timing pairs check outputs separately from the timing claims.
The combined Accuracy timing ELF also passes same-ELF RTL replay with scanout
traffic; the reported timing comparisons use quiet traffic.

All cases use the same reviewed private scene adapter, GCC 16.2, port profile,
64-byte alignment, native replacements enabled, BRAM, cached framebuffer,
clock cost 1680, fixed audio and quiet traffic. The spade recreation retains
room 43, nine active spades, 9/8/8 visible and zero battle controllers. Dodge
samples frames 3323–3325 and Flowey 2400–2402. The collector enforces complete
configuration equality except source identity, and except build kind for an
intentional trace/timing pair. Adapter and launcher hashes are included.

All **12 cases / 36 sampled frames** pass, including all **12 negative controls**.
Nine pair comparisons cover the isolated/combined screens, traced regressions
and spades trace/timing output agreement.

Private evidence is under `.superpowers/layer-vm-20261016/` in the implementation
worktree. The case index, aggregate validating report, pair summaries, exact
commands, source preflights, raw-channel hashes, compiler/disassembly records and
final ELF hashes are retained there. The exact index is `harness/case-index.json`;
`harness/validated-results.json` has SHA-256
`aae9a07a62aeb316377e0916a8f51a807789964000433f97d8d3ac60023a6e79`.
Cases live in the existing isolated harness
workflow `experiments/workflows/spatial-grid-compat-20261016-r1/experiments/cases/`.
Game assets, raw captures and generated binaries are not committed to the repo.

Early private `--patch` exports silently skipped the intended changes when
applied from the nested workflow directory. Those attempted VM/layer candidate
runs are explicitly invalid and excluded from all results above. Their evidence
is preserved in `invalid-patch-runs.md`. Replacement snapshots export actual
signed Git commits with no patch overlay. Before building and again during
comparison, five relevant production source files are checked byte for byte
against their recorded Git revision.

## Build artifacts and limits

| Final ordinary RV32 ELF | Bytes | SHA-256 |
| --- | ---: | --- |
| Deltarune | 1,859,476 | `bb78cc0cde3e7c44a0c8498248b9f2fd8570c81c1b46c15ce06e23a0b83a88b6` |
| Undertale | 1,823,312 | `5d0fa77319ca69e5f50f3586dee1ce8ba657d75ed9259ec519c9c7e108ea0d9a` |

Stable private copies are `final-deltarune.elf` and `final-undertale.elf` in the
evidence directory. These are program builds, not regenerated SD-card bundles.
This coverage is a focused regression/performance sample, not the full replay
matrix or whole-game testing. **Physical Pocket behavior and timing remain
untested.** No card writes, deployment, push or PR were performed.
