---
name: butterscotch-pocket-build
description: Build, package and deploy the Undertale port for the Analogue Pocket (Butterscotch on openfpgaOS) in this repo. Use this whenever the task involves building src/openfpga, running make here, producing the SD card tree, copying to the Pocket's SD card (from the build machine or another one), regenerating textures.bin or music.bin, the v0.9 comparison cores, or committing work in this repo — even if the user only says "rebuild it", "copy it" or "is it ready to copy?".
argument-hint: "[checkout: path or host:path with game data + toolchain] [card: SD mount path, and which machine]"
---

# Building and deploying the Undertale Pocket port

## Inputs

This skill names no machines. Take these from the arguments; if one is
missing and the task needs it, ask the user once for it and use what they
say for the rest of the session, rather than guessing from earlier sessions:

- **checkout**: where the working checkout, the game data and the Pocket
  toolchain are. A local path, or `host:path` when it is on another machine
  reached over SSH. Commands below write it as `$CHECKOUT`.
- **card**: where the Pocket's SD card is mounted, and on which machine
  (for example `/Volumes/Pocket` on a Mac). Commands below write it as
  `$CARD`.

## Where things are

```
undertale-pocket/                 not a git repo; holds the pieces below
├── env.sh                        source before any RISC-V build on macOS
├── butterscotch-pocket/          THIS REPOSITORY: fork of ButterscotchRunner/Butterscotch; trunk is `main` (it was `pocket` until 2026-10-10)
│   ├── src/                      Butterscotch itself; src/sw is the software renderer the port draws with
│   └── src/openfpga/             THE PORT
│       ├── main.c, Makefile, README.md
│       ├── platform/             of_* glue: video, audio, saves, log, bench
│       ├── tools/                mktexpack.c, mkmusic.c, mkcompare.sh, sdcopy.sh
│       ├── dist/                 Pocket core definition (core.json, data slots, instance JSON)
│       ├── data.win, music/      the user's own game data (gitignored)
│       ├── textures.bin, music.bin   generated packs (gitignored)
│       └── out/                  everything the build writes (gitignored):
│           └── build/pocket/butterscotch/   assembled SD card tree
├── openfpgaSDK/                  the openfpgaOS SDK, read only; SDK_ROOT points here by default
├── Diablo/                       reference port; also the source of the v0.9 SDK + runtime
└── screenshots/                  captures sent to the user
```

**Which machine.** Build in `$CHECKOUT`; the card may be on a different
machine from it. If `$CHECKOUT` is a `host:path`, run builds there over SSH
using the host name the user gave, since a `.local` name may not resolve
from every machine. A
cloud session has neither the game data nor a device toolchain, so it can
lint and build `undertale_pc` against a test game but cannot build the
Pocket ELF or check the real game's output.

Read `src/openfpga/README.md` first; it is kept current and describes each
subsystem. The SDK is a dependency, not part of this repository: never edit
it. Until 2026-10-08 the port lived inside a fork of the SDK
(`openfpgaSDK/src/undertale`, branch `undertale`) with Butterscotch cloned
inside it; that tree is superseded by this one.

## Building

```bash
cd undertale-pocket && source env.sh
make -C src/openfpga undertale_pc   # desktop binary only; never touches build/
make -C src/openfpga                # RISC-V ELF + packs + SD tree (v0.7 core only)
make -C src/openfpga compare        # the above + Benchmark entries + v0.9 cores
```

`env.sh` matters: it puts GNU sed on the PATH (the SDK scripts use GNU-only
`sed -i`) and sets `USE_SDK_CONTAINER=0` so the build uses Homebrew's
`riscv64-elf-gcc` instead of the SDK's Docker image, which cannot be built
here because Docker's keychain access fails in non-interactive sessions.

