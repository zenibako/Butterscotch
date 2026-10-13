# Undertale Savestates Implementation Plan

> For agentic workers: execute the tasks with tests and review before integration.

**Goal:** Exact save states and sleep for safe Undertale overworld/menu states.

**Architecture:** A versioned platform envelope carries runner, audio and progress
archive snapshots. Capture and restore occur at the completed frame boundary;
unsupported state is refused with an in-game notice.

**Tech Stack:** C, existing RValue/Instance types, software audio backend,
openfpgaOS save-state service, os25, SDL desktop verification and Verilator.

**Spec:** `docs/superpowers/specs/2026-10-12-undertale-savestates-design.md`

## Global constraints

- Undertale only; battles and dialogue refused initially; Deltarune unsupported.
- No raw process pointers in saved data; preserve shared values and iteration order.
- Validate and allocate before mutating the live runner.
- Envelope maximum 1 MiB minus the openfpgaOS header; refuse oversized states.
- Stay on os25 with a paired os.bin/bitstream/SDK; patch bootloader after OS changes.
- Never package or replace the user's progress save file during deployment.
- Report hardware behavior as unverified until the user tests it.

### Task 1: Runner snapshot codec and transactional restore

Files: `src/runner_snapshot.c`, `src/runner_snapshot.h`, narrowly scoped helpers
in `src/runner.c`, `src/runner.h`, `src/sw/sw_renderer.c`, and tests under `tests/`.

Interfaces:
```c
typedef struct RunnerSnapshot RunnerSnapshot;
bool Runner_snapshotCapture(Runner *r, uint64_t gameId,
    uint8_t **data, size_t *size, const char **reason);
RunnerSnapshot *Runner_snapshotPrepare(Runner *r, uint64_t gameId,
    const uint8_t *data, size_t size, const char **reason);
void Runner_snapshotApply(Runner *r, RunnerSnapshot *snapshot);
void Runner_snapshotFree(RunnerSnapshot *snapshot);
```

- [x] Write tests for scalar/array graphs, shared array ownership, globals,
  instance IDs/order/alarms, RNG, malformed input and unsupported-resource refusal.
- [x] Run them against the missing implementation and record the failure.
- [x] Implement bounded binary encoding; prepare into an owned temporary graph;
  rebuild indices and visual state without gameplay events during apply.
- [x] Run unit tests and an ASan build; check cleanup after rejected input.
- [x] Review the codec and integration contract before platform use.

### Task 2: Audio and progress-file sections

Files: `src/openfpga/platform/ut_snapshot_parts.h`, `of_audio_system.c`,
`ut_save_fs.c`, and targeted tests.

Interfaces (opaque plans keep allocation and I/O out of commit):
```c
bool utAudioSnapshot(AudioSystem*, uint8_t**, size_t*, const char**);
void *utAudioPrepareSnapshot(AudioSystem*, const uint8_t*, size_t, const char**);
void utAudioApplySnapshot(AudioSystem*, void*);
void utAudioFreeSnapshot(void*);
bool utSaveFsSnapshot(FileSystem*, uint8_t**, size_t*, const char**);
void *utSaveFsPrepareSnapshot(FileSystem*, const uint8_t*, size_t, const char**);
bool utSaveFsApplySnapshot(FileSystem*, void*);
void utSaveFsFreeSnapshot(void*);
```

- [x] Test archive rollback and malformed archive rejection; test voice cursor,
  decoder/resampler state, gain ramps and pause/loop flags across a restore.
- [x] Capture pointer-free logical state; prepare cache/file backing before apply.
- [x] Commit prepared state without allocations; preserve backend identity.
- [x] Compare continued audio and archive contents before/after restore.

### Task 3: Envelope, frame boundary and visible refusal

Files: `src/openfpga/platform/ut_savestate.c`, `ut_savestate.h`,
`of_platform.c`, `of_hooks.h`, `src/loop.c`, `src/openfpga/main.c`, Makefile.

- [x] Test envelope lengths, checksum and game identity; reject before mutation.
- [x] Hash game data at startup; cache identity for later snapshot requests.
- [x] Poll at the completed frame boundary, prepare all four sections and apply
  together. Reset pacing and stale input after restore.
