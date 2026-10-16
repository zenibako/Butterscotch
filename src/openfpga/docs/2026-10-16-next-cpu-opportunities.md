# Next Deltarune CPU opportunities — 2026-10-16

Layer-element lookup is the strongest newly measured spade target: 1,200 linear
searches perform 155,400 element-ID comparisons per sampled frame. Removing the
VM call handlers' redundant stack-argument clearing is the smallest candidate.
Preparing precise-collision setup once per pair scan remains a separate dodge
candidate. These are work measurements and recommendations, not speedup results.
No production optimization was implemented in this exploratory pass.

## Existing decisions

The [optimization design](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md)
requires counting work before selecting another collision or allocation change.
The [spatial-grid results](2026-10-16-spatial-grid-results.md) retain bounded grid
synchronization and defer `instance_exists` because the measured copies are tiny.
The [collision-dispatch results](2026-10-15-collision-work-results.md) defer target
sorting/caching; they leave precise pixel scans unmeasured.

The [translated tile-picture optimization](2026-10-14-tile-cache-reuse-results.md)
already avoids rebuilding a picture when a tile layer translates. It does not
remove the game's script calls that update each tile. The performance skill also
records the removed local/argument frame pool: a prior device comparison saved
only 0.2–0.3 ms, within noise. This investigation does not repeat that pool.

## Method and checks

Source export: `68b999d40c66760a9d7c2790d603579dba6a75af`, after the retained
spatial-grid work. All instrumentation and builds use private exported copies;
the maintained source checkout receives only this report.

The final desktop counter binary uses the port's Deltarune/WAD17 defaults with
`-O1 -g0`, seed 7, `BS_UNCAPPED=1`, `BS_NOFLIP=1`, dummy SDL video/audio, fresh
volatile benchmark directories and at most two compiler jobs. It executes the
normal route through frame 3326, or the established recreated spade route through
493. Samples are field 2800–2802, attack meters 3120–3122, dodge 3323–3325 and
spades 490–492. Accuracy and Speed are independent runs.

The spade recreation sets globals at input frame 29, jumps to room 43 at 30 and
presses Z at 40,55,...,475. Sampled frames assert room 43, active/visible spades
and no battle controller: 9 active, 9/8/8 visible, zero controllers. Its draw notes,
draw timing and VM profiling are disabled before steps. Normal-route diagnostics
remain at their existing defaults. No operation clocks are added.

The primary check comprises eight final probe runs (counter on/off × two routes
× two modes), plus four independent capture-only runs from the same source
revision. The capture-only binary has event/pixel hooks but no new counters or
collision-helper refactoring. All twelve exit successfully. Across 48 direct
sampled-frame comparisons, raw RGB555 bytes, dimensions, scene records and
event-entry count/hash agree. Accuracy captures are 640×480; Speed 320×240.
Counters are dumped/reset after drawing, presentation and pending room changes.
Earlier counter iterations agree on the common counters.

Event hashes cover Runner code/collision-event entry, not every nested opcode.
Pixels are captured before the host overlay. This screen does not compare PCM,
complete snapshots or scanout, and includes no new RV32 timing or Pocket run.
Desktop durations are watchdog metadata, not performance results. Only the
sampled windows establish the rates below; zero sampled work is not a whole-game
claim. Private diagnostics are throwaway code and are not production fixtures.

## 1. Layer-element ID lookup

In each sampled spade frame, `gml_Script_tile_layer_shift` executes 1,225 direct
VM `CALL` instructions:

| Callee | Calls/frame |
| --- | ---: |
| `layer_get_all` | 1 |
| `array_length_1d` | 2 |
| `layer_get_depth` | 21 |
| `layer_get_all_elements` | 1 |
| `layer_get_element_type` | 240 |
| `layer_tile_get_x`, `layer_tile_x`, `layer_tile_get_y`, `layer_tile_y` | 240 each |

The last five operations all reach
[`Runner_findLayerElementById`](../../runner.c), which scans runtime layers and
then their element arrays. Direct counters inside that search record, for each
of frames 490/491/492, in both modes:

| Work | Count/frame |
| --- | ---: |
| Searches / successful hits | 1,200 / 1,200 |
| Layer visits, including repeated visits | 6,000 |
| Actual element-ID comparisons | 155,400 |
| Maximum comparisons in one search | 249 |
| Successful lookup immediately repeating the previous runner and ID | 960 |
| Comparisons performed by those repeated lookups | 124,320 |

Thus 80% of these searches immediately repeat an ID. This is locality evidence,
not proof that a cache would remain valid across mutations. No layer-element
search is observed in the other selected windows.

Prioritize a lookup experiment without changing the game script. A last-result
hint could exploit the measured locality; a complete ID index could address
first lookups too. The design must first account for layer/element insertion,
deletion, movement, array reallocation, room transitions and snapshot restoration.
Raw pointers can become invalid. Preserve first-match/miss behavior, `outLayer`,
mutation visibility and snapshot eligibility; derived lookup state must not
change the snapshot format. Benchmark its maintenance and validation costs.

