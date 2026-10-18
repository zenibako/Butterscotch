# Add your own game files

Butterscotch Pocket includes the core and game programs. You supply the game
data from your own computer installation. The preparation script converts its
textures and audio and writes a new folder ready to copy to your SD card.
It does not download games, change your original installation or write saves.

The supported targets are **Undertale v1.08** and **Deltarune Chapter 1**.
Checking a data file's bytecode version does not establish compatibility with
other game releases or chapters.

## 1. Install the core

Choose either installation method:

- **Pocket Sync:** Connect your Pocket or insert its SD card, click
  **Connect to Pocket**, and select the card's root folder (the one containing
  `Assets`, `Cores` and `Saves`). Drag the Butterscotch Pocket **core ZIP** onto
  the Pocket Sync window and follow its install prompts. This works before
  Butterscotch appears in its catalog. Once listed, you can also install it
  from Pocket Sync's core browser.
- **Manual:** Extract the core ZIP into the root of your Pocket SD card,
  merging its `Cores`, `Assets` and `Platforms` folders with the existing folders.

Installing the core does not install your commercial game data. Complete the
preparation steps below for either installation method. The game-data
preparation bundle is a separate computer-side tool; extract it on your
computer, not on the SD card. Do not drag that tools archive into Pocket Sync.

Use Python 3.9 or newer. The preparation bundle must include the native
`mktexpack` and `mkmusic` converters for your computer. The script detects macOS,
Windows or Linux and x86_64 or arm64, and selects `bin/<platform>-<architecture>/`.
It reports a missing or unsupported converter bundle before creating output.

## 2. Select your installed game

For Undertale, select the installed game folder containing `data.win`, or
select `UNDERTALE.app` on macOS. Selecting its exact `game.ios` or Linux
`game.unx` file also works. Keep the game's original `.ogg` music alongside it
or in its `music` folder. An installer executable or a ZIP is not a data file.

For Deltarune, select the installed game folder containing `chapter1_windows`,
`chapter1_mac` or `chapter1_linux` and the shared `mus` folder. On macOS you
can select `DELTARUNE.app`. A Chapter 1 folder or its exact data file also works
when the shared `mus` folder is beside the chapter folder.

Deltarune needs all of these original files:

- Chapter 1's `data.win` or `game.ios`.
- `audiogroup1.dat` in the chapter folder.
- `lang/lang_en.json` in the chapter folder.
- Chapter music and the shared `mus` folder's `.ogg` files.

The script gathers the chapter's referenced music and includes its language
and additional sound data in the generated audio pack. You do not need to
rename or manually copy those extra files to the SD card.

## 3. Prepare a new output folder

Open a terminal in the extracted preparation bundle. Replace the example
paths with your own. Keep paths containing spaces inside quotes.

macOS or Linux:

```sh
python3 prepare_game.py --game undertale --source "/path/to/UNDERTALE.app" --output "prepared-undertale"
python3 prepare_game.py --game deltarune --source "/path/to/DELTARUNE.app" --output "prepared-deltarune"
```

Windows, in PowerShell:

```powershell
py -3 prepare_game.py --game undertale --source "C:\Games\UNDERTALE" --output "prepared-undertale"
py -3 prepare_game.py --game deltarune --source "C:\Games\DELTARUNE" --output "prepared-deltarune"
```

Run only the command for the game you want to install. Choose an output folder
that does not already exist and is outside the original game installation.
If preparation fails, fix the reported missing or invalid file and rerun it.
A failed conversion does not publish a partial output folder. To prepare again,
choose a new output folder; the script does not overwrite an existing folder.

## 4. Install the prepared data

After the script says `Ready`, choose either method:

- **Pocket Sync:** In the prepared output folder, compress the **Assets**
  folder into a ZIP using your computer's file manager. Drag this new ZIP onto
  the connected Pocket Sync window, review the selected `Assets` paths, and
  click **Confirm**. Include all three generated game files.
- **Manual:** Copy the output folder's **Assets** folder into the root of
  your Pocket SD card and merge folders.

For Pocket Sync, check that opening the ZIP shows `Assets` immediately, with
`butterscotch/common` inside it. Do not compress the enclosing
`prepared-undertale` or `prepared-deltarune` folder: that would add an extra
directory above `Assets`. The game-data ZIP contains no core, tools or saves.
It is a private archive for your own card; do not upload it to a release.

The resulting paths on the card are:

| Game | Files under `/Assets/butterscotch/common/` |
|---|---|
| Undertale | `data.win`, `textures.bin`, `music.bin` |
| Deltarune Chapter 1 | `deltarune.win`, `dr_textures.bin`, `dr_music.bin` |

These generated texture and audio packs belong together with that game's data
file. Copy all three files. When changing the game data, prepare all three
again. Your core's matching app, OS and FPGA runtime come from the core ZIP;
preparing game files does not replace them.

Keep the `PREPARED-DATA.json` report on your computer if you need to identify
the data hash and generated files later. It is not needed on the card.

Unmount the card after copying, put it in the Pocket, and open Butterscotch.
Select only a game whose data you installed. Your progress is under
`/Saves/butterscotch/common/`; the preparation output contains no `Saves`
folder. Keep your original games and generated data private.

Pocket Sync installs the core and copies prepared files; it does not run the
Python converters or obtain your purchased game data. Installing an updated
core does not convert a different game version. If a release asks you to
prepare the data again, repeat steps 2–4 with a new output folder.

Pocket Sync's [installation and ZIP import instructions](https://github.com/neil-morrison44/pocket-sync#installation)
describe connecting a card and importing archives. Butterscotch's catalog
listing must point to the **core ZIP**, keeping the computer-side tools and
private game-data archives separate.

## Building the converters from source

This section is for developers preparing bundles or testing the script.
Release users use the included binaries and do not need a C compiler or the
openfpgaOS SDK.

From `src/openfpga/tools` in the full source checkout:

```sh
python3 prepare_game.py --build-tools
```

On Windows use `py -3` and a GCC-compatible MinGW compiler available on `PATH`.
On macOS/Linux use the host C compiler (`cc`), or set `CC`. The tools are built
from the repository's vendored sources without the FPGA toolchain, SDK, Docker
or downloaded game files. Output is `src/openfpga/out/data-tools/<host>/`.
Use `--tools-dir` to select another directory containing the two converters.

A preparation bundle places `prepare_game.py`, these instructions, and
`bin/<host>/mktexpack` and `mkmusic` together (`.exe` on Windows). The native
build workflow tests the real conversion tools using synthetic PNG/PCM game
fixtures before retaining platform artifacts. These tests do not replace
compatibility tests with the user's own complete game on Pocket.
