/*
 * Log sink for Butterscotch: prefixes every line with seconds since start,
 * so load stages can be timed from the on-device terminal.
 */

#include "log.h"
#include "gettime.h"
#include "of_hooks.h"
#include "of_perf.h"
#include "ut_save_format.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The last few lines, kept for the on-screen log overlay. */
static char g_lines[UT_LOG_LINES][UT_LOG_LINE_LEN];
static int g_lineHead = 0; /* line currently being written */
static int g_linePos = 0;

/* Writing a line to the OS console costs about 20 ms on the Pocket, and the
 * console is not visible once the game has its own video mode, so the
 * platform turns it off after the first frame. The overlay buffer is kept. */
static bool g_console = true;

void utLogSetConsole(bool enabled) {
    g_console = enabled;
}

/* The Makefile names these after the game being built. */
#ifndef UT_GAME_NAME
#define UT_GAME_NAME "undertale"
#endif
#ifndef UT_LOG_SLOT_FILE
#define UT_LOG_SLOT_FILE "undertale_1.sav"
#endif

/* Everything logged since start, for utLogDump. When it fills up, the older
 * half goes and a line says so. */
#define UT_LOG_HISTORY (160u * 1024u)
static char g_history[UT_LOG_HISTORY];
static uint32_t g_historyLen = 0;
static bool g_historyCut = false;

static void keepHistory(const char *text) {
    size_t len = strlen(text);
    if (len >= UT_LOG_HISTORY / 2) return;
    if (g_historyLen + len > UT_LOG_HISTORY) {
        /* Drop the older half, from the start of a line. */
        uint32_t from = UT_LOG_HISTORY / 2;
        while (from < g_historyLen && g_history[from - 1] != '\n') from++;
        memmove(g_history, g_history + from, g_historyLen - from);
        g_historyLen -= from;
        g_historyCut = true;
    }
    memcpy(g_history + g_historyLen, text, len);
    g_historyLen += (uint32_t) len;
}

/* Writes the whole log as text to the game's second save slot file
 * (<game>_1.sav), which the core definition already names and no game uses.
 * A core can only write to its save slots, and the Pocket copies those back
 * to the card (Saves/butterscotch/common/) when the core is left through the
 * Analogue menu: this is how a log gets off the device without a screenshot,
 * which holds 21 short lines.
 *
 * What the file held from earlier runs is kept in front of this run's log,
 * as much of its end as fits, so that two benchmarks run one after the other
 * are both there to read. Each run starts with a "Butterscotch log" line.
 * Returns false if the file could not be written. */
#define UT_LOG_EARLIER (72u * 1024u)
#define UT_LOG_HEADER "Butterscotch log, " UT_GAME_NAME "\n"

bool utLogDump(void) {
    /* Read the earlier runs once; later dumps in this run replace only this run's part. */
    static char *earlier = NULL;
    static uint32_t earlierLen = 0;
    static bool earlierRead = false;
    if (!earlierRead) {
        earlierRead = true;
        FILE *old = fopen(UT_LOG_SLOT_FILE, "rb");
        char *text = old != NULL ? malloc(UT_SAVE_SLOT_BYTES) : NULL;
        if (text != NULL) {
            /* In pieces: one read for more than the slot holds came back empty on the device. */
            size_t got = 0;
            while (got < UT_SAVE_SLOT_BYTES - 1) {
                size_t want = UT_SAVE_SLOT_BYTES - 1 - got;
                size_t piece = fread(text + got, 1, want < 4096 ? want : 4096, old);
                if (piece == 0) break;
                got += piece;
            }
            text[got] = '\0';
            size_t len = strlen(text); /* the text ends at the first NUL */
            /* A slot never written, or holding something else, reads as anything at all. */
            if (len > 0 && strncmp(text, "Butterscotch log, ", 18) == 0) {
                size_t from = len > UT_LOG_EARLIER ? len - UT_LOG_EARLIER : 0;
                while (from > 0 && from < len && text[from - 1] != '\n') from++;
                earlierLen = (uint32_t) (len - from);
                memmove(text, text + from, earlierLen);
                earlier = text;
            } else
                free(text);
        }
        if (old != NULL) fclose(old);
    }

    FILE *file = fopen(UT_LOG_SLOT_FILE, "wb");
    if (file == NULL) return false;
    static const char cut[] = "(the start of this run's log no longer fitted and was dropped)\n";
    bool ok = true;
    if (earlier != NULL) {
        ok = fwrite(earlier, 1, earlierLen, file) == earlierLen;
        if (earlierLen > 0 && earlier[earlierLen - 1] != '\n') ok = ok && fputc('\n', file) != EOF;
        ok = ok && fputs("\n", file) >= 0;
    }
    ok = ok && fputs(UT_LOG_HEADER, file) >= 0;
    if (g_historyCut) ok = ok && fputs(cut, file) >= 0;
    ok = ok && fwrite(g_history, 1, g_historyLen, file) == g_historyLen;
    /* Text ends here; whatever the slot held beyond this is cut off by the NUL for a reader that stops at one. */
    ok = ok && fputc('\0', file) != EOF;
    return fclose(file) == 0 && ok;
}

static void keepText(const char *text) {
    keepHistory(text);
    for (; *text != '\0'; text++) {
        if (*text == '\n') {
            g_lineHead = (g_lineHead + 1) % UT_LOG_LINES;
            g_linePos = 0;
            g_lines[g_lineHead][0] = '\0';
        } else if (g_linePos < UT_LOG_LINE_LEN - 1) {
            g_lines[g_lineHead][g_linePos++] = *text;
            g_lines[g_lineHead][g_linePos] = '\0';
        }
    }
}

