# Text-voice silent-tail investigation

Status: a promising **private feasibility prototype**, not a retained mixer change.
Baseline source is `09527f99e6334f717da813304b09790e36ccf785`. No production mixer code,
pack, save format, SDK or deployed core changed during this investigation.

The [physical Pocket results](2026-10-18-pocket-battle-text-results.md) isolate
most extra observation-reveal work to audio processing. The existing
[audio-shadow result](2026-10-15-audio-shadow-results.md) optimizes accepted-PCM
copying, while the new profile points inside voice processing. Existing decisions
require measured work removal and preserved output/state before retaining a change.

## Cause and candidate

Both focused battle strings use SOND 13 `snd_text`, gain 1, effective pitch 1.
The writer requests another independent nonlooping voice per eligible letter;
spaces and control/punctuation paths differ. The packed track 170 contains
54,856 source samples at 32 kHz (1.71425 seconds). Its last nonzero sample is 1857:
**52,998 trailing samples, or 1.656 seconds, decode to exact zero**. The original
embedded WAV has the same silent padding; it was not introduced by the packer.

The current mixer still decodes, resamples, updates gain and accumulates those
zero samples independently for every active voice. Preserve their original
lifetime: playing/position queries, stealing order and Memories observe it.

The recommended first optimization is chunk advancement through a proven silent
suffix. Scan a cached sound once for trailing bytes whose nibbles are each 0 or 8.
When predictor/index and both interpolation endpoints are zero, those codes keep
them zero. Advance decoder cursor, pending nibble, fractional phase and gain ramp
in bulk, with zero contribution to the mix. Keep the original path for streaming,
looping, unknown/mismatched metadata, nonzero state and track-end transitions.
Metadata must be bound to the current cached allocation and track/length. The
private guarded copies check the exact cached-data pointer and use UINT32_MAX
for unknown metadata. This is content-based; no sound-name special case is needed.

## Work removed in deterministic route models

Uncapped desktop deltaTime follows wall time even with BS_AUDIO_DUMP. It cannot
stand in for device concurrency. Both private reference and candidate therefore
substitute the same explicit audio delta, including group-gain updates. Input,
room and sentence guards are otherwise the existing focused route. These are
sensitivity models, **not physical Pocket voice counts, RTL timings or FPS**.

| Observation reveal model | Peak text voices | Text voice output pairs | Bulk pairs | Scalar source decodes, reference → candidate |
| --- | ---: | ---: | ---: | ---: |
| 30 Hz / 1,600 output pairs per input frame |15|481,600|410,880 (85.32%)|321,059 →47,146|
| 100 ms / 4,800 output pairs per input frame |15|1,432,458|1,361,536 (95.05%)|954,952 →47,273|

The 30 Hz reference counts 413,064 zero-tail text-voice pairs (85.77%); conservative
chunk/end guards deliberately retain a little scalar work. Encounter reveal
similarly avoids 87.18% in that model. No other track takes the bulk path in this
route. The models support a large removable workload, not an 85–95% frame speedup.
The physical queue's time/cost feedback still requires a before/after Pocket run.

## Equivalence evidence

- Guarded reference/candidate routes PASS in both models. Direct byte comparison
  agrees for the entire 8,672,000-byte PCM at 30 Hz and 26,016,000-byte PCM at 100 ms,
  plus all 13 captured images per model. The 30 Hz guarded outputs also agree with
  the earlier scalar baseline and instrumentation-only control.
- Pointer-normalized voice fingerprints agree: one cumulative 64-bit fingerprint
  over all 1,355 input boundaries, plus seven accepted-window sequence fingerprints.
  This is hash evidence, not 1,355 independent byte comparisons.
- A separate ASan/UBSan fixture directly compares PCM and every OfVoice byte
  after 73,643 chunks / 7,054,978 requested pairs. It covers three real text sounds,
  variable phase/pitch/chunk sizes, gain ramps, pending nibble parity, track ends,
  and streaming/looping/starvation fallback. Of those pairs, 1,572,820 exercise
  the bulk path; the rest exercise scalar/fallback behavior.
- The work-bound test fails with the scalar-only stub. Deliberate gain-ramp,
  pending-code and deferred-phase mutations are each rejected. Full-game runs
  retain the known vendored stb_ds signed-shift UBSan diagnostic; they are not
  claimed globally sanitizer-clean. Alignment/function sanitizer checks were
  disabled as in the existing desktop fixture configuration.
- Shipping GCC 13.2 / rv32imafc helper assembly has hardware mul/mulh and no software
  integer divide/multiply helper calls. Its only call site is the scalar fallback.
  This is compiler-lowering evidence, not target cycle or whole-program timing.
- Independent review found no blocker in the guarded span/state arithmetic.
  It identified the missing cache-pointer guard in an earlier private copy; new
  guarded copies add it and repeat both models. Production lifecycle remains open.

PCM SHA256: 30 Hz `54e5d759a8e40524adf78ff599a3bc0b2d8db79e20b73ca28343a7687df3ffef`;
100 ms `e0d346dbfce016bc8696adcfd5cb141728fd79a57dc849550bcc4b72c7fa75bc`.
Private reproducible artifacts are under `.superpowers/audio-voice-discovery-20261018/`
and `.superpowers/audio-voices-20261018/`; game assets remain private.

## Before retaining the optimization

Implement derived suffix metadata in `platform/of_audio_system.c` with safe
fallback on allocation failure or unknown metadata. Cover cache load, eviction,
reload and snapshot prepare/apply (which can replace cached allocations). Keep
metadata derived rather than changing the serialized format. Add focused tests
for cache pressure, restore, seek, pause, group-gain changes and each guard edge;
run the existing audio/snapshot suite with the actual dispatch enabled. The
private prototype does not establish those production lifecycle contracts.

Then verify both games' affected audio behavior and build the paired native
ELFs. A focused Pocket before/after measurement must establish actual benefit;
no prediction in milliseconds is warranted yet. Baseline battle rendering remains
another cost, as demonstrated by the audio-disabled physical measurements.

Stable/unity-gain specialization is a smaller second candidate. Decoded-PCM
caching could remove repeated decoding more generally but costs 4× compressed
storage and adds snapshot/seek complexity. The silent-tail path is the narrower
measured opportunity to pursue first. Existing ADPCM local-state and lookup
optimizations are already present; repeating them offers no new work removal.
