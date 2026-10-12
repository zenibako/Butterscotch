#include "ut_snapshot_envelope.h"
#include "ut_snapshot_bytes.h"
#define UT_STATE_MAGIC 0x31535455u
#define UT_STATE_VERSION 2u
#define UT_STATE_HEADER 40u
static uint32_t checksum(const uint8_t *data, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++)
        h = (h ^ (i >= 36 && i < 40 ? 0 : data[i])) * 16777619u;
    return h;
}
bool utSnapshotPack(uint64_t gameId, const UtSnapshotSection sections[4], uint8_t **data, size_t *size) {
    *data = NULL; *size = 0;
    size_t total = UT_STATE_HEADER;
    for (unsigned i = 0; i < 4; i++) {
        if (!sections[i].data || !sections[i].size || sections[i].size > UT_SNAPSHOT_LIMIT - total) return false;
        total += sections[i].size;
    }
    UtSnapshotWriter w = {.ok = true};
    utSnapU32(&w, UT_STATE_MAGIC); utSnapU32(&w, UT_STATE_VERSION);
    utSnapU32(&w, (uint32_t)gameId); utSnapU32(&w, (uint32_t)(gameId >> 32));
    for (unsigned i = 0; i < 4; i++) utSnapU32(&w, (uint32_t)sections[i].size);
    utSnapU32(&w, (uint32_t)total); utSnapU32(&w, 0);
    for (unsigned i = 0; i < 4; i++) utSnapWrite(&w, sections[i].data, sections[i].size);
    if (!w.ok) { free(w.data); return false; }
    uint32_t crc = checksum(w.data, w.size);
    for (unsigned i = 0; i < 4; i++) w.data[36 + i] = (uint8_t)(crc >> (8 * i));
    *data = w.data; *size = w.size; return true;
}
bool utSnapshotUnpack(uint64_t gameId, const uint8_t *data, size_t size, UtSnapshotSection sections[4]) {
    if (!data || size < UT_STATE_HEADER || size > UT_SNAPSHOT_LIMIT) return false;
    UtSnapshotReader r = {data, size, 0, true};
    if (utSnapGetU32(&r) != UT_STATE_MAGIC || utSnapGetU32(&r) != UT_STATE_VERSION ||
        utSnapGetU32(&r) != (uint32_t)gameId || utSnapGetU32(&r) != (uint32_t)(gameId >> 32)) return false;
    uint32_t lengths[4];
    for (unsigned i = 0; i < 4; i++) lengths[i] = utSnapGetU32(&r);
    if (utSnapGetU32(&r) != size || utSnapGetU32(&r) != checksum(data, size)) return false;
    size_t next = UT_STATE_HEADER;
    for (unsigned i = 0; i < 4; i++) {
        if (!lengths[i] || lengths[i] > size - next) return false;
        sections[i].data = data + next; sections[i].size = lengths[i]; next += lengths[i];
    }
    return next == size;
}
