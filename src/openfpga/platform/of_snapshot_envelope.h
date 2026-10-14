#ifndef OF_SNAPSHOT_ENVELOPE_H
#define OF_SNAPSHOT_ENVELOPE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { const uint8_t *data; size_t size; } OfSnapshotSection;
bool ofSnapshotPack(uint64_t gameId, const OfSnapshotSection sections[4], uint8_t **data, size_t *size);
bool ofSnapshotUnpack(uint64_t gameId, const uint8_t *data, size_t size, OfSnapshotSection sections[4]);
#endif