A change to any header under `src/`, `src/*/` or `platform/` empties the
object directories a run builds into (the Makefile keeps a checksum of the
headers in each one's `.headers`), so objects are never left built against an
old struct layout: that links without complaint and crashes on the device,
which happened twice before the check existed. Flag changes are still not
tracked. After changing `-D` flags, remove the affected objects first
(`<game>` is `undertale` or `deltarune`):

```bash
rm -rf src/openfpga/out/.obj/<game>-pc                              # desktop
rm -rf src/openfpga/out/.obj/<game> src/openfpga/out/.obj/<game>-v09   # device
```

The device build prints many warnings and notes; they are expected and not
a problem. What matters is that there are no errors. `make compare` output
should end with `Comparison cores added: ...`, and a build that prints
nothing after that line succeeded. To see only the lines that matter:

```bash
make -C src/openfpga compare 2>&1 | grep -E " error|undefined reference|Comparison|mkcompare:|\*\*\*"
make -C src/openfpga GAME=deltarune 2>&1 | grep -E " error|undefined reference|\*\*\*"
```

The first should print only the `Comparison cores added` line and the
second nothing.

## Two games, one core

The core is `zenibako.Butterscotch`, platform `butterscotch`, with one
entry per game on the Pocket. `GAME=deltarune` on any make target builds
Deltarune's program instead of Undertale's (WAD 17, data in
`games/deltarune/`, binary `deltarune_pc`); run
`tools/deltarune-setup.sh <game Resources folder> [chapter]` once first.
Every device build ends by reassembling the one SD tree from all games
built so far (`tools/mkcard.sh`), so a full card is `make && make
GAME=deltarune`. `compare` adds Undertale benchmark entries and v0.9
cores to that same tree.

Until 2026-10-08 the platform folder was `undertale`. A card set up before
then has `Assets/undertale/`, `Saves/undertale/` and possibly
`Cores/zenibako.Deltarune` with `Assets/deltarune/`; the save belongs in
`Saves/butterscotch/common/` now. Before the first launch of the new
core, copy the save files under `Saves/undertale/` there, or the game
starts as if nothing had been saved. Don't copy over a file that is
already there and newer: that one is progress made since.

## Keeping `src/openfpga/README.md` true

That README says what is here and how to use it, and nothing that a day of
work changes. It went out of date twice by carrying measurements and
walk-throughs of the renderer.

- **No measurements in it**: no frame times, load times, sizes or counts
  read off a run. Those go in the perf skill, dated and with the commit, or
  nowhere. A number may appear only as a definition (a frame is 33 ms) or
  next to the name that sets it (`MUSIC_RATE`, `TEXTURE_RESERVE_MB`).
- **No account of how the renderer does something**: that belongs in the
  comment over the code, and the README points at the file.
- **Update it in the same commit** as a change to anything a reader acts on:
  controls and debug chords, `make` targets, environment variables, file and
  slot names on the card, benchmark entries, what a log line means.
- Its outline comments and the sections marked "To be written" are the
  user's to write. Leave their wording alone; say so if one has gone stale.

## The cardinal rule: don't rebuild under a copy

`make` and `make compare` delete and recreate `src/openfpga/out/build/pocket/butterscotch/`. The
user often copies that tree to the SD card from another machine with rsync,
which takes a minute or more. Rebuilding mid-copy hands them a mixed tree.

- Use `make undertale_pc` for desktop iteration; it never touches `build/`.
- Before a device rebuild, check nobody is pulling:
  `pgrep -f 'rsync --server' | wc -l` must be 0.
- After rebuilding, tell the user the tree is ready and that you will leave
  it alone. If they said they are about to copy, do not rebuild until they
  confirm the copy is done.

## Getting it onto the SD card

Three routes, in order of preference:

1. **Card in the machine that has the checkout:** `make -C src/openfpga compare-copy`
   (or `copy` for the single core). This runs `tools/sdcopy.sh`, which waits
   for the card, mounts it, copies, removes `._*` sidecar files, verifies,
   and unmounts. It prints a specific message on each failure.
2. **Card in a second Mac:** SSH from the build machine
   cannot touch the card (macOS blocks removable volumes for remote
   logins; that restriction is the user's and is not to be worked around).
   Give the user these to run in a Terminal on that Mac:

   ```bash
   rsync -rc --exclude '._*' --exclude '.DS_Store' "$CHECKOUT/src/openfpga/out/build/pocket/butterscotch/" "$CARD/"
   dot_clean -m "$CARD/Assets/butterscotch" "$CARD/Cores" "$CARD/Platforms"
   diskutil unmount "$CARD"
   ```

   - `dot_clean -m "$CARD"` on the whole card prints "Operation
     not permitted" for `.Spotlight-V100`, `.Trashes` and
     `.TemporaryItems`. Those are macOS's own folders and the lines are
     harmless; the narrower paths above avoid them.
   - If the unmount is "dissented by" a `com.apple.Virtualization`
     process, the Claude desktop app has the card open for a session that
     was reading it. Once the copy has finished,
     `diskutil unmount force "$CARD"` is safe.
   - The rsync never deletes, so old benchmark ELFs and instance files
     stay on the card. That is harmless.
   - If `$CHECKOUT` is on a work branch, the user builds main
     with `git fetch <fork remote> && git switch --detach <fork remote>/main` after checking `git status` is
     clean, and switches back afterwards.
3. **Manual:** `cp -R src/openfpga/out/build/pocket/butterscotch/{Cores,Assets,Platforms} "$CARD/"`

**Checking what is on a card.** Every core's `core.json` says version
0.1.0, so it cannot tell builds apart. Compare file times against when the
change merged, `cmp` the ELF against a fresh build, or look for a string
the change added (`strings "$CARD/Assets/butterscotch/common/undertale.elf" | grep "Music only"`).

Plain `copy` after a `compare-copy` leaves stale v0.9 cores on the card; use
`compare-copy` again if those cores should stay current.

Unmount, never eject (`diskutil unmount`, as `sdcopy.sh` does): after an
eject a Mac does not see the card again until it is reseated, and
sometimes not then, and the user asked on 2026-10-09 for unmount to be
used so the card is found next time.

A card reader on an idle, headless Mac can fail to notice a card inserted
while the machine is idle. `sdcopy.sh` declares user activity to
wake it; if no disk appears at all, the card needs reseating.

## What is on the card

| File (Assets/butterscotch/common) | Slot | Source |
|---|---|---|
| `os.bin`, `undertale_os.ini`, `undertale.elf` | 1, 2, 3 | SDK runtime, dist, build |
| `data.win` | 4 | the user's copy (`game.ios` on macOS, renamed) |
| `deltarune.elf`, `deltarune_os.ini`, `deltarune.win`, `dr_textures.bin`, `dr_music.bin` | 3, 2, 4, 5, 6 | the same for the Deltarune entry |
| `textures.bin` | 5 | `tools/mktexpack` from data.win |
| `music.bin` | 6 | `tools/mkmusic` from data.win + `music/*.ogg` |
| `undertale_0.sav` | 10 | written by the game (save archive) |

`undertale_0.sav` lives under `Saves/butterscotch/common/` on the card and is
never part of the build tree. `make import-save SAVE_DIR=<desktop save
folder>` builds one from a desktop save (`tools/mksave`); copying it to the
card replaces the Pocket's own progress, so only do that when asked.

Only slots 4–6 are available for data, and file names are limited to 23
characters. The OS config must not be called `undertale.ini`: the game
reads and writes a file of that name itself.

The core uses the **os25** bitstream from the SDK's own runtime (v0.7). Do
not switch it to os20 with that runtime; it boot-loops before the OS banner.

## Committing

One repository holds both the runner and the port:

```bash
git add -A src .claude && git ... commit      # a branch off main
```

- Conventional commit messages; commits are GPG signed automatically.
- `main` is upstream `main` merged with upstream's draft PR #429
  (`sw-renderer`) and this port. Bring upstream in with a merge, not a
  rebase: the branch is published.
- Check `git remote -v` before pushing: a local checkout may name the
  fork (`zenibako/Butterscotch`) `fork` and upstream ButterscotchRunner
  `origin`, while a cloud clone's `origin` is the fork. Push to the fork
  only when asked, never to upstream, and never open PRs or comment upstream on
  the user's behalf.
- A push from a remote checkout can be stopped by a permission check in
  the session driving it. Then give the user the exact `git push -u
  <fork remote> <branch>` to run there rather than looking for another route.
- Commits use the GitHub noreply address set in each repo's local config;
  leave it as it is.

## Pull requests on the fork

- Work goes to `main` through PRs on `zenibako/Butterscotch`, one topic per
  PR. A follow-up topic gets its own branch from `main` and its own PR,
  rather than riding on an open one.
- **Check the PR is still open before pushing to its branch.** PR #4 was
  merged while follow-up fixes were being pushed to it, and they never
  reached `main`. After a merge, put further work on a new branch from
  `main`.
- Write the full PR description (before and after, the evidence, what is
  not yet checked on the device), but start it with an empty `## Summary`
  section for the user to fill in, then a `---` divider, so their copy and
  the agent's stay apart.
- CI runs about 33 checks: desktop gcc and clang, C++98, MSVC 4.0,
  GCC 2.95, Web, the consoles and the Analogue Pocket build. They build
  with warnings as errors, so the usual failures are:
  - a helper or variable only used under one configuration (profiling, a
    platform): mark it `MAYBE_UNUSED` (`src/common.h`) rather than
    deleting it;
  - `ull` suffixes on 64-bit constants, which MSVC 4.0 cannot read;
  - two `for (int i ...)` loops in one function, which MSVC 4.0 treats as
    one variable declared twice;
  - a platform hook the Web build does not provide.
  Reproduce with the desktop CMake build and `-DWERROR=ON` before pushing.
- CodeRabbit reviews PRs and is rate-limited. Check each finding against
  the code, fix the real ones, and reply on the thread for the rest.

## Syncing with upstream

Upstream (`origin/main`) moves quickly. Bring it in locally, with a merge,
and check the result before it is pushed. Do not use GitHub's "Sync fork"
button on `main`: it merges without your signature and without any of
the checks below, and when the branch is both ahead and behind it also
offers to discard the branch's own commits.

1. **Capture reference frames first**, with the `undertale_pc` built from
   the tree as it is (see the butterscotch-pocket-verify skill): a new game,
   the user's save in an overworld room, and a battle. They are the only
   way to tell afterwards whether the merge changed what is drawn.
2. **Merge:** `git fetch origin && git merge --no-ff origin/main`. If the
   clone is shallow, `git fetch --unshallow origin` first, or the merge
   finds no common history.
3. **Make it build.** The port's Makefile lists Butterscotch's sources
   itself and does not read `CMakeLists.txt`, so read that file's diff for
   new source directories, vendored libraries and options, and mirror them
   in `src/openfpga/Makefile`. The first sync (83 commits, 2026-10-08)
   needed `vendor/miniz` added and the physics stubs
   (`src/physics/physics.c` and `src/physics/disabled/physics_disabled.c`,
   since `ENABLE_PHYSICS` stays off).
4. **Expect the renderer interface to have moved.** The software renderer
   comes from draft PR #429, which upstream does not build, so a change to
   `src/renderer.h` reaches `src/sw/` as a compile error for us to fix.
   That first sync changed `drawSpritePart` to take a fractional source
   rectangle. When #429 itself merges upstream, `src/sw/` will conflict
   with this branch's changes to it throughout; treat that as its own job.
5. **Check:** `make -C src/openfpga undertale_pc`, then compare against
   the reference frames. Identical is the expected result; a difference is
   either an upstream behaviour change (find the commit and say so) or a
   bad merge. Then `make -C src/openfpga lint`, and run the frame scripts
   through `make asan` if anything in `src/sw/` or the loaders changed.
6. **Build for the device and have it run once** before pushing: the
   desktop cannot show memory, timing or card behaviour, and upstream code
   has not been through the Pocket before.
7. **Push** only when asked, to a branch on `fork`, and open a PR into
   `main`.
