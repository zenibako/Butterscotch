#include "ut_bench.h"

#include "of.h"

#include "gettime.h"
#include "of_diag.h"
#include "of_perf.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Key presses that walk from boot to the first battle; see runInputScript()
 * in of_platform.c for the syntax. Frame numbers assume a fresh save state. */
static const char g_script[] =
    "30:Z,100:Z,160:Z,220:Z,230:D,238:D,246:D,254:D,262:D,270:D,278:D,286:D,300:R,308:R,330:Z,370:R,380:Z,700:R*400,1100:L*10,1120:U*400,1560:Z,1605:Z,1650:Z,1695:Z,1740:Z,1785:Z,1830:Z,1875:Z,1920:Z,1965:Z,2010:Z,2055:Z,2100:Z,2145:Z,2190:Z,2235:Z,2280:Z,2325:Z,2370:Z,2415:Z,2460:Z,2505:Z,2550:Z,2595:Z,2640:Z,2685:Z";

typedef struct {
    const char *name;
    int lastFrame;
} UtBenchSection;

/* Sections end on these frames; they follow the script above. */
static const UtBenchSection g_sections[] = {
    { "intro, menu, naming", 650 },
    { "first room 320x240", 1220 },
    { "Flowey talk 320x240", 2060 },
    { "Flowey battle (640x480)", 2600 },
};
#define UT_BENCH_SECTIONS ((int) (sizeof(g_sections) / sizeof(g_sections[0])))

static bool g_running = false;
static int g_frame = 0;
static int g_section = 0;
static uint64_t g_startNanos = 0;
static uint64_t g_sectionStart = 0;
static unsigned g_sectionMs[UT_BENCH_SECTIONS];
static uint64_t g_flipNanos = 0; /* time inside the display flip this section */
static unsigned g_sectionFlipMs[UT_BENCH_SECTIONS];
static unsigned g_firstFrameMs = 0;

void utBenchStart(void) {
    g_running = true;
    g_startNanos = nowNanos();
    utPlatformSetInputScript(g_script);
    utPlatformSetUncapped(true);
    utSaveFsSetVolatile(true);
}

/* SD read throughput, cold and warm.
 *
 * Earlier versions of this test read the same 2 MB of data.win several times
 * and reported 13 MB/s for everything but the first pass, while the loader,
 * reading the file for the first time, managed about 1.2 MB/s. That pattern
 * says repeat reads are served from a cache somewhere below the app. So each
 * test here reads its own, previously untouched megabyte of music.bin (far
 * larger than anything played during the benchmark), and one region is read
 * twice to show the warm figure. */
#define UT_IO_BYTES (1024u * 1024u)
#define UT_IO_FILE "music.bin"

static unsigned ioTest(uint32_t megabyteOffset, uint32_t chunk) {
    static uint8_t buffer[UT_IO_BYTES] __attribute__((aligned(512)));

    FILE *file = fopen(UT_IO_FILE, "rb");
    if (file == NULL) return 0;
    setvbuf(file, NULL, _IONBF, 0);
    fseek(file, (long) megabyteOffset * 1024 * 1024, SEEK_SET);

    uint64_t start = nowNanos();
    uint32_t done = 0;
    while (done < UT_IO_BYTES) {
        size_t got = fread(buffer, 1, chunk, file);
        if (got == 0) break;
        done += (uint32_t) got;
    }
    uint64_t micros = (nowNanos() - start) / 1000u;
    fclose(file);

    return micros > 0 ? (unsigned) ((uint64_t) done * 1000000u / 1024u / micros) : 0;
}

static void ioReport(void) {
    /* Offsets are spread through the second half of the 130 MB pack. */
    utLogPrint("SD read, KB/s, 1 MB each from " UT_IO_FILE ":\n");
    utLogPrint("cold: 4K %u, 64K %u, 1M %u\n", ioTest(70, 4096), ioTest(85, 65536), ioTest(100, UT_IO_BYTES));
    utLogPrint("same 64K region again: %u\n", ioTest(85, 65536));
}

/* The same pack read with the OS's non-blocking call instead of fread.
 * fread is served from the OS file cache, which fetches one 32 KB block per
 * command to the Pocket's host however much was asked for, and each command
 * has a round trip of its own. This times single commands of several sizes,
 * to show how much of a read is that round trip and how much is data, and
 * then a megabyte in the largest commands the OS accepts: into the OS's
 * staging memory (no copy) and into ordinary memory (one copy). */
#ifndef OF_PC
#define UT_ASYNC_TIMEOUT_NANOS 3000000000ull
#define UT_ASYNC_READS_PER_SIZE 8

static volatile int g_asyncDone;
static volatile int g_asyncResult;

/* Runs from the read-completion interrupt. */
static void asyncCallback(int token, int result) {
    (void) token;
    g_asyncResult = result;
    g_asyncDone = 1;
}

/* Reads `total` bytes from `offset` in commands of `length`. Returns the
 * time in microseconds, or 0 if a read failed or timed out. */
