#!/usr/bin/env bash
# Copy a built, matching os25 save-state runtime into an isolated SDK.
set -euo pipefail
if [ "$#" != 3 ]; then
    echo "Usage: $0 <built openfpgaOS checkout> <base SDK> <new staged SDK>" >&2
    exit 1
fi
os=$(cd "$1" && pwd)
base=$(cd "$2" && pwd)
dest="$3"
[ ! -e "$dest" ] || { echo "Use a new destination; the base SDK stays read-only" >&2; exit 1; }
build="$os/src/fpga/targets/pocket/bld/os25"
python3 - "$build" <<'PY'
import pathlib, re, sys
build = pathlib.Path(sys.argv[1])
qsf = (build / 'ap_core.qsf').read_text()
if 'VERILOG_MACRO INCLUDE_SAVESTATE' not in qsf:
    raise SystemExit('os25 was built without INCLUDE_SAVESTATE')
summary = (build / 'output_files/ap_core.sta.summary').read_text()
slacks = [float(x) for x in re.findall(r'^Slack\s*:\s*([-+\d.]+)', summary, re.M)]
if not slacks or min(slacks) < 0:
    raise SystemExit('Runtime has no passing timing report; do not package it')
PY
mkdir -p "$dest"
dest=$(cd "$dest" && pwd)
rsync -a --exclude=.git "$base/" "$dest/"
make -C "$os" sdk DEST="$dest"
{
    echo "Butterscotch Undertale native save-state runtime"
    git -C "$os" rev-parse HEAD
    shasum -a 256 "$dest/runtime/pocket/os.bin" "$dest/runtime/pocket/os25.rbf_r"
} > "$dest/runtime/pocket/UNDERTALE_SAVESTATES"
