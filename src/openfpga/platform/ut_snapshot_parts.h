#ifndef UT_SNAPSHOT_PARTS_H
#define UT_SNAPSHOT_PARTS_H
#include "audio_system.h"
#include "file_system.h"
#include <stddef.h>
#include <stdint.h>

bool utAudioSnapshot(AudioSystem *audio, uint8_t **data, size_t *size, const char **reason);
void *utAudioPrepareSnapshot(AudioSystem *audio, const uint8_t *data, size_t size, const char **reason);
void utAudioApplySnapshot(AudioSystem *audio, void *prepared);
void utAudioFreeSnapshot(void *prepared);
/* Stops idle-hook mixing while a snapshot is captured or prepared. */
void utAudioSnapshotLock(bool locked);
#ifdef OF_PC
void utPcAudioSnapshotReset(void);
#endif
bool utSaveFsSnapshot(FileSystem *fs, uint8_t **data, size_t *size, const char **reason);
void *utSaveFsPrepareSnapshot(FileSystem *fs, const uint8_t *data, size_t size, const char **reason);
bool utSaveFsApplySnapshot(FileSystem *fs, void *prepared);
void utSaveFsFreeSnapshot(void *prepared);
#endif
