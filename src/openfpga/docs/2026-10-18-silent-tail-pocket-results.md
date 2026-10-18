# Physical Pocket: silent audio tail results

The returned `deltarune_tiles_1.sav` is 105,278 bytes, SHA256
`a14ed04238d04f7736a9885048aafc8af8720df6a45aba59695c5f445b67057e`.
It contains seven complete report occurrences and five byte-exact unique reports;
all five modes PASS. Every report identifies the [silent-tail implementation](2026-10-18-silent-audio-tail-results.md)
`7acc6547544b500a1b756dc011cc281c8617c981`, Speed mode, uncapped, seed 7,
OS 0.9.0, core 0 and reported CPU 100 MHz. Exact reports and parsed values are
private under `.superpowers/silent-tail-pocket-results-20261018/`.

## Battle comparison

Compare against [the earlier physical battle results](2026-10-18-pocket-battle-text-results.md)
from `4388a476ab538ff208bc6dc948b754868bc07216`. The corrected castle route begins
later than these battle windows. Both runs accept the same battle text and sample
counts: encounter reveal/full 38/30, observation reveal/full 29/50. Filtered frames
are rejected semantic samples, not timing outliers. These are single runs per
mode, with correlated frame samples, rather than independent repeated trials.
Values below are mean work milliseconds/frame, excluding same-frame flip.

| Normal mode window | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| Encounter reveal |107.646|69.152|35.8%|
| Encounter complete |61.223|60.853|0.6%|
| Observation reveal |100.131|63.219|36.9%|
| Observation complete |59.720|59.913|-0.3%|

The observation reveal penalty falls from 40.411 to 3.306 ms/frame: 91.8% of that
extra penalty disappears. The observation no-audio controls remain closely matched:
54.151 →54.162 ms revealing and 53.173 →53.263 ms complete. These controls support
attributing the large reveal improvement to audio; their differences are not
additive isolated component costs.

The instrumented observation-reveal voice loop falls from 44.347 to 6.151 ms/frame
(86.1% less), and whole mix from 49.936 to 10.347 ms/frame. Separate sprite raster
time remains 21.766 →21.679 ms/frame. Profiler overhead changes work and generated
audio demand, so profile totals cannot substitute for normal-mode timings.

The new normal reveal counter reports 28,448 bulk-silent voice pairs out of 33,977
voice pairs per frame (83.7%); output is 3,057 pairs/frame. This confirms the bulk
path runs on hardware. It preserves voice lifetime while removing repeated silent
work. Wall-clock audio feedback also changes output demand, so the time reduction
is not a same-pair-count leaf benchmark. Complete observation reports zero bulk
pairs, consistent with its near-unchanged frame work.

This is physical evidence of a substantial reduction in the letter-reveal
slowdown. Remaining normal battle work is still roughly 60 ms/frame. This save
contains benchmark reports, not proof of a physical Memories restore or sleep test.

## Confirmed flashing-tile corridor

All modes identify room 65 `room_field_checkers4`, accept 70 walk, 100 reset-cycle
and 100 still samples, and observe one real return to the beginning of the room.
The reset window records 59 lit, 41 dark and 39 white-flash frames. This confirms
the corrected diagnostic sampled the requested hazard room.

| Mean work, ms/frame | Normal | No rendering | No audio | Neither |
| --- | ---: | ---: | ---: | ---: |
| Walking |63.421|49.582|48.097|36.722|
| Reset cycle |63.689|44.838|49.404|33.780|
| Standing |52.349|39.114|42.767|32.130|

No tile window bulk-advances any silent pairs. The old attachment measured outdoor
room 104, so it provides no valid before/after timing baseline for this corridor.
This change does not establish a corridor speedup. Normal Step work is
33.921/31.448/29.558 ms/frame for walking/reset/still, while substantial work also
remains when raster/audio are disabled. No-render retains Draw scripts and setup;
Neither is not pure Step time, and the columns must not be summed or subtracted
as independent cost components.

The strongest new investigation target is interpreted background compatibility
work. In all three instrumented tile windows, `gml_Script___background_get_element`
accounts for about 24 ms/frame inclusive, with `string_pos` about 11.81 ms and
`layer_get_name` about 2.66 ms. These profiler entries overlap and must not be
added. Similar work persists while standing, suggesting repeated lookup/parsing
rather than movement alone; confirming call relationships requires a focused probe.

`platform/game_deltarune.c#g_nativeScripts` currently has no native replacement
for this background script. `src/vm_builtins.c#builtin_layer_get_name` resolves
runtime layers and then static room layers, preserving dynamic layer names.
Investigate the script's repeated layer-name parsing and lookup before choosing
a cache or native translation. Preserve dynamic layer mutations and Memory
restore behavior. Instrumented costs do not predict recoverable milliseconds in
the normal build. Draw notes also report dropped entries, so their top rows are
not a complete raster inventory. No new implementation is included in this report.
