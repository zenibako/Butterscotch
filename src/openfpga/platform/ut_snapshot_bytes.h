#ifndef UT_SNAPSHOT_BYTES_H
#define UT_SNAPSHOT_BYTES_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define UT_SNAPSHOT_LIMIT (1024u * 1024u - 32u)
typedef struct { uint8_t *data; size_t size, capacity; bool ok; } UtSnapshotWriter;
typedef struct { const uint8_t *data; size_t size, offset; bool ok; } UtSnapshotReader;
static inline void utSnapWrite(UtSnapshotWriter *w, const void *p, size_t n) {
    if (!w->ok || n > UT_SNAPSHOT_LIMIT - w->size) { w->ok = false; return; }
    size_t needed = w->size + n;
    if (needed > w->capacity) {
        size_t cap = needed > w->capacity * 2 ? needed : w->capacity * 2;
        if (cap < 256) cap = 256;
        if (cap > UT_SNAPSHOT_LIMIT) cap = UT_SNAPSHOT_LIMIT;
        uint8_t *d = realloc(w->data, cap);
        if (!d) { w->ok = false; return; }
        w->data = d; w->capacity = cap;
    }
    if (n) memcpy(w->data + w->size, p, n);
    w->size += n;
}
static inline void utSnapU32(UtSnapshotWriter *w, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    utSnapWrite(w, b, 4);
}
static inline void utSnapFloat(UtSnapshotWriter *w, float v) {
    uint32_t bits; memcpy(&bits, &v, 4); utSnapU32(w, bits);
}
static inline bool utSnapRead(UtSnapshotReader *r, void *p, size_t n) {
    if (!r->ok || n > r->size - r->offset) { r->ok = false; return false; }
    if (n) memcpy(p, r->data + r->offset, n);
    r->offset += n; return true;
}
static inline uint32_t utSnapGetU32(UtSnapshotReader *r) {
    uint8_t b[4] = {0}; utSnapRead(r, b, 4);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static inline float utSnapGetFloat(UtSnapshotReader *r) {
    uint32_t bits = utSnapGetU32(r); float v; memcpy(&v, &bits, 4); return v;
}
static inline uint32_t utSnapHash(const void *data, size_t n) {
    const uint8_t *p = data; uint32_t hash = 2166136261u;
    for (size_t i = 0; i < n; i++) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}
#endif
