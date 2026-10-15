#!/usr/bin/env bash
# Copy a built, matching os25 save-state runtime into an isolated SDK.
# The runtime comes from our openfpgaOS fork, zenibako/openfpgaCore.
set -euo pipefail
os_required=6a9594f4c6add2cb33034b08cf4d18df684f11e2
# --allow-timing-miss packages a build whose setup or hold slack is negative,
# such as the fork's os25 at 100 MHz. Without it, such a build is refused.
allow_miss=0
if [ "${1:-}" = --allow-timing-miss ]; then
    allow_miss=1
    shift
fi
if [ "$#" != 3 ]; then
    echo "Usage: $0 [--allow-timing-miss] <built openfpgaOS checkout> <base SDK> <new staged SDK>" >&2
    exit 1
fi
os=$(cd "$1" && pwd)
base=$(cd "$2" && pwd)
dest="$3"
git -C "$os" merge-base --is-ancestor "$os_required" HEAD 2>/dev/null || {
    echo "Build zenibako/openfpgaCore at $os_required or later; $os does not contain it" >&2
    exit 1
}
[ ! -e "$dest" ] || { echo "Use a new destination; the base SDK stays read-only" >&2; exit 1; }
build="$os/src/fpga/targets/pocket/bld/os25"
python3 - "$build" "$allow_miss" <<'PY'
import pathlib, re, sys
build = pathlib.Path(sys.argv[1])
allow_miss = sys.argv[2] == '1'
qsf = (build / 'ap_core.qsf').read_text()
if 'VERILOG_MACRO INCLUDE_SAVESTATE' not in qsf:
    raise SystemExit('os25 was built without INCLUDE_SAVESTATE')
summary = (build / 'output_files/ap_core.sta.summary').read_text()
slacks = [float(x) for x in re.findall(r'^Slack\s*:\s*([-+\d.]+)', summary, re.M)]
if not slacks:
    raise SystemExit('Runtime has no timing report; do not package it')
worst = min(slacks)
print(f'os25 worst slack: {worst:+.3f} ns')
if worst < 0:
    if not allow_miss:
        raise SystemExit('Runtime misses timing; pass --allow-timing-miss to package it anyway')
    print('Packaging a build that misses timing (--allow-timing-miss)')
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
