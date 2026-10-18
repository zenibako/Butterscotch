# Loading investigation: streamed TXTR metadata deferred

Task 4 retains **no production change**. The candidate removed a 256 KiB temporary
TXTR allocation and reduced both logical bytes and modeled libc read capacity.
It also increased metadata seeks and refill calls. Without target cold-read
latency and RV32 parser-cost measurements, that tradeoff remains unproven. The
candidate is deferred; the evidence does not establish that it is slower.

Baseline: `57222b381f9c4bd1fdcbe864e998ed21bb80030e` (production `007ca69`).
The [sequential design](../../../docs/superpowers/specs/2026-10-17-sequential-optimization-design.md)
and [Pocket performance guidance](../../../.claude/skills/butterscotch-pocket-perf/SKILL.md)
require counts before selecting a change and distinguish cold reads from decoder
CPU cost. Existing STRG reuse, lazy TXTR staging, tiled packs and the texture read
window were already present. Original assets and pack formats remain unchanged.

## Measurements on the retained predecessor

Private adapters counted actual `fread` returned bytes and exact file spans in
DataWin, BinaryReader and the texture pack reader, plus in-memory metadata
consumption and tile decode work. These are application-level requests, **not
physical SD traffic**. No clock calls were added to the counters. Startup stops
at frame 1; longer runs use the deterministic benchmark through exit frame 3047.

| data.win workload | Returned bytes | Unique span bytes | Repeated span bytes |
| --- | ---: | ---: | ---: |
| Undertale startup | 16,299,596 | 16,297,488 | 2,108 |
| Deltarune startup | 5,022,004 | 5,020,216 | 1,788 |
| Undertale through 3047 | 16,308,396 | 16,297,488 | 10,908 |
| Deltarune through 3047 | 5,073,020 | 5,020,216 | 52,804 |

Each game stages 262,144 TXTR bytes. Undertale actually consumes 316 unique
metadata bytes (26 textures, eight-byte records); Deltarune consumes 260 (eight
textures, 28-byte records). The count/pointer table is included. The maximum
consumed end equals those lengths, so the staged suffix is unused by parsing.

| Texture workload through 3047 | Returned pack bytes | Repeated span bytes | Decode calls / distinct tiles | Encoded bytes decoded | Decoded bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Undertale | 562,686 | 42,298 | 337 / 313 | 457,650 | 2,760,704 |
| Deltarune | 1,126,500 | 259,368 | 566 / 440 | 1,093,232 | 4,636,672 |

Repeated file spans and repeated tile decodes are separate measurements. The
one-window cache can reuse bytes without avoiding another decode. A broader
persistent tile cache was not selected: its ownership, eviction and whole-app
heap effects need a separate design and a larger workload sample.

## Candidate, tests and deferral

The measured candidate excluded only lazy TXTR from per-chunk staging and used
the existing FILE-backed BinaryReader. Eager textures and other chunks retained
their existing paths. The controller approved this concrete design before edits.

The focused fixture first passed baseline behavior and then failed the intended
byte bound: 262,144 versus 316/260. Candidate checks passed under both WAD16 and
WAD17 with ASan/UBSan. They covered exact metadata against the mapped loader,
nonzero chunk base, following-chunk traversal, eager exact blob bytes, lazy
read-once behavior, truncated metadata, out-of-file pointers, and the removed
256 KiB allocation. Child failures disabled core dumps. Lazy loading does not
validate unread PNG payload; these are metadata checks, not a full asset
validator. Successful loads were freed; existing malformed-input paths abort,
so recoverable failure cleanup was not introduced or claimed.

The maintained `loading-fixture` target compiled and passed with each game before
being withdrawn with the deferred implementation. Real benchmark captures also
matched across baseline/candidate and counter-on/off controls: raw RGB555 plus
state/event records at Undertale frames 2604–2606 and Deltarune 3044–3046. No PCM
comparison, native candidate build or RTL timing was performed for this deferred
candidate.