## 2. Redundant stack-argument clearing

Both [`handleCall` and `handleCallV`](../../vm.c) initialize a 16-slot `stackArgs`
array before overwriting every live slot with `stackPop`. The preserved production
RV32 ELF confirms an unconditional `memset(..., 0, 128)` in each handler, before
the zero-argument branch. DWARF establishes an 8-byte RV32 `RValue`; desktop
allocation payloads use a different size and are not substituted here.

| Window | VM calls/frame | RV32 stack bytes cleared/frame |
| --- | ---: | ---: |
| Field 2800–2802 | 89–91 | 11,392–11,648 |
| Attack meters 3120–3122 | 245–247 | 31,360–31,616 |
| Dodge 3323–3325 | 258–272 | 33,024–34,816 |
| Spades 490–492 | 1,348–1,354 | 172,544–173,312 |

All sampled calls use direct `CALL` and at most 16 arguments; dynamic `CALLV`
and overflow require separate regression coverage. Counts agree between modes.
Most spade calls are the tile-shift calls above, so these are overlapping causes
of work; do not add separate hypothetical speedups together.

The smallest candidate removes only the two `= {0}` initializers. The emitted
RV32 assignment loops write both words of every live argument before dispatch;
zero-argument calls pass null, and cleanup visits only live slots. Preserve the
overflow allocation path, argument ownership, local initialization and growth.
Confirm the clears disappear in the candidate ELF, then test zero, 16 and 17+
arguments, nested/dynamic calls/events and reference-valued arguments in both games.

Heap script-argument storage is much smaller: 4–6 allocations/frame in spades,
9–12 in dodge, requesting only 48–80 and 232–240 RV32 payload bytes respectively.
No overflow argument allocations or nested-event stack-save allocations occur
in these selected windows. Zero-local requests are counted separately because
`safeCalloc(0, ...)` returns before libc. These observations continue to favor
the initialization change over another broad allocation pool.

## 3. Repeated setup in precise pair scans

Counters cover pair sides separately, preserving A-before-B short circuit.

| Dodge frame | Precise scans | A point tests | B point tests | Rotated tests | Mask reads |
| --- | ---: | ---: | ---: | ---: | ---: |
| 3323 | 5 | 1,171 | 767 | 15 | 1,923 |
| 3324 | 6 | 1,182 | 774 | 22 | 1,939 |
| 3325 | 4 | 1,156 | 752 | 0 | 1,908 |

Counts agree between modes. The other selected windows execute no point-helper
work; separate origins cover the shape/list/standalone-point APIs and mouse
dispatch. This does not establish their invocation or bbox costs.

Production RV32 inspection confirms scale checks remain in the pixel loop and
the point helper repeats angle checks, two divisions, origin conversions and
mask frame/stride setup. Rotated points also repeat the existing software-double
radians calculation and float trig calls. The compiler delays frame remainder
until mask-local bounds acceptance, so mask reads, rather than the earlier
source mask-selection counter, bound that operation's observed frequency.

Screen a transient prepared descriptor for each pair side, confined to one
synchronous precise scan and initialized lazily. Keep per-sample division and
arithmetic order, truncating casts, compatibility sampling and early exits.
Prepare mask metadata only when reached. End its lifetime before callbacks or
other mutation boundaries. Most samples are unrotated; this hypothesis concerns
repeated checks/setup rather than primarily trig. Standalone/shape APIs do not
yet justify widening the change.

## Evidence identity

Private evidence is retained under
`$CHECKOUT/.superpowers/next-optimization-20261016/`: source exports, builders,
patches, headers, exact build/run commands, raw sampled pixels, logs,
`summary-r4.json`, `analysis-r4.log`, disassembly excerpts and `manifest.json`.
The first probe iteration's shutdown totals are invalid: it retained borrowed
code names past `DataWin_free`. Later iterations copy names into owned storage;
independent static review verified the repair and final counter placement.

- Final summary SHA-256: `656ab7ef1330fd3069792ec13f0de6e4550d55d2ac868320699198ba37cd5e91`.
- Manifest SHA-256: `1d1c8167f873f1351ecca4429d1a2547eaac4a8b854a75127abda8d8ff069e84`.
- Final counter binary SHA-256: `c85cd84a7db370fef62387fd698c8ad676076cad85db35cf05326b9d986c8927`.
- Capture-only binary SHA-256: `3ad70664c490987ef906d649cfe56bc83592e87560abd5f8b210bd05f449768d`.
- Inspected RV32 ELF SHA-256: `83e14e1e37fce649e453d3ad19e98ae3478f09e14113d15bc1918106acdd50a1`.

That ELF is the preserved spatial-grid `b6a12f6` dodge Accuracy timing case,
not a new build of `68b999d`. Git blob checks establish that its `vm.c`,
`collision.h`, `vm_builtins.c` and `runner.c` match the source export. Its exact
case path and tool commands are in the private evidence. No candidate timing,
hardware benefit or broad behavioral verification is claimed by this report.
