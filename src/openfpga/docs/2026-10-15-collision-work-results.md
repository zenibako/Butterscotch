# Collision dispatch measurement decision — 2026-10-15

Defer both a maintained sorted-population optimization and a generation-keyed
collision target cache. The sampled dispatcher sorts at most two targets. The
recreated spade chase performs no collision-event dispatch sorting, despite
having nine active spades. These counts do not establish the cost of other
collision APIs, a CPU bottleneck, saved cycles, or a Pocket speedup.

This executes Task 2 of the [collision measurement plan](../../../docs/superpowers/plans/2026-10-15-deltarune-collision-measurement.md),
within the [shared optimization design](../../../docs/superpowers/specs/2026-10-15-deltarune-optimization-design.md).
The existing [tile-cache results](2026-10-14-tile-cache-reuse-results.md) motivate
investigating the spade scene's Step work, but do not attribute it to sorting.
Source under test: `55d9d22d84557920974b56a6708edfa83540568c`, parent `f9d5442`.
No cache, production behavior change, or deployment is part of this result.

## Method and scope

A private host adapter samples the actual `dispatchCollisionEvents` path, calls
`Runner_takeCollisionWork` immediately after each completed `Runner_step`, and
writes sampled sorted target lists, actual collision callbacks, and game-event
code entries. Lists contain the full sorted `(objectIndex, instanceId)` pairs;
equality is an exact tuple-sequence comparison, with target object as part of
the key. No hash is used to decide population equality. The callback serial is
recorded alongside every list, so a repeat after a callback is distinguishable
from a repeat with no intervening callback.

Completed-frame labels are `runner->frameCount + 1` before stepping, matching the
harness's `FRAME_TARGET=first_frame-1` convention. They are not desktop dump
frame indices. Captures occur after drawing; raw RGB555 is saved before host
notices/overlays. State uses `Runner_dumpStateJson`, which is a diagnostic view
of instance fields, self variables, globals and tiles, not a complete snapshot
serialization oracle. Event records cover `executeCode` entry in Runner,
including event context; they are not an opcode trace of every nested VM call.

All routes use seed 7, fresh disposable directories and volatile benchmark
saves, `BS_UNCAPPED=1`, `BS_NOFLIP=1`, SDL dummy video/audio, and the existing
`--bench` (Accuracy) or `--bench-smooth` (Speed) route. There is no frame pacing
or display flip. Ordinary routes retain their existing benchmark diagnostics;
these are diagnostic runs, never timing measurements. ASan/UBSan PC objects
were reused only within matching WAD16/WAD17 and diagnostic-on/off configurations,
with at most two compiler jobs. Every temporarily edited source was explicitly
forced through make, using absolute core paths and relative platform paths.
The PC flags include `-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined
-fno-sanitize=alignment,function`; on builds additionally define
`OF_COLLISION_WORK_PROFILE`. Native-script and other PC behavior use the existing
Makefile defaults; this is separate from the coordinator's RV32 adapter.

The spade route is a controlled recreation, not imported player progress:
room 43 at input frame 30, globals at frame 29
`darkzone=1,char[1]=0,char[2]=0,plot=60,interact=0`, and two-frame Z presses at
40,55,...,475. It follows `experiments.py::SCENES`; benchmark draw notes,
draw timing and VM profiling are disabled before its steps. At frames
490/491/492 it has 9/9/9 active spades, 9/8/8 visible spades by the harness's
camera-rectangle test, and zero battle controllers. Each sample asserts room
43, active and visible spades, and absence of battle controllers.

## Per-frame work

Accuracy and Speed have the same counts in every row. `targets` sums the input
lengths of actual qsort calls, including any self/inactive entries; it does not
count successful collisions. `self/other boxes` count actual bbox calculations
in this dispatcher, including compatibility rechecks. Comparator counts are
host libc/ASan qsort observations and must not be projected onto RV32 qsort.

| Scene and completed frame | Sorts | Targets | Comparisons | Self boxes | Other boxes | Length histogram: 0 / 1 / 2–7 / 8–31 / ≥32 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Field 2800, 2801, 2802, each | 1 | 1 | 0 | 1 | 1 | 0 / 1 / 0 / 0 / 0 |
| Attack meters (`bmenu`) 3120, 3121, 3122, each | 0 | 0 | 0 | 0 | 0 | 0 / 0 / 0 / 0 / 0 |
| Dodge 3323 | 2 | 4 | 4 | 3 | 4 | 0 / 0 / 2 / 0 / 0 |
| Dodge 3324 | 2 | 4 | 4 | 4 | 4 | 0 / 0 / 2 / 0 / 0 |
| Dodge 3325 | 2 | 4 | 4 | 2 | 4 | 0 / 0 / 2 / 0 / 0 |
| Recreated spades 490, 491, 492, each | 0 | 0 | 0 | 0 | 0 | 0 / 0 / 0 / 0 / 0 |
| Flowey 2400, 2401, 2402, each | 5 | 5 | 0 | 2 | 5 | 0 / 5 / 0 / 0 / 0 |

