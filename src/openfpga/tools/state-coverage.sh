#!/usr/bin/env bash
# Where can the desktop build capture a save state, and does it restore?
#
#   tools/state-coverage.sh [-g game] [-n frames] [-x last-frame]
#       [-s script-file | -p recording.json | -b] [-e NAME=value]... [save:compare]...
#
# Runs <game>_pc along a route (an input script, a recording, or -b for the
# built-in benchmark route) with BS_STATE_PROBE on and prints, per room and
# verdict, what the scene policy says, what the serializer itself refuses, and
# the largest state captured. Each save:compare pair then saves at frame
# `save` with the scene policy set aside, restores that state in a fresh
# process and checks that frame `compare` is identical to an uninterrupted run.
#
# Desktop only: it shows what the serializer accepts and that a restore is
# exact, not timing, memory limits or anything else about the device.
set -euo pipefail
port=$(cd "$(dirname "$0")/.." && pwd)
game=undertale every=30 last= script= playback= bench=0
extra=()
while getopts "g:n:x:s:p:be:" opt; do
    case $opt in
        g) game=$OPTARG ;; n) every=$OPTARG ;; x) last=$OPTARG ;;
        s) script=$(tr -d '\n' < "$OPTARG") ;; p) playback=$OPTARG ;;
        b) bench=1 ;; e) extra+=("$OPTARG") ;;
        *) sed -n '2,16p' "$0" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
bin="$port/${game}_pc"
[ -x "$bin" ] || { echo "Build it first: make GAME=$game ${game}_pc" >&2; exit 1; }
data="$port/games/$game"
[ -e "$data/data.win" ] || { echo "No game data in $data" >&2; exit 1; }

run=$(mktemp -d "${TMPDIR:-/tmp}/state-coverage.XXXXXX")
trap 'rm -rf "$run"' EXIT
for f in "$data"/*; do ln -s "$f" "$run/"; done
cd "$run"

# A run starts from no saves, a fixed seed and no frame pacing, like
# bs-frames.sh, so two runs of one route are the same frames.
play() {
    rm -f ./*.sav file0 file9 ./*.ini 2>/dev/null || true
    local env=(BS_SEED=7 BS_UNCAPPED=1 BS_NOFLIP=1)
    [ -z "$script" ] || env+=("BS_SCRIPT=$script")
    [ -z "$playback" ] || env+=("BS_PLAYBACK=$playback")
    env+=(${extra[@]+"${extra[@]}"} "$@")
    if [ "$bench" = 1 ]; then env "${env[@]}" "$bin" --bench > last.log 2>&1
    else env "${env[@]}" "$bin" > last.log 2>&1; fi
}

probe=(BS_STATE_PROBE="$every")
[ -z "$last" ] || probe+=(BS_EXIT_FRAME="$last")
if [ "$bench" = 0 ] && [ -z "$last" ]; then
    echo "Give -x <last-frame> (or -b): a scripted run has no end of its own" >&2; exit 2
fi
play "${probe[@]}" || { echo "The run failed; last lines:" >&2; tail -5 last.log >&2; exit 1; }

printf 'samples  frames        max bytes  room / policy / capture / serializer refusals\n'
grep 'State probe' last.log | sed 's/^.*State probe	//' | awk -F'\t' '
    { key = $3 "  |  " $4 "  |  " $5 ($7 == "" ? "" : "  |  " $7)
      if (!(key in n)) { first[key] = $1; order[++rows] = key }
      n[key]++; lastf[key] = $1; if ($6 + 0 > max[key]) max[key] = $6 + 0 }
    END { for (i = 1; i <= rows; i++) { k = order[i]
          printf "%7d  %5d-%-5d  %9d  %s\n", n[k], first[k], lastf[k], max[k], k } }'

status=0
for pair in "$@"; do
    save=${pair%%:*} compare=${pair##*:}
    play BS_DUMP_FRAME="$compare" BS_DUMP_PATH="$run/ref.ppm"
    play BS_STATE_PROBE_BYPASS=1 BS_STATE_SAVE_FRAME="$save" BS_STATE_SAVE_PATH="$run/state.bin" \
        BS_EXIT_FRAME=$((save + 2))
    if [ ! -s state.bin ]; then
        echo "save $save -> compare $compare: no state ($(grep -m1 'refused' last.log | sed 's/^\[[^]]*\] //'))"
        status=1; continue
    fi
    size=$(wc -c < state.bin | tr -d ' ')
    rm -f restored.ppm
    play BS_STATE_PROBE_BYPASS=1 BS_STATE_LOAD_FRAME=40 BS_STATE_LOAD_PATH="$run/state.bin" \
        BS_DUMP_FRAME="$compare" BS_DUMP_PATH="$run/restored.ppm"
    if [ -s restored.ppm ] && cmp -s ref.ppm restored.ppm; then
        echo "save $save -> compare $compare: identical ($size bytes)"
    else
        echo "save $save -> compare $compare: DIFFERENT ($size bytes)"; status=1
    fi
    rm -f state.bin ref.ppm
done
exit $status
