# Deltarune collision measurement implementation plan

> **For agentic workers:** Use the `executing-plans` skill to implement this plan task by task. Steps use checkboxes for tracking. Delegate only if the user authorizes it.

**Goal:** Determine whether repeated collision sorting or bounding-box work merits optimization and capture the mutation/order contract needed to change it safely.

**Architecture:** Compile optional integer counters into collision dispatch and add synthetic callback-order fixtures against the actual runner. Ordinary builds remain free of these counters and emit no new logging. Cache implementation is a separate decision after this plan's measured result.

**Tech Stack:** C, current Runner/VM native test callbacks, asset-free fixtures, desktop diagnostic routes and os25 timing controls.

**Spec:** [Shared design and verification protocol](../specs/2026-10-15-deltarune-optimization-design.md).

## Global constraints

All shared-design constraints apply. Keep target ordering by object index then
instance ID, descendant-inclusive target populations, nested snapshot-arena
lifetime, active-instance checks, room-change exits and callback mutation
visibility. Do not cache bounding boxes or change collision algorithms here.
Do not infer recoverable cycles from call counts alone.

### Task 1: Characterize collision order and collect optional work counters

**Files:** Modify `src/runner.c#dispatchCollisionEvents`, `src/runner.h` and
`src/openfpga/Makefile`; create `src/openfpga/tests/collision-work.c`.

**Interfaces:** Under `OF_COLLISION_WORK_PROFILE`, define the following type,
add a `collisionWork` field of that type to `Runner`, and add a take/reset
function. Nothing is serialized; counters are derived diagnostics.

```c
typedef struct {
    uint64_t sorts, targets, comparisons, selfBoxes, otherBoxes;
    uint64_t targetLengths[5]; /* 0, 1, 2..7, 8..31, >=32 */
} RunnerCollisionWork;
void Runner_takeCollisionWork(Runner *runner, RunnerCollisionWork *out);
```

  Count sorts/lengths immediately before the existing qsort; increment the
  comparison count through a diagnostic-only comparator path. If comparator
  context requires a temporary active-runner pointer, save/restore it around
  qsort and test nested dispatch. qsort itself executes no GML callbacks.
  Count bbox calculations at the actual self/other call sites. The take
  function copies all fields and zeros the runner's counters.

- [ ] Create an asset-free fixture that includes the real `runner.c` and links
  PC objects excluding `main.o` and `up/runner.o`, matching the existing include
  fixture pattern. It can call the private `dispatchCollisionEvents` directly.
  Use `VMContext.nativeCode` test callbacks to record collision `(self, other)`
  IDs while exercising production dispatch and object-list maintenance.
- [ ] Establish the unmodified dispatch's expected callback sequence for:
  mixed parent/child targets with reversed insertion order; multiple self
  instances; a callback creating a target; destroying/deactivating a later
  target; changing an object's type; moving/resizing a target before a later
  collision; a pending room change; and nested instance queries that push/pop
  the snapshot arena. Check the arena's final length as well as callback order.
  Preserve observed later-self visibility; do not impose a new phase-wide
  snapshot policy.
- [ ] Add zero/one/many-target counter assertions. Require one take operation
  to clear counters and a second take to return all zeros. Baseline behavioral
  tests should pass; the new counter API tests initially fail to compile.
- [ ] Implement guarded counters without extra clocks, allocations or log calls
  inside pair/comparator loops. Never change the production comparator path.
  Diagnostic samples are for counts, not for timing comparisons.
- [ ] Add `collision-work-fixture` emitting
  `$(PC_BIN)_collision_work_fixture`. Build/run it for WAD16 and WAD17 using
  fresh PC object directories and the existing sanitizer flags. Set the WAD
  version explicitly per fixture build. Run the callback-order cases with the
  macro both enabled and disabled; compile counter-specific assertions only
  when it is enabled.
- [ ] Run the existing snapshot suite and both games' port lint. Inspect normal
  ELF symbols/disassembly to confirm profiling calls/strings are absent when
  `OF_COLLISION_WORK_PROFILE` is undefined.
- [ ] Commit as `test(runner): measure collision dispatch work and ordering`,
  with a GPG signature.

### Task 2: Produce the measured decision

**Files:** Temporary diagnostic driver in the private harness/desktop case;
create a dated `src/openfpga/docs/2026-10-15-collision-work-results.md`.

**Interfaces:** Read `Runner_takeCollisionWork` after completed frames in a
diagnostic-only build. Produce per-scene counts, bucket-length distributions,
fixture evidence and a proceed/defer decision; no cache is delivered here.

- [ ] Instrument a private desktop loop adapter to call the take function after
  dispatch and save counts for Deltarune field/menu/dodge, the recreated spade
  chase, and Undertale Flowey. Use the existing fixed seed/input/scene setup;
  do not relabel a recreated scene as imported gameplay. Write reports outside
  the timed region. Ordinary timing builds do not include this adapter.
- [ ] Count repeat populations with a private diagnostic record of target
  object plus the full sorted `(objectIndex, instanceId)` sequence. Compare
  sequences exactly before calling them equal; a hash match alone is not proof.
  Record callbacks between repeats and whether membership/order changed.
- [ ] Verify diagnostic-on/off frame, PCM and event-order agreement on the
  collected routes. Run the shared first-stage ordinary parent/candidate timing
  control to detect accidental cost in a normal build; no speedup is expected
  from instrumentation that compiles out.
- [ ] Write a decision supported by counts: defer if work is rare/small; propose
  maintained sorted populations if membership changes rarely and sorting is
  frequent; propose a generation-keyed cache only after enumerating every
  relevant mutation/reset/restore entry point, including
  `Runner_addInstanceToObjectLists`, `Runner_removeInstanceFromObjectLists`,
  `Runner_clearAllObjectLists`, object changes, and snapshot rebuilds. Address
  scratch-arena lifetime and cache memory cost explicitly.
- [ ] Identify the exact workload and isolated cycle experiment that would test
  the selected proposal. A callback-order fixture must remain the reference;
  do not substitute a cached implementation as its own oracle. Bounding-box
  caching remains deferred unless separate evidence establishes its cost and
  every position/sprite/mask mutation can invalidate it correctly.
- [ ] Commit the report as `docs: record collision dispatch optimization decision`.
  If evidence supports a cache, prepare its concrete follow-up design/plan for
  review; do not silently widen this measurement task into implementation.
