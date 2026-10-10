# Sprite layout experiment: device result, 2026-10-10

The hot build completed on the Pocket. Its outside-view probe fell only
from 19.87 to 19.56 microseconds (0.31 microseconds, 1.6%); its inside-view
probe increased from 73.48 to 78.34 microseconds (4.86 microseconds, 6.6%).
This fails the proposed criterion of an outside-view result well below
19.9 microseconds. Drop this particular sprite-layout optimisation as a
performance direction; these measurements do not rule out instruction-cache
cost elsewhere.

## Evidence and identity

- Experiment: `f42a23ce36a398d6b4885c93440238af0c927b68`, branch
  `perf/mirrored-blend`. The experiment adds a second program linked with
  `tools/hot-sprites.ld` alongside the ordinary program.
- Source: the user's attached `deltarune_1.sav`, a text log, not game progress.
  SHA-256: `9926f45f59bf9001a1a9c8e26a4f67aeaa4efb9061fd97f511ce832d06069482`.
- [Exact report excerpts](2026-10-10-sprite-layout-results.txt) are copied
  from original lines 1028–1120 (ordinary) and
  1781–1873 (hot). Earlier stored reports without
  probes are excluded; the repeated ordinary summary is counted once.
- Ordinary title: `Deltarune ch. 1 benchmark, speed`. Hot title additionally
  names `-Wl,--section-ordering-file=tools/hot-sprites.ld`.
- Both report OS 0.7.0, core variant 0, CPU 100 MHz, the same 40x40 probe,
  and 3600 benchmark frames. Both end with a probe and the benchmark halt.
  The hot run therefore booted and completed. These are user-supplied device
  measurements; no new device run was performed to produce this report.

## Frame times

Milliseconds per frame; each cell is work / total. Work excludes display flip.
Delta is hot minus ordinary work.

| Section | Ordinary | Hot | Delta work | Change |
|---|---:|---:|---:|---:|
| opening text 320x240 | 27.3 / 28.3 | 26.8 / 27.8 | -0.5 | -1.8% |
| creation screens 320 | 66.7 / 67.6 | 66.2 / 67.0 | -0.5 | -0.7% |
| town yard 320x240 | 27.2 / 27.8 | 27.1 / 27.7 | -0.1 | -0.4% |
| field (640x480) | 44.4 / 45.1 | 44.4 / 45.1 | +0.0 | +0.0% |
| battle: menu, attack | 67.8 / 68.2 | 67.5 / 67.9 | -0.3 | -0.4% |
| battle: dodging | 80.2 / 80.7 | 80.0 / 80.4 | -0.2 | -0.2% |
| battle: next turn | 62.8 / 63.3 | 62.7 / 63.2 | -0.1 | -0.2% |

The 3600-frame run fell from 147.9 to 146.7 seconds (1.2 seconds, 0.8%).
Load to first frame stayed at 11.5 seconds. Battle work improved by just
0.1–0.3 ms per frame; the field did not improve.

## What the probe establishes

`src/sw/sw_renderer.c:swrSpriteCostProbe` warms the sprite's texture and
copies, disables per-draw timing and call notes, then times 2000 consecutive
outside-view calls followed by 2000 inside-view calls.
`src/openfpga/platform/of_platform.c:utPlatformSpriteCosts` selects that count.
The outside measurement includes repeated renderer calls and clipping;
the inside measurement also draws pixels. It calls `Renderer_drawSprite`
directly, so it does not measure the GML builtin wrapper or VM dispatch.

This is a warm repeated-call measurement, not a count of instruction-cache
misses or their stall cycles. The new layout failed to remove the roughly
20 microseconds spent on an outside-view call. A broad conclusion that
instruction-cache misses never matter would go beyond this experiment.

There is one probe-bearing run per build, with no estimate of run-to-run
variance. The small frame improvements and inside-probe regression are
observations, not established statistical effects. Keep the ordering file
available as experimental evidence; no runtime, build tree or card files
were changed while closing the measurement.
