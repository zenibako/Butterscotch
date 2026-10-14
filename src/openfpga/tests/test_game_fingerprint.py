"""A container-supplied game fingerprint must survive an unavailable data.win."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

port = Path(__file__).resolve().parents[1]
sdk = os.environ.get('SDK_ROOT', str(port / 'out/savestate-sdk-strg'))
source = (port / 'Makefile').read_text()
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / 'Makefile').write_text(source + '\n.PHONY: fingerprint-fixture\nfingerprint-fixture:\n\t@echo $(BS_DEFS)\n')
    def flags(*args):
        return subprocess.check_output(['make', '--no-print-directory', 'fingerprint-fixture',
            f'SDK_ROOT={sdk}', 'SAVESTATES=1', *args], cwd=root, text=True)
    supplied = '0123456789abcdef'
    assert f'-DOF_STATE_GAME_ID=0x{supplied}ULL' in flags(f'STATE_GAME_ID={supplied}')
    # Local builds still calculate the same hash, and missing-file CI builds
    # remain valid when they do not claim to be tied to supplied game data.
    data = b'fingerprint fixture game data'
    game = root / 'games/undertale'
    game.mkdir(parents=True)
    (game / 'data.win').write_bytes(data)
    expected = hashlib.sha256(data).hexdigest()[:16]
    assert f'-DOF_STATE_GAME_ID=0x{expected}ULL' in flags()
    assert f'-DOF_STATE_GAME_ID=0x{supplied}ULL' in flags(f'STATE_GAME_ID={supplied}')
    failed = subprocess.run(['make', '--no-print-directory', 'fingerprint-fixture',
        f'SDK_ROOT={sdk}', 'SAVESTATES=1', 'STATE_GAME_ID='], cwd=root, capture_output=True, text=True)
    assert failed.returncode and 'Could not fingerprint' in failed.stderr
    # Run the real wrapper with an OCI stub: it must hash the moved host data
    # before forwarding the ID into a container that cannot follow its symlink.
    wrapper_port = root / 'repo/src/openfpga'
    (wrapper_port / 'tools').mkdir(parents=True)
    (wrapper_port / 'games/undertale').mkdir(parents=True)
    (wrapper_port / 'games/undertale/data.win').write_bytes(data)
    wrapper = wrapper_port / 'tools/native-sdk-container.sh'
    wrapper.write_text((port / 'tools/native-sdk-container.sh').read_text())
    stub_sdk = root / 'stub-sdk'
    (stub_sdk / 'tools').mkdir(parents=True)
    (stub_sdk / 'tools/oci.sh').write_text('oci_run() { printf "%s\\n" "$@"; }\n')
    forwarded = subprocess.check_output(['bash', str(wrapper), 'make'],
        cwd=wrapper_port, env={**os.environ, 'SDK_ROOT': str(stub_sdk)}, text=True)
    assert f'STATE_GAME_ID={expected}' in forwarded
print('PASS: supplied/local fingerprints and missing-fingerprint validation')
