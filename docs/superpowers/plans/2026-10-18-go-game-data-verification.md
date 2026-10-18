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

## Practical limits

Synthetic tests do not establish full-game behavior, memory use or conversion
speed with commercial assets. JPEG/GIF decode is supported but stb byte parity
has not been established. Vorbis fixture parity does not prove exact decoded
samples for every possible track. No live Pocket Sync, physical Pocket, or
Windows execution was performed here. No user SD card, original game data or
save files were modified. Nothing was pushed, published or submitted to the catalog.

Publication retains Python/C tools as developer references. Public Go bundles
need only the standalone executable. The plugin prepares a separate output
folder; users explicitly merge its Assets folder to install their own data.
