#include "of_snapshot_envelope.h"
#include "of_snapshot_bytes.h"
#define OF_STATE_MAGIC 0x31535455u
#define OF_STATE_VERSION 2u
#define OF_STATE_HEADER 40u
static uint32_t checksum(const uint8_t *data, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++)
        h = (h ^ (i >= 36 && i < 40 ? 0 : data[i])) * 16777619u;
    return h;
}
bool ofSnapshotPack(uint64_t gameId, const OfSnapshotSection sections[4], uint8_t **data, size_t *size) {
    *data = NULL; *size = 0;
    size_t total = OF_STATE_HEADER;
    for (unsigned i = 0; i < 4; i++) {
        if (!sections[i].data || !sections[i].size || sections[i].size > OF_SNAPSHOT_LIMIT - total) return false;
        total += sections[i].size;
    }
    OfSnapshotWriter w = {.ok = true};
    ofSnapU32(&w, OF_STATE_MAGIC); ofSnapU32(&w, OF_STATE_VERSION);
    ofSnapU32(&w, (uint32_t)gameId); ofSnapU32(&w, (uint32_t)(gameId >> 32));
    for (unsigned i = 0; i < 4; i++) ofSnapU32(&w, (uint32_t)sections[i].size);
    ofSnapU32(&w, (uint32_t)total); ofSnapU32(&w, 0);
    for (unsigned i = 0; i < 4; i++) ofSnapWrite(&w, sections[i].data, sections[i].size);
    if (!w.ok) { free(w.data); return false; }
    uint32_t crc = checksum(w.data, w.size);
    for (unsigned i = 0; i < 4; i++) w.data[36 + i] = (uint8_t)(crc >> (8 * i));
    *data = w.data; *size = w.size; return true;
}
bool ofSnapshotUnpack(uint64_t gameId, const uint8_t *data, size_t size, OfSnapshotSection sections[4]) {
    if (!data || size < OF_STATE_HEADER || size > OF_SNAPSHOT_LIMIT) return false;
    OfSnapshotReader r = {data, size, 0, true};
    if (ofSnapGetU32(&r) != OF_STATE_MAGIC || ofSnapGetU32(&r) != OF_STATE_VERSION ||
        ofSnapGetU32(&r) != (uint32_t)gameId || ofSnapGetU32(&r) != (uint32_t)(gameId >> 32)) return false;
    uint32_t lengths[4];
    for (unsigned i = 0; i < 4; i++) lengths[i] = ofSnapGetU32(&r);
    if (ofSnapGetU32(&r) != size || ofSnapGetU32(&r) != checksum(data, size)) return false;
    size_t next = OF_STATE_HEADER;
    for (unsigned i = 0; i < 4; i++) {
        if (!lengths[i] || lengths[i] > size - next) return false;
        sections[i].data = data + next; sections[i].size = lengths[i]; next += lengths[i];
    }
    return next == size;
}
bool ofSnapshotFromOtherGame(uint64_t gameId, const uint8_t *data, size_t size) {
    if (!data || size < OF_STATE_HEADER || size > OF_SNAPSHOT_LIMIT) return false;
    OfSnapshotReader r = {data, size, 0, true};
    if (ofSnapGetU32(&r) != OF_STATE_MAGIC || ofSnapGetU32(&r) != OF_STATE_VERSION) return false;
    uint64_t id = ofSnapGetU32(&r);
    id |= (uint64_t)ofSnapGetU32(&r) << 32;
    for (unsigned i = 0; i < 4; i++) ofSnapGetU32(&r);
    return id != gameId && ofSnapGetU32(&r) == size && ofSnapGetU32(&r) == checksum(data, size);
}
