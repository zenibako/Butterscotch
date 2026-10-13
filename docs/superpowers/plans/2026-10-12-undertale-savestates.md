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

### Proven room read and immutable ROOM cache

The next hardware log identifies the delay: staging room 47 took 8,347 ms.
Core decode took 452 ms, values 2,064 ms, indexes 5 ms, and raw-buffer cleanup
0 ms. Application remained 26 ms. The loading notice took 7 ms. Total restore
was 11,292 ms and returned success.

Save-state builds now retain the owned ROOM chunk already read by the per-chunk
loader. This costs 4,507,012 bytes (4.30 MiB) of resident heap without another
startup read. A memory-only reader preserves absolute offsets and fails rather
than falling back to file access. Lazy payload creation/unloading and room-reset
semantics remain intact; root game-data teardown frees the cache. Generic and
Deltarune builds do not enable it. Snapshot versions and game identity remain
unchanged, including compatibility with existing version-5 states.

Verification: fixture tests cover parity, reload after closing the file, invalid
offset rejection and cache-backed cold snapshot preparation. The same parity
test passes for all 336 rooms in the actual Undertale data. The exact cached
feature under ASan/UBSan preserves the reference future frame after capture and
fresh-process restore; an existing version-5 state also matches that frame.
Snapshot tests, lint and native build pass. The card runtime pair matches; replace
only `Assets/butterscotch/common/undertale.elf` from `room-cache-test`.
Pocket load timing, warning removal and the additional heap footprint remain
hardware verification gates.

### ROOM cache hardware result and archive timing

The next Pocket run confirms room 47 payload preparation fell from 8,347 ms
to 7 ms with the retained ROOM chunk. Core preparation took 2,548 ms, but
progress archive application took 6,213 ms; total restore was 9,123 ms and
returned success. The user confirms the red Loading failed warning remains,
with faster loading afterward. This does not establish which archive operation
blocked: slot lookup, open, unbuffered write and close all share that interval.

Add native-only timings around lookup, open, buffering, write and close,
emitting one line after those calls finish. Keep persistence and completion
ordering unchanged. This diagnostic requires another manual load and log export
to identify the blocking operation before altering the handoff.

Source tracing found that both registered filename lookup passes call the
Pocket GETFILE implementation, which first performs a synchronous four-byte
DS_CMD_READ. Slot lookup and fopen can therefore issue host commands before
LOAD acknowledgement despite the archive write being memory-backed. Pocket
slot IDs remain fixed during this launch, so kernel lookup now trusts registry
hits there. MiSTer and sim retain live stale-mapping validation. Lookup misses
still use the existing discovery path; archive persistence and transactional
restore ordering remain unchanged.

An executable extracted-source regression covers both registered lookup passes,
full-name ordering, misses and stale dynamic mappings on all three targets.
It fails before the Pocket guard and passes afterward. Rebuild only the OS
firmware; the FPGA bitstream, boot ABI and app service layout are unchanged.
The diagnostic app retains split archive timings for hardware attribution.

Verification: snapshot suite (including transactional archive fault cases),
lookup regression, async-save and timeout regressions, and lint pass. The
native ELF contains the archive timers. The firmware rebuild passes its contract
check; packaged OS CRC and boot ABI match the unchanged runtime contract.
The regenerated patch applies cleanly to the pinned OS revision. Package
`butterscotch-load-handoff-test.zip` contains only `Assets/butterscotch/common/`
`os.bin` and `undertale.elf`, with no Saves or bitstream replacement. The user
must manually test load and export the log: warning removal and latency remain
unverified on hardware.

### Pocket verification of filename-query fix

The user reports no red Loading failed message and fast loading with the
load-handoff update. The exported log's latest run receives a 238,293-byte
state during the intro (room 1), restores room 47 and returns load ok in
2,977 ms after the request; this excludes OS and game startup. Room payload
preparation takes 6 ms. Archive timings are lookup 0, open 0, buffering 0,
write 1 and close 0 ms; complete archive application is 12 ms, compared with
6,213 ms in the previous run. All section application takes 24 ms. This
verifies successful post-startup restore and removal of the reported red
warning on this device run. The log is a startup restore, not a separate
measurement of manual loading or the full sleep/wake interval.

