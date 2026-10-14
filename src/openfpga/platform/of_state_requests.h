#ifndef OF_STATE_REQUESTS_H
#define OF_STATE_REQUESTS_H
#include "runner.h"
#include <stddef.h>
#include <stdint.h>
void ofSavestateStartup(void);
void ofSavestateInit(Runner *runner);
/* Optional streaming reads must not compete with the Pocket state command. */
bool ofSavestateDeferFileReads(void);
const char *ofSavestateReason(Runner *runner);
bool ofSavestateCapture(Runner *runner, uint8_t **data, size_t *size, const char **reason);
bool ofSavestateRestore(Runner *runner, const uint8_t *data, size_t size, const char **reason);
/* True after a save/load pause: the loop must rebase its pacing clock. */
bool platformSnapshotBoundary(Runner *runner);
void ofSavestateMenuPoll(void);
void ofPlatformSnapshotNotice(const char *text);
void ofPlatformSnapshotGet(uint32_t values[3]);
void ofPlatformSnapshotResumed(Runner *runner, const uint32_t values[3]);
#endif