- [x] Add explicit Undertale overworld/menu eligibility and specific notices;
  expose readiness in the port menu. Keep Deltarune refused.
- [x] Add desktop capture/restore controls and cold-process frame comparisons.
- [x] Run overworld/menu comparisons and battle/dialogue refusal tests.

### Task 4: Paired os25 runtime and delivery

Files: staged runtime outside the stock SDK; core definition, build settings,
packaging scripts and `src/openfpga/README.md` in this repository.

- [x] Enable the service in the isolated os25 runtime checkout.
- [ ] Re-run handshake simulation (Verilator unavailable on this host/image).
- [x] Fit os25 and inspect setup/hold reports, sweep if a fit is unsuitable.
- [x] Stage SDK, bitstream, os.bin and matching bootloader together.
- [x] Build Undertale, run lint/ASan and existing frame comparisons.
- [x] Package a separately named test tree/zip and report measured limits.
- [x] Commit implementation with signing; leave publishing decisions to the user.
- [ ] Validate fresh launch, Memories, sleep/wake, and refusal on the Pocket.

## Progress

Implementation and desktop integration are in place. Cold-process restore has
matched future frames for the overworld, movement through a room transition,
and the main and status menus. Dialogue refusal was visually checked. Archive
short-write/close-failure tests preserve existing progress, and the actual SDL
queue test verifies that restore discards abandoned audio. All five review
findings in the runner were fixed and independently rechecked. The combined
sanitizer suite and lint pass. Real ASan/UBSan runs cold-restored overworld and
status-menu snapshots, then closed the menu and moved, matching future frames.

The initial os25 fit failed 100 MHz timing. Parallel seed fits exhausted VM
memory. At 90 MHz, seed 20 still missed setup by 0.278 ns. Seed 8 passes every
reported clock/model check; the CPU setup margin is 0.177 ns and the minimum
reported margin is 0.089 ns. The paired runtime and RISC-V app are packaged in
`src/openfpga/out/butterscotch-undertale-native-saves-test.zip`. Current real
snapshots are about 786 KiB, within the service envelope. Hardware behavior
remains unverified. Task tracking is here because the configured GitLab work-item
lookup is unavailable from this sandbox.

## First Pocket test: open INI and refused sleep

The Snowdin overworld save was refused because the runner kept an INI context
open. The same refusal reproduced on desktop with a temporary copy of the
Pocket's ordinary progress archive, in `room_tundra3`. The card's sleep payload
still contained PICO-8 data with a valid outer CRC, explaining Undertale's wake
rejection. The user confirmed sleep was attempted after the INI refusal; no
successful Undertale save state preceded it.

Current and cached INI sections, ordered keys/values, paths, and dirty state are
now included in a bounded optional core trailer. Preparation and apply do not
flush those edits to disk. Desktop save/cold-load of the same Snowdin scene now
succeeds; subsequent movement frames match byte-for-byte, including under
ASan/UBSan. Wrong-application payloads have a specific rejection message.
User-facing terminology is "save states". Pocket sleep/wake retesting is pending.

The later device diagnostic recorded successful core capture in 10,593 ms
and total capture in 10,881 ms, followed by a 10,862 ms transfer call that
returned zero despite the Pocket showing Save failed. The OS includes a
10-second host-read wait and returns zero even on its timeout, so the transfer
result alone is not proof of a written state. Capture latency is the current
timeout hypothesis. Compression level 2 preserves the zlib snapshot format;
the Snowdin desktop capture dropped from 47 ms to 22 ms, stayed below the
combined payload limit, and restored byte-identical future frames. Hardware
save and sleep/wake retesting remains required.

The level-2 device retest captured in 6,440 ms, then again waited 10,957 ms
in transfer. Both device runs also logged a six-second direct music-read
timeout immediately before capture. The next change defers optional music
refills while the Pocket menu or an outstanding state request is visible,
before modifying buffered data or issuing any file call. A side-effect-free
SS_STATUS read uses the pinned runtime register layout. The audio regression
failed before the guard, then passed with unchanged voice/buffer state while
deferred and successful real-file refill after resuming. An already accepted
read can still race a request; this change does not cancel host transfers.

