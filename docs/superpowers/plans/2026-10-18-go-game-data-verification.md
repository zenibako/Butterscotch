# Go game-data preparation verification

Implementation commit: `2405d39b`. Test date: 2026-10-18.

## Empirical checks

- Native Go unit/CLI/plugin tests passed; malformed data, missing tracks, source
  safety, existing output, failure cleanup, Unicode paths and menu responses covered.
- Existing Python/C converters and Go produced byte-identical complete synthetic
  Undertale/Deltarune game files, including PNG, PCM, generated Vorbis, Deltarune
  audio group/language, aligned combined packs and source data.
- Real C texture oracle additionally matched custom QOI, both bzip2-QOI headers,
  PNG16, transparency boundaries, sparse pages and clipped 65x65 tiles.
- Standard-Go WASI CLI executed for both synthetic games; output matched native
  bytes, and repeat preparation refused existing output without changing files.
- Entire prep unit suite executed as wasm32, including malformed high-bit counts.
  Symlink creation is unavailable in the WASI host; only that subtest skips there.
- Actual TinyGo plugin exports executed in the Extism Go SDK with relative
  `pocket` and `host` preopens in both orders. Both games, including generated
  Vorbis, matched native output. Progress, existing-output refusal, save retention,
  and Kill/Exit were exercised.
- Patched Vorbis module's own malformed Huffman, EOF, allocation, residue and
  exact integer-root regressions passed. Root and nested-module `go vet` passed.
- CLI cross-builds succeeded for Darwin/Linux/Windows, amd64 and arm64, with cgo off.
- Workflow YAML syntax checked locally; remote CI has not run.
- Independent code reviews completed; reproduced findings fixed and re-reviewed.

## Commands

From `src/openfpga/tools/pocket-data`:

```sh
POCKET_PLUGIN_WASM="$PWD/out/plugin.wasm" REQUIRE_PLUGIN_TESTS=1 go test ./... -count=1
go test -C third_party/vorbis ./... -count=1
go vet ./...
go vet -C third_party/vorbis ./...
```

Go 1.27.2 was used locally; go.mod requires 1.25+. The CLI WASM uses the
standard Go compiler. The plugin uses TinyGo 0.43.0 and Binaryen 133, as recorded
in CI. Standard Go's handling of relative WASI preopens failed actual host tests;
no filesystem shim or cwd hack is retained.

## Original verification limits

Synthetic tests do not establish full-game behavior, memory use or conversion
speed with commercial assets. JPEG/GIF decode is supported but stb byte parity
has not been established. Vorbis fixture parity does not prove exact decoded
samples for every possible track. No live Pocket Sync, physical Pocket, or
Windows execution was performed here. No user SD card, original game data or
save files were modified. Nothing was pushed, published or submitted to the catalog.

Publication retains Python/C tools as developer references. Public Go bundles
need only the standalone executable. The plugin prepares a separate output
folder; users explicitly merge its Assets folder to install their own data.

## Real installation follow-up

Fix commit: `96d9ebc7`. Tested the user's installed macOS Steam copies of
Undertale 1.08 and Deltarune Chapter 1. The Deltarune app's chapter directory,
shared music directory, audio group and English language data were discovered
from the app bundle. Commercial input/output stays outside Git and release ZIPs.

The tests exposed and fixed zero alignment after Deltarune's bzip2 texture
streams, positive Ogg position offsets, intermediate Ogg page positions and
whole-final-packet discard. Explicit product rounding also prevents ARM Go
from fusing arithmetic that WASM executes separately. Each codec/rounding fix
has a synthetic regression; no commercial fixture is needed to reproduce it.

Both complete native conversions pass. Game data and all 26 Undertale / eight
Deltarune texture pages match the C reference exactly. All 443 Undertale and
203 Deltarune audio track names, sample counts, rates and encoded sizes match.
Music bytes can differ from the independent C Vorbis decoder: representative
source-PCM comparisons differ by at most one int16 unit; this is a sampled
measurement, not a bound for every track. Comparing the complete decoded ADPCM
packs gives RMS differences of 10.82 / 12.21 int16 units and signal-to-difference
ratios of 52.68 / 52.09 dB respectively. No listening or Pocket gameplay claim
is inferred from those measurements.

Pocket Sync 7.1.0 installs plugins through published GitHub release assets; it
has no local unpublished-plugin loader. Testing therefore uses an isolated
development build of official tag v7.1.0, commit
`c2143b237dc42f566f052916282c435a2c6b64a6`. Only application identity and the
frontend plugin listing were changed to show a clearly labelled local fixture.
Its Rust plugin runner is unchanged (SHA-256
`e78ed8bb7e4642130cd30c8ffabff766da805761289e4dbe8d940e8693bf7d8d`).
The installed production app/profile was preserved; conversion targets a
scratch SD directory with private copies of the actual game files.

Both games completed through the actual Pocket Sync menus. All three output
assets and the preparation manifest match native Go byte for byte for each
game. Undertale took 5m56s; the final deterministic Deltarune run took 2m08s in
this debug host. These are development-build timings, not release benchmarks.
The UI also exercised Back, Exit and refusal to replace existing output.
All 574 original input files retained their hashes, and a scratch save sentinel
was preserved. No game data was installed on the physical SD card.

The long per-track log exposed a Pocket Sync flex-layout issue that squeezed
completion controls out of view. A follow-up changes only the plugin's progress
adapter: report phases and the first/every 64th audio track, retaining detailed
CLI logs. Deltarune was converted again with this final adapter; output remains
identical, and a screenshot confirms the full result and Back/Exit buttons are
visible. Repeat refusal and Exit were retested in that build. The converter
itself is unchanged from the full Undertale/Deltarune parity runs.

The final CLI/plugin tests, WASI prep suite, nested Vorbis tests and root/nested
vet pass. Six native targets and the standard WASI CLI build successfully.
The refreshed macOS ARM64 archive is also extracted and executed against the
real Deltarune installation; its output matches the native reference run.
Windows/Linux execution, stock Pocket Sync release installation and physical
Pocket gameplay remain untested. Nothing was published or submitted upstream.
