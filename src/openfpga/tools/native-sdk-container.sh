#!/usr/bin/env bash
# The SDK wrapper mounts only the SDK; this app lives in another checkout.
set -euo pipefail
port=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$port/../.." && pwd)
source "${SDK_ROOT:?Set SDK_ROOT}/tools/oci.sh"
fingerprint=$(python3 - "$port/games/undertale/data.win" <<'PY'
import hashlib, sys
h = hashlib.sha256()
with open(sys.argv[1], 'rb') as f:
    for block in iter(lambda: f.read(1048576), b''):
        h.update(block)
print(h.hexdigest()[:16])
PY
)
flags=$(printf '%s' "${MAKEFLAGS:-}" | sed -E 's/--jobserver-[^ ]+//g; s/(^| )-j[0-9]*( |$)/\1\2/g')
[ "${1:-}" = make ] || { echo "Expected a make invocation" >&2; exit 1; }
oci_run --rm --user "$(id -u):$(id -g)" \
    -v "$repo:$repo" -v "$SDK_ROOT:$SDK_ROOT" -w "$(pwd -P)" \
    -e CPATH= -e OF_SDK_IN_CONTAINER=1 -e "MAKEFLAGS=$flags" \
    "${SDK_IMG:-openfpgaos-firmware}" "$@" "STATE_GAME_ID=$fingerprint"
