# Go game-data preparation implementation plan

> **For agentic workers:** Use subagent-driven-development for independent codec
> tasks and executing-plans for their integration. Track completed tasks here.

**Goal:** Prepare supported games with one Go codebase on native hosts and WASM,
then expose the same conversion through Pocket Sync menus.

**Architecture:** A `prep` library parses and converts, a CLI handles flags, and
an Extism adapter handles plugin messages. Keep the current formats and C tools
as an independent compatibility oracle.

**Tech stack:** Go 1.24+, standard library, pinned pure-Go oggvorbis, Extism Go PDK.

**Spec:** ../specs/2026-10-18-go-game-data-design.md

## Global constraints

No cgo/subprocesses in conversion. Preserve source/saves; refuse existing output.
Preserve UTX2/UTM1/UTXT. No commercial test assets or external publication.
Signed conventional commits. Keep native and WASM byte-identical.

## Task 1: Shared container parsing and texture conversion

Files: `tools/pocket-data/go.mod`, `prep/container.go`, `texture.go`, `image.go`
and corresponding `_test.go` files, relative to `src/openfpga`.

Interface: `Parse([]byte) (*Container,error)`, `Container.Chunk(string)
([]byte,error)`, `Container.U32(int) (uint32,error)`,
`Container.String(uint32) (string,error)`, `Container.Data []byte`.
`TexturePack([]byte,func(string)) ([]byte,error)` emits UTX2.

- [ ] Write malformed FORM and synthetic image/RLE tests; run
  `go test ./prep` and observe missing implementation failure.
- [ ] Implement overflow-safe parsing, PNG/custom QOI/bzip2 decoding, RGB1555
  conversion and tiled pack generation. Bound dimensions and decoded bytes.
- [ ] Compare PNG/QOI output to `mktexpack`; run tests and sign a commit.

## Task 2: Audio conversion

Files: `prep/audio.go`, `music.go`, their tests and licensed testdata.
Consumes Container. Produces `MusicPack(data []byte, external map[string]string,
groups map[int][]byte, raw map[string][]byte, progress func(string)) ([]byte,error)`.

- [ ] Write failing PCM, resampling, ADPCM, group and invalid audio tests.
- [ ] Implement WAV/Vorbis decode, weighted mono resampling with ties-to-even,
  IMA encoding, sorted UTM1 records and raw-file records with bounds checking.
- [ ] Verify PCM pack bytes against C and measure Vorbis parity. Test invalid
  and empty inputs fail. Run tests and sign a commit.

## Task 3: Safe preparation and CLI

Files: `prep/discover.go`, `prepare.go`, tests, `cmd/prepare-data/main.go`.
Consumes TexturePack/MusicPack. Produces `Prepare(Options) (string,error)` where
Options has Game, Source, Output strings and Progress func(string).

- [ ] Port discovery/safety regression fixtures before implementation; confirm
  `go test ./prep -run Prepare` fails, then implement and re-run.
- [ ] Preserve data/pack filenames, validate emitted tracks, append texture
  footer, hash manifest and stage the complete folder before publishing.
- [ ] Add CLI `--game --source --output`; verify errors return nonzero and
  Unicode/space paths work. Compare complete output to Python/C fixture output.

## Task 4: Execute WASM conversion

Files: integration host/test scripts under `tools/pocket-data/integration`.

- [ ] Build CLI with `CGO_ENABLED=0 GOOS=wasip1 GOARCH=wasm go build
  -o <output>/prepare-data.wasm ./cmd/prepare-data`.
- [ ] Execute it in a WASI host against synthetic Undertale/Deltarune inputs;
  compare every game output byte to native CLI, including error/cleanup cases.
- [ ] Cross-build Darwin/Linux/Windows amd64/arm64 binaries, run tests/vet and
  update workflow/docs to the standalone Go build. Commit the verified pipeline.

## Task 5: Pocket Sync plugin

Files: `plugin/session.go`, tests, `cmd/pocket-plugin/main_wasip1.go`, plugin.json.

- [ ] Write protocol tests for start/game selection/confirm/exit/Kill and forged
  responses before implementing the state machine.
- [ ] Use fixed staging locations and Prepare; expose `start` and
  `handle_response` using Extism exports and host print progress messages.
- [ ] Build `GOOS=wasip1 GOARCH=wasm go build -buildmode=c-shared`; test in
  Extism host with mapped temporary directories and check error responses.
- [ ] Document staging, output merge and separate plugin release assets. Review
  the complete change, fix findings, rerun checks and sign the final commits.
