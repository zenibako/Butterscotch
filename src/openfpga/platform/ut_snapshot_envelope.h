#ifndef UT_SNAPSHOT_ENVELOPE_H
#define UT_SNAPSHOT_ENVELOPE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { const uint8_t *data; size_t size; } UtSnapshotSection;
bool utSnapshotPack(uint64_t gameId, const UtSnapshotSection sections[4], uint8_t **data, size_t *size);
bool utSnapshotUnpack(uint64_t gameId, const uint8_t *data, size_t size, UtSnapshotSection sections[4]);
#endif
