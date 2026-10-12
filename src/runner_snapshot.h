#ifndef _BS_RUNNER_SNAPSHOT_H_
#define _BS_RUNNER_SNAPSHOT_H_

#include "runner.h"
#include <stddef.h>
#include <stdint.h>

/* Prepared snapshots own every allocation needed by apply. Apply performs no
 * allocation or game event dispatch. Free is valid before or after apply. */
typedef struct RunnerSnapshot RunnerSnapshot;
/* Allocation-free resource preflight. A NULL result means the core resources
 * appear supported; capture still validates the complete value graph. */
const char *Runner_snapshotCheck(Runner *r);
bool Runner_snapshotCapture(Runner *r, uint64_t gameId,
                            uint8_t **data, size_t *size, const char **reason);
RunnerSnapshot *Runner_snapshotPrepare(Runner *r, uint64_t gameId,
                                       const uint8_t *data, size_t size,
                                       const char **reason);
void Runner_snapshotApply(Runner *r, RunnerSnapshot *snapshot);
void Runner_snapshotFree(RunnerSnapshot *snapshot);

#endif
