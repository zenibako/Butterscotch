# Portable Go game-data preparation

Approved in chat on 2026-10-18: port the complete Python/C preparation pipeline
to Go, verify CLI and WASM conversion first, then add Pocket Sync plugin menus.

## Existing decisions

`src/openfpga/GAME-DATA.md` defines the supported Undertale 1.08 / Deltarune
Chapter 1 inputs, output filenames, source preservation and separate core/data
installation. `2026-10-17-sequential-optimization-design.md`, Loading hitches,
requires bounded decoding and preserving original game assets. The existing
`prepare_game.py`, `mktexpack.c`, `mkmusic.c`, `image_decoder.c`,
`sw_texture_pack.h` and `of_music_pack.h` are the compatibility references.

## Architecture

Use Go 1.25 or newer (the pinned WASI test runtime requires it). Create one Go module at `src/openfpga/tools/pocket-data`. Package `prep` owns
bounded FORM parsing, discovery, conversion, validation and staged output.
No subprocesses, cgo, Python runtime or platform converter binaries are used.
The native CLI and standard-Go WASI CLI call the same library. The plugin uses
TinyGo 0.43.0 with Binaryen 133: actual host tests exposed standard Go's repeated
cwd prefixing for relative WASI preopens; TinyGo handles both mount orders. A separate Extism adapter
implements Pocket Sync's Choice/Text/Exit protocol using that library after
conversion has passed native/WASI integration checks.

Alternatives considered: a Go wrapper still needs native executables; linking
C converters into WASM creates a second toolchain and two conversion paths.
The chosen pure Go implementation gives native and WASM the same behavior.

## Contracts

- Preserve `--game`, `--source`, `--output`, installed-folder/.app/direct-file
  discovery, required music checks, and Deltarune group/language inclusion.
- Preserve UTX2 tiles/RGB1555/RLE, UTM1 sorted names/mono 32 kHz IMA ADPCM,
  and the 4096-aligned texture append with 16-byte UTXT footer. No runtime edits.
- Decode PNG and GameMaker custom QOI/bzip2-QOI without premultiplying alpha;
  preserve the existing decoder's JPEG/GIF coverage. Decode PCM WAV and Vorbis.
- Reject malformed bounds, missing tracks, ambiguous inputs, duplicate names,
  unsupported dimensions and names, and incomplete conversion; do not silently
  drop inputs. Convert one texture page / audio track at a time with explicit
  allocation limits suitable for wasm32. Native and WASM produce identical bytes.
- Use a pinned pure Go Vorbis decoder. Compare PCM-derived outputs exactly to C;
  measure and disclose any Vorbis differences rather than claiming exact parity.
- Refuse existing output, output inside source or Saves, and unsafe symlink paths.
  Write into an adjacent staging directory and publish only after validation.
  Preserve originals, installed data, saves and failed-conversion atomicity.
- Reports contain hashes and source basename, never full personal source paths.
- Keep Python/C reference tools while migrating; Go becomes the documented public
  path. No commercial fixtures, publication, SD writes or upstream changes.

## Plugin workflow

Pocket Sync exposes `pocket` (SD root) and `host` (private sandbox); it does not
grant arbitrary computer paths or subprocess execution. Ask users to stage their
own installation below `pocket/Butterscotch-input/<game>`. Menus select game,
explain staging, request conversion, show progress and results, and allow exit.
Use a new prepared-output folder on the card so existing installed files are
never replaced by an unreviewed conversion. Tell users to merge its Assets into
the card root; a subsequent installer can be designed separately if needed.
Reject forged/out-of-order responses and handle Kill without partial publication.
Ship plugin.wasm + plugin.json separately from the core ZIP.

## Verification

Use synthetic PNG/QOI/PCM and licensed Vorbis fixtures. Exercise malformed
containers, texture edges/transparency, resampling and ADPCM, names/group offsets,
missing audio, Unicode paths, symlinks, existing output, failure cleanup, CLI
exit status, and protocol state. Compare C reference packs to Go outputs and
native Go to an actually executed WASI binary. Build native macOS/Windows/Linux
and run Go tests natively and in WASI. Execute Extism exports in a test host when
available; explicitly distinguish that from live Pocket Sync/Pocket testing.
