/* Compile the SDK desktop shim here so snapshot reset can flush its private
 * callback ring under the same mutex. The SDK itself stays read-only. */
#if defined(OF_PC) && defined(UT_PC_SDL_SOURCE)
#include UT_PC_SDL_SOURCE
#include "ut_snapshot_parts.h"
void utPcAudioSnapshotReset(void) {
    SDL_LockMutex(g_audio_mutex);
    g_audio_read_pos = g_audio_write_pos = 0;
    SDL_UnlockMutex(g_audio_mutex);
}
#endif
