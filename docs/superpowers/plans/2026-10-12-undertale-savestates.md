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

- [ ] Write tests for scalar/array graphs, shared array ownership, globals,
  instance IDs/order/alarms, RNG, malformed input and unsupported-resource refusal.
- [ ] Run them against the missing implementation and record the failure.
- [ ] Implement bounded binary encoding; prepare into an owned temporary graph;
  rebuild indices and visual state without gameplay events during apply.
- [ ] Run unit tests and an ASan build; check cleanup after rejected input.
- [ ] Review the codec and integration contract before platform use.

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
void utSaveFsApplySnapshot(FileSystem*, void*);
void utSaveFsFreeSnapshot(void*);
```

- [ ] Test archive rollback and malformed archive rejection; test voice cursor,
  decoder/resampler state, gain ramps and pause/loop flags across a restore.
- [ ] Capture pointer-free logical state; prepare cache/file backing before apply.
- [ ] Commit prepared state without allocations; preserve backend identity.
- [ ] Compare continued audio and archive contents before/after restore.

### Task 3: Envelope, frame boundary and visible refusal

Files: `src/openfpga/platform/ut_savestate.c`, `ut_savestate.h`,
`of_platform.c`, `of_hooks.h`, `src/loop.c`, `src/openfpga/main.c`, Makefile.

- [ ] Test envelope lengths, checksum and game identity; reject before mutation.
- [ ] Hash game data at startup; cache identity for later snapshot requests.
- [ ] Poll at the completed frame boundary, prepare all three sections and apply
  together. Reset pacing and stale input after restore.
- [ ] Add explicit Undertale overworld/menu eligibility and specific notices;
  expose readiness in the port menu. Keep Deltarune refused.
- [ ] Add desktop capture/restore controls and cold-process frame comparisons.
- [ ] Run overworld/menu comparisons and battle/dialogue refusal tests.

### Task 4: Paired os25 runtime and delivery

Files: staged runtime outside the stock SDK; core definition, build settings,
packaging scripts and `src/openfpga/README.md` in this repository.

- [ ] Enable the service in os25 in the openfpgaOS fork; simulate handshake.
- [ ] Fit os25 and inspect setup/hold reports, sweep if a fit is unsuitable.
- [ ] Stage SDK, bitstream, os.bin and matching bootloader together.
- [ ] Build Undertale, run lint/ASan and existing frame comparisons.
- [ ] Package a separately named test tree/zip and report measured limits.
- [ ] After hardware validation, commit all changes with signing; leave external
  PR text and publishing decisions to the user.

## Progress

Approved scope recorded; implementation not started. Task tracking is here because
the configured GitLab work-item lookup is unavailable from this sandbox.
