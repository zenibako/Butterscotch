# Legacy Memory identity compatibility

Three supplied Pocket Memories reproduced the "another game or version"
refusal against the sentry candidate based on `c3a1aed3`. Their envelope and
runner checksums are valid. Their stored identity matches the loaded Deltarune
metadata and code hash before the optional whole-`data.win` fingerprint is added.

The loader now accepts that computed legacy identity as well as the current
identity. It passes the matched identity to the runner reader, so mixed outer
and inner identities still fail. It does not rewrite input files, alter snapshot
formats, or weaken the identity used by new captures. Legacy states retain their
original metadata-and-code binding, which cannot detect every non-code change
in the game file; use their original game data.

## Verification

- `test_state_identity.py` failed on legacy loading before the fix and passes
  afterward. It links the real envelope reader and tests builds with and without
  the packaged fingerprint, wrong games, corrupt/truncated envelopes and mixed
  inner/outer identities.
- The full ASan/UBSan snapshot suite passes, including both WAD variants and
  restore completion ordering.
- Retained desktop legacy and current states load in fresh processes, recapture
  with the current fingerprint, and cold reload with identical rendered pixels.
- Both production Pocket ELFs build with Memories enabled. Their fast VM loop
  remains at `0x4000` and normal-save `fsync` uses syscall 82. The matched runtime
  is unchanged.

The supplied Pocket files cannot be fully restored by the ARM64 desktop runner:
native `RunnerPlain` is 23,400 bytes and desktop is 23,408; native `InstancePlain`
is 244 bytes and desktop is 252. The retained HUD and current Pocket ELFs have
matching native layouts. All supplied files decompress with valid raw checksums,
valid native epsilon and room-count boundaries, in rooms 80 and 81.

This validates the identity fix and native layout continuity, not complete
restoration of these files on hardware. Test the unchanged original Memories on
a Pocket candidate before treating their recovery as confirmed. Private input
files, decoded contents and detailed logs remain in ignored local evidence.
