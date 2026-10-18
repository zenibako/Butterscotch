# Physical Pocket: focused battle-text measurements

The returned `deltarune_focus_1.sav` contains five complete unique PASS reports
from source `4388a476ab538ff208bc6dc948b754868bc07216`, Speed mode, uncapped,
seed 7, OS 0.9.0, core 0, reported CPU 100 MHz. Attachment SHA256:
`15e128f190d46c63b4529e7a9d8537cfe9e25fe5541932a9f8f3378126763118`. The file is 105,437 bytes including the terminating NUL.
No user save or binary is committed here. Parsed reports and the numerical check
are retained privately under `.superpowers/focus-pocket-results-20261018/`.

All modes accepted the same semantic sample counts: 29 observation-reveal and 50
observation-full frames (38/30 for initial encounter). These are correlated frame
samples from one run per mode, not independent repeated trials. Headline values
below are mean work in milliseconds per frame, excluding same-frame flip time.

| Observation text | Normal | No rendering | No audio | Neither |
| --- | ---: | ---: | ---: | ---: |
| Revealing |100.131|49.250|54.151|25.288|
| Complete |59.720|26.697|53.173|23.616|
| Reveal minus complete |40.411|22.553|0.978|1.672|

Disabling audio reduces reveal work by 45.980 ms (45.9%) but complete-text work by
6.547 ms (11.0%). Almost all of the measured reveal penalty disappears with audio
disabled. The initial encounter repeats the pattern: 107.646/61.223 ms normally,
56.279/54.195 ms without audio. This strongly identifies audio processing as the
main contributor to the extra slowdown while letters appear. These counterfactual
modes are not additive components and do not isolate individual mixer operations.

## Profile attribution

The instrumented observation windows report:

| Counter | Revealing | Complete |
| --- | ---: | ---: |
| Voice processing, ms/frame |44.347|3.610|
| Whole mix, ms/frame |49.936|7.519|
| Output write/shadow copy, ms/frame |2.116|1.472|
| Output sample pairs/frame |6,715|4,568|
| Sprite raster total, ms/frame |21.766|20.332|

Voice-loop time grows 12.3× while generated output grows 1.47×. Output volume alone
therefore does not explain the increase. No effect-loading time is reported in
either observation window. Repeated effect SD reads are not the leading evidence.

`platform/of_audio_system.c#mixAndWrite` times the active-voice scan and calls to
`mixVoice` under OF_LOAD_MIX_VOICES. That includes ADPCM decoding, resampling,
gain/group gain and accumulation. OF_LOAD_MIX_WRITE includes both `of_audio_write`
and accepted-PCM shadow copying. Short sounds cache compressed data, while each
active voice still performs decoding/mixing. `ofPlaySound` permits distinct voices
to overlap, but this log does not measure actual overlap counts or prove a bug.

The GML profiler's 42.45 ms `draw_sprite` entry is not 42.45 ms of raster work.
`sw/sw_renderer.c#SWR_PROFILED` finishes raster timing before calling
`platformDrawProfile`; `platform/of_perf.c#platformDrawProfile` then pumps audio.
The VM builtin timer includes that pump. The separate raster counters above are
more appropriate for comparing sprite drawing. Profile instrumentation also
slows execution; its totals are not the uninstrumented baseline.

## Next investigation

Measure active/peak voices, voice-pairs/output-pairs, sound IDs, pitch and per-sound
processing during text reveal. OF_LOAD_MIX_VOICE_PAIRS is already accumulated but
was omitted from this report. This can distinguish concurrent letter-sound voices
from different decode/resampling workloads. Preserve intended sound and voice
semantics; the current evidence does not justify cutting polyphony or muting text.

The output queue follows wall-clock consumption, so slow frames request more
audio work and can amplify CPU cost. Previous fixed 800-pairs/frame simulated
replays do not reproduce this feedback. Validate any future mixer change for PCM
and voice-state equivalence, then repeat this physical workload.

Battle rendering remains another substantial target: without audio, work still
averages about 53–54 ms/frame. Even with both raster and audio disabled, Draw scripts
and setup remain. Do not read the Neither column as pure Step/VM cost.

The legacy castle windows in this log sampled outdoor room 104. They do not measure
the user-confirmed flashing-tile corridor. That scene requires the corrected
[Deltarune Tiles diagnostic](2026-10-18-flashing-tile-benchmark-validation.md) and
its separate `deltarune_tiles_1.sav` result. No slowdown fix is claimed here.
