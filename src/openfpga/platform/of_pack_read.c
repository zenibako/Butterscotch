/*
 * Exact-size reads of the texture pack.
 *
 * fread on openfpgaOS is served from the OS's file cache, which fetches a
 * whole 32 KB block from the Pocket's host for any miss: about 28 ms however
 * little was asked for. A texture tile is a few KB, so most of each block is
 * wasted, and a battle's first frames read dozens of tiles. of_file_read_async
 * fetches exactly what is asked, at about 3.5 ms plus 0.6 ms per KB.
 *
 * As in the music reader (readDirect in of_audio_system.c), the read is
 * started and then waited for: while one is in flight the OS fails every
 * ordinary read with "busy", so it must finish before anything else reads.
 */

#include "of.h"

#include "gettime.h"
#include "log.h"
#include "of_hooks.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef OF_PC

/* Staging memory for direct reads; smaller sizes are tried if the OS cannot
 * give this much. */
#define UT_PACK_STAGE_BYTES (64u * 1024u)
#define UT_PACK_STAGE_MIN (16u * 1024u)
/* As for music: longer than the 2 s after which the OS drops a stalled read
 * (a screenshot being saved), after which direct reads are not used again. */
#define UT_PACK_TIMEOUT_NANOS 3000000000ull
#define UT_PACK_SETTLE_NANOS 3000000000ull

static bool g_packProbed;
static bool g_packDirect;
static uint32_t g_packSlot;
static uint8_t *g_packStage;
static uint32_t g_packPiece;
static volatile int g_packDone;
static volatile int g_packResult;
static uint64_t g_packRefusedNanos;

uint64_t platformPackRefusedNanos(void) {
    return g_packRefusedNanos;
}

/* Runs from the read-completion interrupt. */
static void packCallback(int token, int result) {
    (void) token;
    g_packResult = result;
    g_packDone = 1;
}

static void packProbe(const char *path) {
    g_packProbed = true;
    uint32_t slot = 0;
    uint32_t maxRead = of_file_async_max_read();
    if (maxRead < UT_PACK_STAGE_MIN || of_file_slot_find(path, &slot) != 0) return;
    uint32_t size = UT_PACK_STAGE_BYTES < maxRead ? UT_PACK_STAGE_BYTES : maxRead;
    for (; size >= UT_PACK_STAGE_MIN && g_packStage == NULL; size /= 2) {
        g_packStage = of_file_dma_stage_alloc(size, 4096);
        if (g_packStage != NULL) g_packPiece = size;
    }
    if (g_packStage == NULL) return;
    g_packSlot = slot;
    g_packDirect = true;
    logInfo("SWR: %s is read directly, up to %u KB at a time.\n", path, (unsigned) (g_packPiece / 1024u));
}

static bool packReadPiece(uint32_t offset, uint32_t size) {
    g_packDone = 0;
    g_packResult = 0;
    uint64_t start = nowNanos();
    /* Refused while the host is busy with something else; that passes quickly. */
    while (of_file_read_async((int) g_packSlot, offset, g_packStage, size, packCallback) < 0) {
        if (nowNanos() - start > 50000000ull) return false;
    }
    g_packRefusedNanos += nowNanos() - start;
    /* The poll is the OS's fallback for a lost interrupt. */
    while (!g_packDone && of_file_async_poll() != 1) {
        if (nowNanos() - start > UT_PACK_TIMEOUT_NANOS) {
            /* Do not go back to ordinary reads while the OS still counts
             * this one as in flight. */
            uint64_t gaveUp = nowNanos();
            while (of_file_async_busy() == 1 && nowNanos() - gaveUp < UT_PACK_SETTLE_NANOS) of_file_async_poll();
            logWarn("SWR: a direct texture read timed out; using fread from here on.\n");
            g_packDirect = false;
            return false;
        }
    }
    return g_packResult >= 0;
}

bool platformPackRead(const char *path, uint32_t offset, uint32_t size, void *dest) {
    if (!g_packProbed) packProbe(path);
    if (!g_packDirect) return false;
    uint8_t *out = dest;
    for (uint32_t done = 0; done < size;) {
        uint32_t want = size - done < g_packPiece ? size - done : g_packPiece;
        if (!packReadPiece(offset + done, want)) return false;
        memcpy(out + done, g_packStage, want);
        done += want;
    }
    return true;
}

#else

uint64_t platformPackRefusedNanos(void) {
    return 0;
}

bool platformPackRead(const char *path, uint32_t offset, uint32_t size, void *dest) {
    (void) path;
    (void) offset;
    (void) size;
    (void) dest;
    return false;
}

#endif