const char *utLogLine(int age) {
    if (age < 0 || age >= UT_LOG_LINES - 1) return "";
    /* The head line is still being written; age 0 is the last complete one. */
    return g_lines[(g_lineHead - 1 - age + 2 * UT_LOG_LINES) % UT_LOG_LINES];
}

/* Load breakdown: DATAWIN_LOG_CHUNKS makes the loader log "DataWin: NAME, ..."
 * as it starts each chunk. Time between consecutive lines is the time spent
 * on the earlier chunk; the slowest few are kept so the benchmark report can
 * show where loading time goes after the boot log has scrolled away. */
#define UT_LOAD_TOP 4
static char g_chunkName[5];
static uint64_t g_chunkStart = 0;
static struct { char name[5]; unsigned ms; } g_slowest[UT_LOAD_TOP];
static char g_loadSummary[UT_LOG_LINE_LEN];
static char g_loadPhases[UT_LOG_LINE_LEN];

static void finishChunk(uint64_t now) {
    if (g_chunkStart == 0) return;
    unsigned ms = (unsigned) ((now - g_chunkStart) / 1000000u);
    for (int i = 0; i < UT_LOAD_TOP; i++) {
        if (ms <= g_slowest[i].ms) continue;
        for (int j = UT_LOAD_TOP - 1; j > i; j--) g_slowest[j] = g_slowest[j - 1];
        memcpy(g_slowest[i].name, g_chunkName, sizeof(g_chunkName));
        g_slowest[i].ms = ms;
        break;
    }
    g_chunkStart = 0;
}

/* Room and texture loads announce themselves in the log; their time goes
 * into the slow-frame report (see of_perf.h). The host tools share this file
 * but have no frames to report on. */
#ifdef UT_HOST_TOOL
static void trackFrameLoads(const char *format, const char *text, uint64_t now) {
    (void) format; (void) text; (void) now;
}
#else
static void trackFrameLoads(const char *format, const char *text, uint64_t now) {
    static uint64_t roomStart = 0;
    if (strncmp(format, "Room changed:", 13) == 0) {
        roomStart = now;
        utAudioRoomChange();
    } else if (strncmp(format, "Runner: Room loaded:", 20) == 0) {
        if (roomStart != 0) utPerfAddLoad(UT_LOAD_ROOM, now - roomStart);
        roomStart = 0;
    } else if (strncmp(format, "SWR: Loaded TXTR", 16) == 0) {
        const char *took = strstr(text, " took ");
        if (took != NULL) utPerfAddLoad(UT_LOAD_TEXTURE, strtoull(took + 6, NULL, 10) * 1000000u);
    }
}
#endif

static void trackLoad(const char *format, const char *text, uint64_t now) {
    trackFrameLoads(format, text, now);
    if (strncmp(format, "DataWin: phases:", 16) == 0) {
        /* Keep the loader's own phase totals, minus the "DataWin: " prefix. */
        snprintf(g_loadPhases, sizeof(g_loadPhases), "%s", text + 9);
        size_t len = strlen(g_loadPhases);
        if (len > 0 && g_loadPhases[len - 1] == '\n') g_loadPhases[len - 1] = '\0';
    } else if (strncmp(format, "DataWin: %.4s", 13) == 0) {
        finishChunk(now);
        memcpy(g_chunkName, text + 9, 4);
        g_chunkName[4] = '\0';
        g_chunkStart = now;
    } else if (strncmp(format, "Loaded \"", 8) == 0 || strncmp(format, "Unknown chunk", 13) == 0) {
        finishChunk(now);
        int n = snprintf(g_loadSummary, sizeof(g_loadSummary), "slowest chunks:");
        for (int i = 0; i < UT_LOAD_TOP && g_slowest[i].ms > 0; i++)
            n += snprintf(g_loadSummary + n, sizeof(g_loadSummary) - (size_t) n, " %s %u.%us", g_slowest[i].name,
                          g_slowest[i].ms / 1000, (g_slowest[i].ms % 1000) / 100);
    }
}

const char *utLogLoadSummary(void) {
    return g_loadSummary;
}

const char *utLogLoadPhases(void) {
    return g_loadPhases;
}

void utLogPrint(const char *format, ...) {
    char text[256];
    va_list va;
    va_start(va, format);
    vsnprintf(text, sizeof(text), format, va);
    va_end(va);
    if (g_console) fputs(text, stdout);
    keepText(text);
}

void platformLog(const logType type, const char *format, va_list va) {
    static uint64_t start = 0;
    static bool atLineStart = true;
    char text[256];

    uint64_t now = nowNanos();
    if (start == 0) start = now;

    if (atLineStart) {
        unsigned ms = (unsigned) ((now - start) / 1000000u);
        snprintf(text, sizeof(text), "[%3u.%02u] ", ms / 1000, (ms % 1000) / 10);
        if (g_console) fputs(text, stdout);
        keepText(text);
    }

    const char *prefix = "";
    switch (type) {
        case LOG_TYPE_WARNING: prefix = "Warning: "; break;
        case LOG_TYPE_ERROR:   prefix = "Error: ";   break;
        case LOG_TYPE_DEBUG:   prefix = "Debug: ";   break;
        case LOG_TYPE_NORMAL:  break;
    }
    if (g_console) fputs(prefix, stdout);
    keepText(prefix);

    vsnprintf(text, sizeof(text), format, va);
    if (g_console) fputs(text, stdout);
    keepText(text);
    trackLoad(format, text, now);

    size_t len = strlen(format);
    atLineStart = len > 0 && format[len - 1] == '\n';
}