### Rebase runtime dependency onto 865d627

At the user's request, reapply the existing openfpgaOS patch onto revision
865d6277b0be77daaf1f3b998d18fe2445b0f649 and regenerate its diff against that
base. The patch applies without conflicts. Upstream changes wake presentation
to Resuming... in both the BRAM bootloader and OS; the existing capture,
restore, file-handoff and lookup changes remain in the patch. Update the
README's reproducible dependency pin. Preserve the previous hardware-tested
checkout and runtime. This dependency update requires fresh bootloader/OS
verification; previous Pocket results were obtained on the earlier base.

Verification on the new base: patch replay reproduces every dependency source
change byte for byte. The firmware container builds the bootloader and OS;
OS CRC, Resuming... string and generated bootloader MIF pass checks. Runtime
async-abort, CRC/host-timeout and three-target filename-lookup regressions pass.
The snapshot suite, restore-completion/ownership tests and lint pass. App API
headers match the hardware-tested dependency. No new bitstream is assembled or
installed; packaging the quiet bootloader requires updating the boot MIF in a
matching fitted bitstream. Pocket validation remains pending for this base.

### October 13: repeated sleep/wake investigation

The rebased 90 MHz control captured a fresh 239,171-byte sleep state before
wake; the user confirmed the exact resumed position, and the log received that
payload successfully in 2,998 ms after startup. The matched 100 MHz build then
captured a fresh 239,354-byte sleep state at 15:54:42 America/New_York with a
valid OFSS CRC. After two more sleep/wake attempts, the entire sleep file and
its timestamp remained identical, and the user returned to the initial sleep
position. This establishes a repeated-cycle failure in that device run, not a
universal inability to capture at 100 MHz. Negative CPU setup timing remains;
its worst path is Decode_UOP to SrcPlugin_SRC1, not proof of the sleep cause.

The previous desktop checks did not model two complete Pocket cycles. Added
three successive boot-LOAD/next-SAVE handshakes to the actual bridge/controller
RTL harness, recreating the FPGA model each time rather than only toggling POR.
The expanded harness passes 104 checks. It models requests, readiness, declared
sizes, counters and rearming; CPU serialization and proprietary host SD file
selection/persistence remain outside the RTL fixture. Container Verilator
required compatible warning flags, not RTL changes. Desktop ASan/UBSan checks
also cold-restore, transition room 46 to 47, recapture, cold-restore that state
and recapture again successfully. These tests do not reproduce the device bug.

Native diagnostics now record live CPU frequency, enabled state and request/size
registers. Before announcing save readiness (or refusal), the app writes the
capture/refusal log to its existing spare log slot. Successful loads also dump
the log after acknowledgement. Diagnostic write failure is ignored and must not
change request success or completion ownership. The completion test fails before
this audit exists and passes afterward, including successful SAVE despite failed
audit and retries during rejected receive/old-state cleanup. Persistence on sleep
and diagnostic overhead still require hardware measurement. No sleep fix is
claimed; compare first and second captures in the next two-cycle run.

Verification: two cold-process application restore/capture cycles preserve the
future frame byte for byte, including a room transition; ASan/UBSan report no
errors. The complete snapshot suite, completion/commit tests, lint and native
ELF build pass. The envelope checksum is logged as a state identity for capture
and receive. Runtime and FPGA bitstream are unchanged for this ELF-only test.
The actual Pocket host is not simulated, and no claim is made that this fixes
the repeated-cycle failure. Native log-slot persistence and audit time are the
next device measurements.

