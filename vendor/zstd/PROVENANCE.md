Zstandard v1.5.7, official tag archive:
https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
SHA-256: 37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
This matches Homebrew's published v1.5.7 source checksum. The official
release archive was also downloaded (SHA-256
eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3);
its `lib/` tree and single-file source template match the tag archive.

`zstd.c` was generated from that archive with:
`cd build/single_file_libs && python3 combine.py -r ../../lib -x legacy/zstd_legacy.h -k zstd.h -o zstd.c zstd-in.c`
The generated file was adjusted to disable multithreading, force memcpy-based
unaligned memory access (`MEM_FORCE_MEMORY_ACCESS=0`) for RV32, and resolve
the local `zstd.h` include. Its upstream single-file template already disables
assembly. `zstd.h`, `zstd_errors.h`, and `LICENSE` are copied unchanged from
the archive. We use the BSD license option in LICENSE. The generated source
retains the upstream notices for bundled third-party code.