Across the 15 sampled frames in either mode: 24 sorts, 30 input targets,
12 comparisons, 18 self boxes and 30 other boxes. Eighteen sorts have length one;
six have length two. No empty or longer sort occurs. These are bounded samples,
not whole-route rates. Zero work in this dispatcher does not rule out work in
GML collision builtins, spatial queries, or other overlap functions.

Image inspection and captured instance populations confirm the actual labels:
field walking in `room_field2` (room 52); attack meters with `obj_attackpress`
and `obj_burstbolt`; enemy turn with `obj_heart`, `obj_grazebox` and regular
bullets; `room_dark_chase2` (43) for spades; `room_floweybattle` (307) for Flowey.
The two battle samples still report room 52 because the battle is layered into
that room.

## Exact repeated populations and callbacks

Both modes and diagnostic settings produce the same exact list sequence.

- Field: target 30 is `[(30,101244)]` for self 101248 on all three frames.
  There are two exact repeats across frames and no collision callbacks.
- Dodge: target 201 is `[(203,104494),(203,104496)]` for selves 104472 then
  104471 at frames 3323 and 3324. At 3325 it becomes
  `[(203,104496),(203,104500)]` for both selves. Of the five transitions between
  these six same-target lists, four are exact repeats and one changes membership.
  Three repeats are within a frame and one is across frames. Callback 28 at
  3323 and callback 29 at 3324 occur between equal lists, so two exact repeats
  span callbacks. Callback 30 occurs before the changed 3325 list. The observed
  sequence change proves that a population cannot simply be reused throughout
  this three-frame sample without invalidation. It does not prove that callback
  30 caused the membership change; ordinary game events also run between frames.
- Flowey: the five targets 749,759,760,761,762 each have one stable member:
  `(749,113844)`, `(759,113845)`, `(760,113847)`, `(761,113848)`,
  `(762,113846)` respectively. Each target repeats exactly across the next two
  frames: ten repeats, no collision callbacks. The selves are 113944 for target
  749 and 113844 for the other targets.
- Attack meters and recreated spades have no target snapshots from this path,
  so no repeated-sort opportunity can be inferred there.

Observed callbacks are supplemental to the committed asset-free fixture,
which remains the independent ordering/mutation oracle. It covers parent/child
ordering and handler precedence, later-self visibility of created/changed
instances, destroy/deactivate/type-change suppression, bbox changes and moving
self during callbacks, pending-room callback suppression, nested snapshot-arena
relocation and nested Runner dispatch. Those cases prevent treating a stable
sample as an invariant about all callbacks.

## Diagnostic on/off and repeat checks

All twelve retained runs exit normally: three routes × two modes × on/off.
Across the six on/off pairs, all 30 sampled raw RGB555 frames are byte-identical.
The exact ordered target lists, callback tuples, game-event code entries, room
labels and dimensions agree. Per mode, the field/attack/dodge route records
96/146/166 event-code entries and 0/0/3 collision callbacks; spades records
98 entries and no callbacks; Flowey records 51 entries and no callbacks.

Deltarune's 24 JSON state captures also match byte-for-byte. Flowey's six
on/off state comparisons differ only in `obj_time` instance 100000's
`selfVariables.idle_time`. Removing only that wall-clock field makes the parsed
JSON states equal. Raw Flowey state equality is not claimed.

The same diagnostic-on ELF was repeated independently for all three Accuracy
routes. Its counts, ordered event/list streams, 15 raw pixel frames and the
same scoped state comparison agree. The repeated Deltarune route and Flowey
produce different accepted-PCM pair/hash records; Flowey also changes
`obj_time.idle_time`. Thus uncapped desktop audio and this clock field are
nondeterministic even without a binary change. All six retained on/off PCM
records differ. Host PCM is excluded, with no byte-identity claim. The
coordinator's separate fixed-audio QEMU/RTL matrix supplies the ordinary
shared changes' raw PCM and bounded replay checks; see the completed combined
result below. No collision cycle cost or speedup is inferred from host counts.

These real-game sanitizer runs are not sanitizer-clean: both diagnostic settings
report the same signed-shift warning at vendored `stb_ds.h:1132:67`; the recreated
spade route additionally reports a float-to-int range warning at unmodified
`rvalue.h:569:36`. The retained runs have no AddressSanitizer memory finding or
null-string failure. Earlier adapter attempts exposed null string/name handling
in the generic JSON dumper; private formatting substitutes placeholders for
those nullable strings/keys. That workaround is not committed to production.
Several earlier host launches stalled before sampled output; their disposable
cases are retained, and the final launcher uses a 30-second per-run timeout.
One stalled r2 process was terminated; r4 timeout failures were retained.
These failures are excluded from the completed r5 evidence, not counted as passes.

## Decision, memory and next experiment

Keep the current dispatcher. Even perfect elimination of the measured sorting
removes only the reported calls/comparisons; their cycle cost is unmeasured.
The most frequently sorted Flowey populations are singletons. The only
nontrivial repeated sort has two entries and changes membership during the
sample. This evidence does not justify persistent cache storage or more mutation
maintenance. No follow-up cache design is selected or implemented.

