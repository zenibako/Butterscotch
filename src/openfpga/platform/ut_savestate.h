#ifndef UT_SAVESTATE_H
#define UT_SAVESTATE_H
#include "runner.h"
#include <stddef.h>
#include <stdint.h>
void utSavestateStartup(void);
void utSavestateInit(Runner *runner);
const char *utSavestateReason(Runner *runner);
bool utSavestateCapture(Runner *runner, uint8_t **data, size_t *size, const char **reason);
bool utSavestateRestore(Runner *runner, const uint8_t *data, size_t size, const char **reason);
/* True after a save/load pause: the loop must rebase its pacing clock. */
bool platformSnapshotBoundary(Runner *runner);
void utSavestateMenuPoll(void);
void utPlatformSnapshotNotice(const char *text);
void utPlatformSnapshotGet(uint32_t values[3]);
void utPlatformSnapshotResumed(Runner *runner, const uint32_t values[3]);
#endif
