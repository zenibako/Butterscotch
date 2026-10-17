# Readable Deltarune battle names — 2026-10-17

Speed mode now point-samples the recognized KRIS, SUSIE and RALSEI name labels,
with a hand-hinted five-by-eight R for KRIS and RALSEI. Names keep their size and
placement. This is the approved legibility-first prototype, retained with asset
and transform guards. Other HUD elements retain the
[shared battle sampler](2026-10-16-shared-battle-sampling-results.md);
[regular font sampling](2026-10-16-font-sampling-results.md) is unchanged.

The source name labels are 43x15, 48x15 and 61x15, rather than exactly doubled
pixel art. Strongest-foreground filters closed letter counters and blurred HP
into blocks; no single nearest-neighbor phase preserved every original stroke.
The original R has an almost vertical lower-right stroke. Hinting gives its leg
a distinct diagonal at the native 320x240 Speed resolution. Accuracy remains the
reference and receives no hinting.

The game adapter supplies names, exact source dimensions and FNV-1a fingerprints
of the original RGB555 texels. The shared renderer accepts only immutable,
white/transparent sources, ordinary unrotated full-sprite draws, normal blending,
positive exact 2:1 scaling, and the active battle Speed context. Modified,
localized, resized, rotated/flipped, surface or other unsupported draws keep
their existing paths. All source texels are checked once per source texture
lifetime; small derived masks expire with that texture. Original source and
shared half-size cache pixels remain unchanged. A native-mask clipping correction
preserves a single visible row or column. The temporary texture wrapper cannot
be retained by deferred overlay merging.

## Verification

Both game configurations pass 1,367 battle contrast/guard cases, 166 font sampling
cases and 499 sprite clipping cases under ASan/UBSan with the existing alignment
and function sanitizer exclusions. New cases cover literal R shape/counters,
source phases, tint, opacity, one-pixel and other clipping, changed fingerprints,
changed/colored source pixels, invalid source/patch bounds, transforms, inactive
battle context, mutable textures, and original/shared-cache preservation. A
private disabled-feature control fails the literal shape test; a one-pixel
clipping test reproduced the review finding before its correction. Lint and the
snapshot suite pass.

Seeded desktop comparisons use Deltarune battle frame 3050 and Undertale battle
frame 2400, plus frame 300 in each game and mode. Both battle Accuracy captures,
all four intro captures and Undertale's Speed battle are byte-identical to their
baseline. Both Speed battle captures match their frame-skip=2 controls. The new
Deltarune Speed battle matches the approved private prototype byte-for-byte.
Full-game captures retain the documented vendored stb_ds signed-shift diagnostic;
focused renderer fixtures have no sanitizer diagnostics.

Exact commands, source revision, negative controls, independent review, captures
and enlarged comparison live in private `.superpowers/hud-hint-build-20261017/`.
No original game pixels or extracted game assets are checked in. There is no
physical Pocket or native timing claim from these desktop checks. Packaging uses
the existing verified paired runtime, retaining Memories, sleep and save-slot
flush behavior; new OS or FPGA sources are outside this change.