The guarded diagnostic record costs 80 bytes per Runner plus one comparator
context pointer. Ordinary builds add neither. A future retained-vector cache
would need at least `4*N` bytes per populated vector on RV32 plus capacities,
keys and invalidation metadata; its benefit cannot be established from these
small N values. Maintaining existing bucket order could avoid a copied vector
but would add work and ordering requirements to every relevant mutation.
Neither alternative is free.

Any future proposal must first enumerate and verify add/remove/clear paths
(`Runner_addInstanceToObjectLists`, `Runner_removeInstanceFromObjectLists`,
`Runner_clearAllObjectLists`), object changes and ancestry effects, destruction,
activation/inactivation semantics, room/reset cleanup, and snapshot restore's
object-list rebuild. A cache must own storage: `instanceSnapshots` is a nested,
relocating scratch arena, and pointers into it cannot survive pop/reallocation
or become a frame-wide cache. No generation scheme is claimed complete here.
Bounding-box caching stays deferred: callback-driven position, sprite, mask,
scale and related changes already invalidate simple reuse, and these counts do
not measure their isolated cost.

The ordinary macro-off combined candidate passes the parent/candidate dodge
3323–3325 replay and full shared correctness matrix, described below. This is
a bundled timing control, not an isolated collision timing measurement.
Task 1's RV32 disassembly comparison found identical
2240-byte dispatcher and 128-byte comparator text, so no instrumentation speedup
is expected. That is scoped machine-code evidence, not a linked-ELF timing result.

If target profiling later identifies sorting cost worth pursuing, first measure
an isolated RV32 same-ELF operation case: preserve the observed singleton field
and five Flowey target lists plus the two exact dodge populations above; run a
fixed number of ordinary snapshot/copy/qsort operations with length 0/1/2 and
with the callback-order fixture's larger populations; write the count and checksum
after the measured batch. Compare the unchanged path with a separately reviewed
proposal under identical compiler, libc, alignment, cache and memory conditions,
and report raw cycles plus harness overhead. Preserve the fixture's expected
callback sequence as the oracle, including mutation between selves. Only then
repeat actual frames and, if justified, a Pocket base/candidate/base comparison.
A microbenchmark gain alone cannot establish a scene speedup.

## Private evidence and reproduction

Private evidence is outside Git at:
`butterscotch-pr20-fixes/.superpowers/sdd/2026-10-15-deltarune-collision-measurement/task-2-evidence/`
under the shared project root. Retained cases are `r5-{deltarune-route,
deltarune-spades,undertale-flowey}-{accuracy,speed}-{on,off}`; same-ELF repeats
are `repeat-1-r5-...-accuracy-on` for each route. Assets are read-only symlinks
to the verified harness inputs, never staged. The directory contains:

- `before/`, `instrumented/`, final `instrumentation.patch`, and `cw-private.h`;
- `build-r5-*-*.log` with exact compiler commands; matching explicit forced-source
  arguments are in `build-r3-*-*.command` (r5 uses those same arguments);
- per-run `command.json`, `run.log`, raw pixels and JSON state;
- `cw-run-r5.py`, `cw-repeat.py`, `cw-analyze.py`, `cw-verify.py`;
- `r5-analysis.json`, `verification.json`, binary/source/patch hashes in
  `retained-manifest.json`, filtered `nm` output, and `restoration.json`;
- earlier failed/revised runs and patches, retained under their original names.

To reproduce, start from `55d9d22`, apply the exact private patch with its header
at the recorded absolute path, use the saved make arguments for each matching
object directory with the listed `-W` sources, and run the launcher from a fresh
evidence namespace. Each `command.json` also independently specifies the exact
binary, environment and arguments, with its run directory as cwd. Do not rerun
into an existing case. Run `python3 cw-analyze.py r5` and `python3 cw-verify.py`
on the retained evidence to reproduce the count/equality decision. The verifier
checks exact counts and byte pixels, scopes the single state-field exclusion,
and records PCM differences. Binary hashes bind these captures to the retained
executables; source and patch hashes bind the private adapter.

After capture, all four temporarily edited production sources were restored from
preserved copies and compared byte-for-byte with committed `55d9d22` contents.
No diagnostic adapter, raw game material, cache implementation or SDK change is
included in the report commit.

## Completed ordinary-build integration gate

The exact combined code `2ee841c` passes the shared 32-ELF matrix against
source-equivalent baseline `a6ea5c0`: 64 repeat QEMU captures, 32 positive quiet
RTL replays, 32 rejected negatives, 16 cross-revision and 16 trace/timing raw
RGB555/PCM/pair comparisons, plus eight exact timing-ELF scanout replays.
`OF_COLLISION_WORK_PROFILE` is absent from every selected build's source flags.
Trace RTL checks the dynamic PCM hash; timing excludes that field and checks
pair counts, with both build kinds' raw QEMU PCM compared. Full results and
hashed inventory are in the [combined results](2026-10-15-deltarune-optimization-results.md).

This closes the ordinary-build compatibility gate. Because the bundle also
changes audio and strings, its cycle deltas cannot be assigned to collision.
Retain the optional counters and semantic fixtures; defer a cache as above.
No physical Pocket gain or whole-game coverage is established.