The guarded device retest still showed a six-second read stall before capture;
the first deferral message appeared only after transfer returned. Source review
then found that os25's hardware-mixer boot leaves the machine timer disabled,
while the existing two-second async watchdog runs only from that timer ISR.
The save-state app now installs an empty 1 kHz callback through the existing
SDK API to enable OS housekeeping. It logs timer control before/after.
Failed optional direct reads no longer fall through to another blocking host
read, and their staging allocation remains alive for late completion. A second
regression injects a request after refill entry and verifies no new file read,
preserved unread bytes, and normal resumed refill. Hardware confirmation of
watchdog timing and save delivery remains required.

The watchdog device retest confirmed timer control `0 -> 1` and reduced the
music-read stall to about two seconds. Capture still took 6,525 ms and transfer
10,970 ms; delivery is not fixed. The user also hears music gaps during initial
200–700 ms texture loads, which settle after walking for a few seconds. The
100 ms output target cannot cover an uninterrupted read longer than that, and
the runtime skips app idle hooks during syscall reads. The next diagnostic
measures graph/encoding/CRC/compression/packing separately, samples host read
words after transfer, notes requests first seen during a music read, and splits
slow texture seeks from reads. Further behavioral patches should follow those
measurements rather than assume a Pocket deadline or compression bottleneck.

The detailed device run measured graph 164 ms, encoding 1,089 ms, raw checksum
278 ms, deflate 4,545 ms, and output packing/checksum 179 ms. Total capture was
6,580 ms; the host-read counter remained zero. A request was seen during an
accepted music read, so the timing gap also includes its blocked completion.

The next package changes that measured compression bottleneck to pinned
Zstandard 1.5.7 level 1 in core revision 4, retaining revision 3 decoding.
The full Snowdin desktop capture is about 10 ms and fits the envelope; both
overworld and game-menu restores reproduce future frames exactly. Corrupt,
truncated, trailing, unknown-size, and oversized-window frames are refused.
The OS now logically aborts an unfinished async read for a pending save and
quarantines its storage until physical completion; fault tests cover late IRQs,
blocked reuse, normal completion, and LOAD exclusion.

Initial texture stalls also include an allocation/free probe of the 4 MiB heap
reserve after each new item. Large musl allocations use a zero-filled mmap.
The native package uses a conservative read-only capacity query when the fresh
mmap gap and tracker space prove the reserve plus allocator headroom fits;
otherwise it retains the original allocation/eviction policy. The first check
logs old-probe and query timings for an on-device comparison. Tests confirm
bounds, nonmutation, append-only API checks, and fallback behavior.

The updated OS was rebuilt and its matching bootloader patched into the
previously timing-verified os25 bitstream before SDK export. Generic CMake CLI
and Pocket app builds pass; runtime patch reverse-application was checked.
Hardware save/load, sleep/wake, and audio/stutter improvements remain unverified.

### String references and transfer failure reporting

The next hardware log confirmed the matching package was installed. Capture
still took 5,786 ms, including 3,787 ms in Zstandard; the host read zero words.
The sleep-state file retained its older P8SS payload, explaining the wrong-format
message on wake. A successful Undertale save has not yet been observed.

The allocation comparison measured 198,469 us for the old 4 MiB probe and 52 us
for the new capacity query. This verifies the removed per-texture probe cost;
it does not establish that all video or music stalls are resolved.

The desktop Snowdin raw graph was dominated by two text maps. Core revision 5
uses content-checked indices for strings already in STRG, preserves unmatched
strings inline, and restores independent owned copies. Both writer and reader
bound cumulative expanded strings, including terminators. Revision 3 and 4
readers remain available. Four-byte Zstandard matches avoid the wider hash path
on RV32 while retaining the same decoder format.

