---
name: undertale-pocket-build
description: Build, package and deploy the Undertale port for the Analogue Pocket (Butterscotch on openfpgaOS) in this repo. Use this whenever the task involves building src/openfpga, running make here, producing the SD card tree, copying to the Pocket's SD card (from this Mac or another one), regenerating textures.bin or music.bin, the v0.9 comparison cores, or committing work in this repo — even if the user only says "rebuild it", "copy it" or "is it ready to copy?".
---

# Building and deploying the Undertale Pocket port

## Where things are

```
undertale-pocket/                 not a git repo; holds the pieces below
├── env.sh                        source before any RISC-V build on macOS
├── butterscotch-pocket/          THIS REPOSITORY: fork of ButterscotchRunner/Butterscotch, branch `pocket`
│   ├── src/                      Butterscotch itself; src/sw is the software renderer the port draws with
│   └── src/openfpga/             THE PORT
│       ├── main.c, Makefile, README.md
│       ├── platform/             of_* glue: video, audio, saves, log, bench
│       ├── tools/                mktexpack.c, mkmusic.c, mkcompare.sh, sdcopy.sh
│       ├── dist/                 Pocket core definition (core.json, data slots, instance JSON)
│       ├── data.win, music/      the user's own game data (gitignored)
│       ├── textures.bin, music.bin   generated packs (gitignored)
│       └── out/                  everything the build writes (gitignored):
│           └── build/pocket/undertale/   assembled SD card tree
├── openfpgaSDK/                  the openfpgaOS SDK, read only; SDK_ROOT points here by default
├── Diablo/                       reference port; also the source of the v0.9 SDK + runtime
└── screenshots/                  captures sent to the user
```

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

The Makefile does not track header or flag changes. After editing a header,
or changing `-D` flags, remove the affected objects first:

```bash
rm -rf src/openfpga/out/.obj/undertale-pc                    # desktop
rm -rf src/openfpga/out/.obj/undertale src/openfpga/out/.obj/undertale-v09   # device
```

`make compare` output should end with `Comparison cores added: ...`. A build
that prints nothing after that line succeeded; filter the noise with
`| grep -E " error|undefined reference|Comparison|\*\*\*"`.

## The cardinal rule: don't rebuild under a copy

`make` and `make compare` delete and recreate `src/openfpga/out/build/pocket/undertale/`. The
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

1. **Card in this Mac mini:** `make -C src/openfpga compare-copy`
   (or `copy` for the single core). This runs `tools/sdcopy.sh`, which waits
   for the card, mounts it, copies, removes `._*` sidecar files, verifies,
   and unmounts. It prints a specific message on each failure.
2. **Card in a second Mac:** SSH from here cannot touch the card
   (macOS blocks removable volumes for remote logins; that restriction is
   the user's and is not to be worked around). Give the user these to run in
   a Terminal on that Mac:

   ```bash
   rsync -rc --exclude '._*' --exclude '.DS_Store' <user>@<build-mac>:<path to>/butterscotch-pocket/src/openfpga/out/build/pocket/undertale/ /Volumes/Pocket/
   dot_clean -m /Volumes/Pocket/Assets/undertale /Volumes/Pocket/Cores /Volumes/Pocket/Platforms
   diskutil eject /Volumes/Pocket
   ```
3. **Manual:** `cp -R src/openfpga/out/build/pocket/undertale/{Cores,Assets,Platforms} /Volumes/Pocket/`

Plain `copy` after a `compare-copy` leaves stale v0.9 cores on the card; use
`compare-copy` again if those cores should stay current.

A card reader on an idle, headless Mac can fail to notice a card inserted
while the machine is idle. `sdcopy.sh` declares user activity to
wake it; if no disk appears at all, the card needs reseating.

## What is on the card

| File (Assets/undertale/common) | Slot | Source |
|---|---|---|
| `os.bin`, `undertale_os.ini`, `undertale.elf` | 1, 2, 3 | SDK runtime, dist, build |
| `data.win` | 4 | the user's copy (`game.ios` on macOS, renamed) |
| `textures.bin` | 5 | `tools/mktexpack` from data.win |
| `music.bin` | 6 | `tools/mkmusic` from data.win + `music/*.ogg` |
| `undertale_0.sav` | 10 | written by the game (save archive) |

`undertale_0.sav` lives under `Saves/undertale/common/` on the card and is
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
git add -A src .claude && git ... commit      # branch pocket
```

- Conventional commit messages; commits are GPG signed automatically.
- `pocket` is upstream `main` merged with upstream's draft PR #429
  (`sw-renderer`) and this port. Bring upstream in with a merge, not a
  rebase: the branch is published.
- The `fork` remote is `zenibako/Butterscotch` on GitHub; `origin` is
  upstream. Push to `fork` only when asked, and never open PRs or comment
  upstream on the user's behalf.
- Commits use the GitHub noreply address set in each repo's local config;
  leave it as it is.

## Syncing with upstream

Upstream (`origin/main`) moves quickly. Bring it in locally, with a merge,
and check the result before it is pushed. Do not use GitHub's "Sync fork"
button on `pocket`: it merges without your signature and without any of
the checks below, and when the branch is both ahead and behind it also
offers to discard the branch's own commits.

1. **Capture reference frames first**, with the `undertale_pc` built from
   the tree as it is (see the undertale-pocket-verify skill): a new game,
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
7. **Push `pocket`** only when asked. Leave the fork's `main` as an
   untouched mirror of upstream.
