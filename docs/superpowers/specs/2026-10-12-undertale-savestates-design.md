# Undertale save states and sleep

Approved scope: Undertale first, exact overworld and menu restoration, with a
visible refusal when the live state cannot be restored safely. Battles and
dialogue are excluded initially. Deltarune remains unsupported.

## Existing decisions

- `src/openfpga/README.md`, Saves: progress files live in an archive in slot 0;
  packaging must never replace the player's progress archive.
- The Menu and Resolution sections: notices must be drawn into the RGB555
  framebuffer, at both resolutions, using the port's font.
- `.agents/skills/openfpgaos-platform-notes/SKILL.md`: stay on os25 for 640x480
  RGB555; stage a paired runtime and SDK instead of changing the SDK dependency.
- `Runner_dumpStateJson` is a diagnostic, not a complete restorable snapshot.
- The pico8pocket wake test established that the Pocket delivers the blob and
  load request before the app starts. The OS must use real elapsed time for its
  pickup deadline, and changes to os.bin require a matching bootloader patch.

## Architecture

Capture after Step, Draw, GUI and `Runner_handlePendingRoomChange`, before the
next input frame. No game script may be executing at capture or restore.
Store a versioned binary description of mutable state, never process pointers.
Immutable code, textures and lookup tables are reloaded from the same game data.
Preserve instance iteration order, shared array identity and array ownership.

The platform envelope contains separate runner, audio, progress-file and platform
sections. Platform state preserves performance mode and desktop test counters;
the latter cannot be inferred from runner frames across room transitions.
It carries the game-data identity, format version, section lengths and checksum.
The complete envelope must fit the service's 1 MiB window minus its header.
Declare the actual blob size for each save handover. Never truncate a snapshot.

The runner section contains globals, all live instances, scalar instance fields,
alarms, variables and reachable values; room identity and mutable visual state;
RNG, game-time offset, frame/instance counters; supported DS pools; renderer
settings; current and cached INI contexts, including paths and pending dirty
edits. Capturing an open INI does not close it or flush it to the progress file.
Derived object buckets, collision grids and draw caches are rebuilt.
Runner data is bounded before compression; the compressed envelope remains
subject to the service limit. The loaded game metadata and code are hashed at
startup and combined with a build-time fingerprint of the complete game file.
Unsupported resources cause refusal, including active physics, user surfaces,
pending async work, open game file handles, unsupported values or saved-room
resources without a restore path. Empty or dormant resources must not cause a
false refusal merely because a pool has allocated storage.

Audio carries logical voices, track IDs, exact decoder/resampler positions,
gain ramps, pause/loop state, stream metadata and the next voice ID. Pack data
and caches are rebuilt. Progress-file contents are part of the state so the
restored runtime cannot observe newer files while its variables are older.

## Refusal and load behavior

The first game profile permits ordinary overworld play and menus. It denies
battle/dialogue/cutscene state and transitions. The writer checks every resource
it encounters; eligibility is not a promise based only on a room name.
Draw a specific message (for example, "Can't save during dialogue") and report
failure to the Pocket. Hardware testing must establish the Pocket's behavior
when sleep is refused; do not claim that refusal keeps the device awake until
observed. Display readiness in the port menu so the player can check before sleep.

Parse, validate and allocate a restore plan before changing any live state.
Reject bad checksums, lengths, versions, game identities and resource references.
Commit the prepared plan at the frame boundary without firing Create, Room Start
or gameplay events. Rebase wall-clock fields and suppress stale input edges.
If a restore cannot be prepared, the running game continues and the failure is
visible. The Pocket boot screen during wake is expected.

## Verification

Unit tests cover shared arrays and ownership, instance order and alarms, RNG,
corruption/truncation, incompatible identities, refusal without mutation, and
allocation cleanup. Desktop game tests compare a continued run with a restored
run, including a restore in a new process, matching future frames and logical
state. Verify both overworld and menu snapshots and explicit battle/dialogue
refusals. Compare audio after restore and archive contents. Run lint and ASan.

Build os25 with the tested save-state service, inspect setup and hold timing,
and stage its bitstream, os.bin and SDK as one runtime. On the Pocket verify
fresh launch, Memory save/load, sleep/wake, and visible refusal. Desktop tests
cannot establish FPGA timing, SD behavior or sleep refusal UI.