The runtime uses an equivalent table-based CRC32 and reports a timeout when the
host read is incomplete. Source tests cover complete/partial/zero transfers,
timer wrap, ownership, a single completion, and CRC parity. The OS and matching
bootloader are rebuilt before exporting a fresh SDK. Pocket transfer success
and sleep/wake restoration remain hardware gates.

Verification of the revision 5 package: the full desktop Snowdin capture is
257,090 bytes (507,841 raw core bytes, 175,318 compressed), and the game-menu
capture is 257,135 bytes. Each restores in a fresh process and reproduces the
future frame byte for byte. Real revision 3 and 4 captures reproduce the same
reference frame. A full Snowdin capture/restore under ASan/UBSan also passes.
The native build and ZIP checks pass, including exact ELF/runtime pairing and
exclusion of progress saves. Test package: `butterscotch-string-ref-save-test.zip`.

### October 13: successful save, load refusal and stale wake state

The Pocket saved a 239,250-byte Undertale envelope with a valid outer CRC. The
log measured 2,618 ms for capture and 1,590 ms for transfer, with 59,849 host-read
words against 59,821 expected. The manual save gate passes. Manual load still
showed "Loading failed" and a battle-frame refusal for saved room resources.
Sleep from the overworld did not replace the older P8SS sleep-state file.

The load eligibility check incorrectly rejected saved room resources in the live
state being replaced. Preparation now accepts those resources without touching
them; application releases them using the runner's shared cleanup helper.
Capture remains restricted. A regression models an overworld snapshot loaded
from a battle holding room instances, tiles and runtime layers, checks refusal
without mutation, and checks cleanup and restoration after commit.

Successful native loads now acknowledge after all four sections commit, before
freeing the old graph. Request completion ownership prevents a second finish
after OS receive rejection or save completion from rejecting a newer request.
Source tests exercise commit ordering, failure paths and retries during cleanup.
Request/result and restore-stage logs expose future sleep refusal and load
latency. No numeric Pocket load or sleep deadline is assumed. Clean manual load
and sleep/wake remain device verification gates.

Verification: the snapshot suite and lint pass; saved-room and completion tests
run under ASan/UBSan. A desktop capture and fresh-process restore reproduce the
prior future-frame reference byte for byte. The native build and ZIP checks
pass, with the unchanged matching runtime pair and no progress saves bundled.
Package: `butterscotch-restore-room-test.zip`.

### October 13: functional hardware restore confirmed

The user confirmed manual save/load and sleep/wake restore the expected game
position. The newest log reports successful manual and wake loads; wake now
receives a 238,293-byte Undertale payload. Basic hardware restoration passes.
The Pocket still displays a red "Loading failed" during manual load.

Manual load took 9,103 ms: receive 131 ms, game preparation 2,551 ms, audio
27 ms, files 7 ms, application 6,204 ms, and old-graph cleanup 180 ms. Wake
restore after startup took 3,021 ms, with application only 25 ms. The combined
application interval covers progress-archive write, runner apply, audio apply
and platform input/clock reset. Source inspection does not isolate the delay;
progress-file persistence uses CRAM and datatable updates, not an SD write.

Add separate timings for those four application calls before changing their
behavior. The red warning is consistent with delayed completion, but a numeric
host deadline is not established. Clearing the warning remains a hardware gate.

The next device run disproved a consistently slow apply operation: archive
12 ms, runner 3 ms, audio 9 ms and input/clock 0 ms; total apply 26 ms. Game
preparation instead took 10,874 ms, and total manual load 11,248 ms. The same
238,139-byte state still restored successfully. That game interval also includes
the loading notice. Core preparation can read absent room definitions from
`data.win`, so file-service contention is a candidate, not an established cause.

Add notice timing, core decode/room/value/index/free timings, and individual
staged room-read timings. Preserve codec and acknowledgement semantics until
those measurements distinguish asset I/O from allocation and graph work.

The preparation diagnostic passes the snapshot suite, completion tests and lint;
its fresh-process restore reproduces the reference future frame byte for byte.
The native ELF contains the new timers. The card's OS and bitstream match the
staged runtime exactly, so this test can replace only
`Assets/butterscotch/common/undertale.elf` from `prepare-profile-test`.