static uint32_t asyncTime(uint32_t slot, uint32_t offset, uint8_t *dest, bool advanceDest, uint32_t length, uint32_t total) {
    uint64_t start = nowNanos();
    for (uint32_t done = 0; done < total; done += length) {
        g_asyncDone = 0;
        g_asyncResult = 0;
        uint64_t issued = nowNanos();
        if (of_file_read_async((int) slot, offset + done, advanceDest ? dest + done : dest, length, asyncCallback) < 0) return 0;
        /* The poll is the OS's fallback for a lost interrupt. */
        while (!g_asyncDone && of_file_async_poll() != 1) {
            if (nowNanos() - issued > UT_ASYNC_TIMEOUT_NANOS) return 0;
        }
        if (g_asyncResult < 0) return 0;
    }
    return (uint32_t) ((nowNanos() - start) / 1000u);
}

static void asyncReport(void) {
    static uint8_t heapBuffer[UT_IO_BYTES] __attribute__((aligned(512)));
    static const uint32_t sizes[] = { 4096, 16384, 32768, 65536 };
    char line[64];
    int at;

    uint32_t slot = 0;
    uint32_t maxRead = of_file_async_max_read();
    uint8_t *stage = maxRead > 0 ? of_file_dma_stage_alloc(maxRead, 4096) : NULL;
    if (stage == NULL || of_file_slot_find(UT_IO_FILE, &slot) != 0) {
        utLogPrint("async read: not available (max %u)\n", (unsigned) maxRead);
        return;
    }

    at = snprintf(line, sizeof(line), "async max %uK ms:", (unsigned) (maxRead / 1024u));
    for (int i = 0; i < 4; i++) {
        if (sizes[i] > maxRead) continue;
        /* Each size gets its own untouched stretch of the pack. */
        uint32_t micros = asyncTime(slot, (110u + 2u * (uint32_t) i) * 1024u * 1024u, stage, false, sizes[i],
                                    sizes[i] * UT_ASYNC_READS_PER_SIZE);
        unsigned tenths = micros / (100u * UT_ASYNC_READS_PER_SIZE);
        at += snprintf(line + at, sizeof(line) - (size_t) at, " %uK %u.%u", (unsigned) (sizes[i] / 1024u), tenths / 10, tenths % 10);
    }
    utLogPrint("%s\n", line);

    uint32_t total = UT_IO_BYTES - UT_IO_BYTES % maxRead;
    uint32_t staged = asyncTime(slot, 118u * 1024u * 1024u, stage, false, maxRead, total);
    uint32_t copied = asyncTime(slot, 120u * 1024u * 1024u, heapBuffer, true, maxRead, total);
    utLogPrint("async 1M KB/s: staged %u, copied %u\n",
               staged > 0 ? (unsigned) ((uint64_t) total * 1000000u / 1024u / staged) : 0,
               copied > 0 ? (unsigned) ((uint64_t) total * 1000000u / 1024u / copied) : 0);
}
#else
static void asyncReport(void) {}
#endif

void utBenchAddFlipTime(uint64_t nanos) {
    if (g_running) g_flipNanos += nanos;
}

void utBenchFrame(void) {
    if (!g_running) return;

    uint64_t now = nowNanos();
    if (g_frame == 0) {
        g_firstFrameMs = (unsigned) ((now - g_startNanos) / 1000000u);
        g_sectionStart = now;
    }
    g_frame++;
    if (g_frame < g_sections[g_section].lastFrame) return;

    g_sectionMs[g_section] = (unsigned) ((now - g_sectionStart) / 1000000u);
    g_sectionFlipMs[g_section] = (unsigned) (g_flipNanos / 1000000u);
    g_sectionStart = now;
    g_flipNanos = 0;
    if (++g_section < UT_BENCH_SECTIONS) return;

    g_running = false;
    utLogPrint("=== Undertale benchmark ===\n");
#ifndef OF_PC
    {
        /* Say which OS and bitstream this ran on; tables look alike otherwise. */
        const struct of_capabilities *caps = of_get_caps();
        utLogPrint("OS %u.%u.%u, core variant %u, CPU %u MHz\n", (unsigned) ((caps->os_version >> 16) & 0xFF),
                   (unsigned) ((caps->os_version >> 8) & 0xFF), (unsigned) (caps->os_version & 0xFF),
                   (unsigned) caps->core_variant, (unsigned) (caps->cpu_freq_hz / 1000000u));
    }
#endif
    utLogPrint("load to first frame: %u.%u s\n", g_firstFrameMs / 1000, (g_firstFrameMs % 1000) / 100);
    utLogPrint("%s\n", utLogLoadSummary());
    utLogPrint("%s\n", utLogLoadPhases());
    utLogPrint("ms per frame:          work   total\n");
    int firstFrame = 0;
    unsigned totalMs = 0;
    for (int i = 0; i < UT_BENCH_SECTIONS; i++) {
        unsigned frames = (unsigned) (g_sections[i].lastFrame - firstFrame);
        unsigned total = g_sectionMs[i] * 10u / frames;
        unsigned work = (g_sectionMs[i] - g_sectionFlipMs[i]) * 10u / frames;
        utLogPrint("%-21s %3u.%u  %3u.%u\n", g_sections[i].name, work / 10, work % 10, total / 10, total % 10);
        firstFrame = g_sections[i].lastFrame;
        totalMs += g_sectionMs[i];
    }
    utLogPrint("%d frames in %u.%u s; full speed is 33.3\n", firstFrame, totalMs / 1000, (totalMs % 1000) / 100);
    utLogPrint("work = total minus display flip\n");
    ioReport();
    asyncReport();
    utDiagHalt("benchmark finished");
}