Target inspection confirms musl `__fseeko_unlocked` clears both read-buffer
pointers after each successful seek. A review correction matters here:
`setvbuf(file, NULL, _IOFBF, 128 * 1024)` **does not install a 128 KiB buffer** in
this target libc. Its null-buffer branch skips the size assignment, retaining
`__fdopen`'s default 1,024 bytes. Only a non-null supplied buffer uses `size - 8`.
`__stdio_read` requests the caller's remaining length minus one plus that buffer.

The initial model incorrectly applied `size - 8` to the null-buffer call and
therefore overstated requested bytes. That model and its report are preserved as
explicitly superseded evidence. The corrected model verifies both original
instrumented source callsites pass null; the RFBUF trace itself records mode and
size but omitted the buffer pointer.

Actual baseline/candidate seek/read traces with the corrected target-libc model:

| Game | Baseline bulk calls / bytes | Candidate metadata seeks / fread calls | Candidate logical bytes | Modeled candidate refill calls / requested capacity |
| --- | ---: | ---: | ---: | ---: |
| Undertale | 4 / 262,144 | 26 / 79 | 316 | 26 / 26,702 |
| Deltarune | 4 / 262,144 | 8 / 65 | 260 | 8 / 8,216 |

The old bulk handle is explicitly unbuffered and reads four 64 KiB pieces.
The model tracks preceding FILE-buffer state and excludes the shared header
refill from this table. Baseline metadata parsing is in memory; the baseline
bulk operation needs one seek. Whole traced TXTR-range seek counts are one for
Undertale and two for Deltarune (the latter includes another range seek); the
candidate records 26/eight metadata-entry seeks.

The candidate saves 235,442/253,928 modeled lower-level requested bytes but adds
22/four refill calls. These are modeled libc requests, not measured Pocket
syscalls or physical SD traffic. The OS cache, fixed per-request cost and parser
CPU overhead determine whether smaller requests improve startup. Matching host
pixels establishes semantics, not that latency tradeoff. The controller deferred
the candidate pending those measurements; no slowdown or speedup is claimed.

## Memory, compression and coverage limits

The deferred candidate removes 262,144 transient bytes but does not establish a
lower whole-app peak. The largest observed parser buffers were 5,264,628 bytes
(Undertale) and 1,866,216 (Deltarune). Existing pack indices require 96,464 and
57,416 heap bytes respectively; the decoder also has a 65,536-byte static input
window and 32,768-byte static output tile, plus caller-owned output. These are
component sizes, not a complete measurement of the roughly 51 MB app heap.

No new compression format was implemented or asset pack generated. RV32 decoder
cycles and complete peak-memory evidence for such a format are absent. Host
compression ratios or throughput would not meet the design's retention criteria.
The existing decoder's encoded/decoded byte counts above establish work volume,
not decoder cycle cost or a compression speedup.

Focused fixtures had no sanitizer findings. Whole-app Deltarune runs retained the
existing `vendor/stb/ds/stb_ds.h:1132` signed-shift UBSan finding; ASan reported no
errors. Flags were `-O1 -g -fsanitize=address,undefined
-fno-sanitize=alignment,function -fno-omit-frame-pointer`. LeakSanitizer is
unsupported on this macOS host, so runs used `detect_leaks=0`. This is not a clean
whole-app UBSan or leak-check claim.

Private evidence is under `src/openfpga/out/sequence-loading-20261017/`:
`counts.json`, `refill-counts.json`, `memory-counts.json`, capture summaries,
per-game commands/logs, before/after asset hashes, and `rejected-candidate/` with
the exact four files, complete patch and SHA-256 manifests. Target disassembly
and its ELF/compiler provenance are in
`.superpowers/sdd/2026-10-17-sequential-pocket-optimizations/target-stdio-evidence.json`
and the adjacent `target-{fseeko,stdio-read,setvbuf}.txt` files. The incorrect null-buffer model is explicitly superseded in
`superseded-incorrect-128k-null-buffer-model/`; `refill-model-correction.json`
records the correction. All candidate edits
were restored to baseline bytes; the fixture is preserved privately. Production,
SDK/runtime, normal saves, Memories, sleep and snapshot formats remain unchanged.
