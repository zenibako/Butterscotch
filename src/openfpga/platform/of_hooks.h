/*
 * The functions Butterscotch calls into this port. Butterscotch declares
 * each one itself next to the call, so without this header nothing checks
 * the definitions here against a prototype.
 */
#ifndef OF_HOOKS_H
#define OF_HOOKS_H

#include "audio_system.h"
#include "file_system.h"
#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

AudioSystem *platformCreateAudioSystem(void);
FileSystem *platformCreateFileSystem(void);
void platformDestroyFileSystem(FileSystem *base);
uint16_t *platformAcquireFramebuffer(int width, int height);
void platformSetNextFramebuffer(uint16_t *framebuffer, int width, int height, int bpp);
/* Port-internal: a file stored in the audio pack (of_audio_system.c). */
bool utAudioReadPackFile(const char *path, uint8_t **outData, uint32_t *outSize);
bool utAudioHasPackFile(const char *path);

/* What is heard (a debug setting): a muted sound runs its course unheard and costs almost nothing. Music is
 * what the game streams or keeps in a file of its own, a sound effect what is embedded in its data. */
typedef enum { UT_AUDIO_NORMAL, UT_AUDIO_DISABLED, UT_AUDIO_MUSIC_ONLY, UT_AUDIO_SOUND_ONLY, UT_AUDIO_MODE_COUNT } UtAudioMode;
void utAudioSetMode(UtAudioMode mode);
UtAudioMode utAudioMode(void);
void platformLog(const logType type, const char *format, va_list va) __attribute__((format(printf, 2, 0)));

#endif