Review identified a transport hazard in a proposed post-ACK LOAD dump: a new
blob could arrive while NV-slot I/O holds CRAM0 and overflow the unbackpressured
bridge FIFO. Removed that write before packaging. Automatic audits now occur
only before SAVE readiness/refusal, with handles closed before acknowledgement.
LOAD trace remains in the in-memory log for the user's ordinary Save log action.


### October 13: second sleep reaches capture but audio refuses

The diagnostic card log at 16:25:48 matches sleep envelope ID `42cf9a7c`.
The fresh 100 MHz capture produced 242,156 bytes and woke successfully from
that exact payload. The subsequent sleep request reached the app in room 45,
and game serialization succeeded (178,440 bytes), but audio capture refused
with "Audio cannot be captured safely". The unchanged sleep file is therefore
consistent with an explicit refused second capture, rather than a missing
request. This evidence does not identify clock frequency as the cause, and
repeated physical 90 MHz cycles have not been verified.

Audio capture now logs the specific rejected safety check, ring capacity,
free/unread/reserved pairs, accepted-write count, PCM shadow availability and
encoding allocation size; rejected voice cursors include their track, slot and
buffer bounds. Safety checks and the save format are unchanged. The sanitizer
fixture exercises two further PCM capture/restore cycles with Pocket's reserved
pair and requires queue-shadow refusal diagnostics. This fixture does not
simulate hardware playback or allocation pressure; the device refusal is not
yet reproduced, and no fix is claimed. The next ELF-only run should distinguish
queue accounting, invalid voices and allocation failure before changing behavior.


Read-only runtime/RTL review found a candidate for queue-shadow refusal: Pocket
`audio.c` resets the voice's position but `audio_mixer.v` updates its readback
`pos_latch` only on advancement. The saved audio has three unread PCM pairs,
below the stream's four-pair advancement threshold. A stale position may thus
produce a phantom backlog, suppress replenishment and violate the shadow's
accepted-write bound. The saved streamed voice itself satisfies cursor bounds.
A refusal with a small accepted-write count and a large reported queue would
support this mechanism. This is a source-level hypothesis, not a verified fix;
no runtime or RTL behavior has been changed for this diagnostic.


### October 13: phantom audio backlog reproduced in actual mixer RTL

The next device audit identifies queue-shadow refusal directly: capacity
131072, free 15102, queued 115969, reserved 1, written 3, shadow present, zero
bytes encoded. The first capture (ID `99da25ff`, 241843 bytes) and its wake load
succeeded; the second game capture succeeded and audio refused. Allocation and
voice-cursor checks are not the cause of this particular refusal.

Added `test_stream_restore_position_readback` to the real mixer harness: play
until position is nonzero, deactivate, queue a reset to zero, restore three PCM
pairs, verify real position and registered readback, then refill and repeat.
Before the change the voice table correctly holds zero but readback stays 253,
producing a phantom backlog. Exactly six new assertions fail across two cycles
(183 pass). Updating `pos_latch` on a non-volume queued CPU write's S_IDLE
commit makes all 189 mixer checks pass, including PCM and memory-stall parity.
The existing registered read latency and queued-write ownership are preserved.
The patch applies cleanly to `865d627` and the resulting source/test match the
verified clone byte for byte. This reproduces a frequency-independent RTL bug;
Pocket behavior and a new fit's timing still require verification.


The rebuilt 100 MHz seed-8 test completed synthesis, fitting, assembly and
STA successfully. Setup timing remains unmet: worst setup slack -0.859 ns
(previous 100 MHz test -0.788 ns). This package is an experimental hardware
check of the reproduced sleep bug, not a timing-qualified release. The mixer
readback storage still infers a 32x22 MLAB with registered readback. Packaging
contains only os25.rbf_r, the matching rebased os.bin and the diagnostic ELF;
archive contents are byte-checked and contain no progress saves. Artifact:
`out/butterscotch-os25-100-audio-reset-test.zip`, with hashes and verification
limits in `out/os25-100-audio-reset-proof.json`. Repeated Pocket sleep/wake
verification remains pending.
