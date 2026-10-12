# Undertale Savestates Implementation Plan

> For agentic workers: execute the tasks with tests and review before integration.

**Goal:** Exact native Memories and sleep for safe Undertale overworld/menu states.

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
