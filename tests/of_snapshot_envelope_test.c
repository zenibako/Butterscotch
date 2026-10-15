#include "../src/openfpga/platform/of_snapshot_envelope.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    const uint8_t a[] = {1,2}, b[] = {3,4,5}, c[] = {6};
    OfSnapshotSection parts[4] = {{a,2},{b,3},{c,1},{a,2}}, out[4];
    uint8_t *data; size_t size;
    assert(ofSnapshotPack(0xABC123ull, parts, &data, &size));
    assert(ofSnapshotUnpack(0xABC123ull, data, size, out));
    for (int i = 0; i < 4; i++) assert(out[i].size == parts[i].size && !memcmp(out[i].data, parts[i].data, out[i].size));
    assert(!ofSnapshotUnpack(0xABC124ull, data, size, out));
    /* Another game's intact state is named as such; one's own, or a damaged
     * one, is not. */
    assert(ofSnapshotFromOtherGame(0xABC124ull, data, size));
    assert(!ofSnapshotFromOtherGame(0xABC123ull, data, size));
    for (size_t i = 0; i < size; i++) assert(!ofSnapshotFromOtherGame(0xABC124ull, data, i));
    data[size - 1] ^= 1;
    assert(!ofSnapshotFromOtherGame(0xABC124ull, data, size));
    data[size - 1] ^= 1;
    /* Bad section lengths under a matching checksum are damage, not
     * another game. */
    uint8_t *bad = malloc(size);
    memcpy(bad, data, size);
    bad[16] = 9;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++) h = (h ^ (i >= 36 && i < 40 ? 0 : bad[i])) * 16777619u;
    for (unsigned i = 0; i < 4; i++) bad[36 + i] = (uint8_t)(h >> (8 * i));
    assert(!ofSnapshotFromOtherGame(0xABC124ull, bad, size));
    assert(!ofSnapshotUnpack(0xABC123ull, bad, size, out));
    free(bad);
    for (size_t i = 0; i < size; i++) assert(!ofSnapshotUnpack(0xABC123ull, data, i, out));
    for (size_t i = 0; i < size; i++) {
        data[i] ^= 1;
        assert(!ofSnapshotUnpack(0xABC123ull, data, size, out));
        data[i] ^= 1;
    }
    free(data);
    parts[1].size = (size_t)-1;
    assert(!ofSnapshotPack(0, parts, &data, &size));
    puts("snapshot envelope: passed");
}
