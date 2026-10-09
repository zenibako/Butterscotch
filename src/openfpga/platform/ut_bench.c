#include "ut_bench.h"

#include "of.h"

#include "gettime.h"
#include "of_diag.h"
#include "of_perf.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char *name;
    int lastFrame;
} UtBenchSection;

#ifdef UT_GAME_DELTARUNE
#define UT_BENCH_TITLE "Deltarune ch. 1"

/* The opening is played as it comes (left and confirm, which gets through the
 * first text and into the character creation screens). The town and the Dark
 * World are a long way into the game, so the run jumps to them: the party and
 * the dark-zone flag are set the frame before each jump. In the field the
 * walk to the right runs into the room's enemy, and confirm plays the battle
 * out. Room numbers and frame counts are for the chapter 1 data file. */
#define UT_DR_YARD 2100
#define UT_DR_FIELD 2600
static const char g_script[] =
    "150:L*4,160:Z,174:L*4,184:Z,198:L*4,208:Z,222:L*4,232:Z,246:L*4,256:Z,270:L*4,280:Z,294:L*4,304:Z,318:L*4,"
    "328:Z,342:L*4,352:Z,366:L*4,376:Z,390:L*4,400:Z,414:L*4,424:Z,438:L*4,448:Z,462:L*4,472:Z,486:L*4,496:Z,"
    "510:L*4,520:Z,534:L*4,544:Z,558:L*4,568:Z,582:L*4,592:Z,606:L*4,616:Z,630:L*4,640:Z,654:L*4,664:Z,678:L*4,"
    "688:Z,702:L*4,712:Z,726:L*4,736:Z,750:L*4,760:Z,774:L*4,784:Z,798:L*4,808:Z,822:L*4,832:Z,846:L*4,856:Z,"
    "870:L*4,880:Z,894:L*4,904:Z,918:L*4,928:Z,942:L*4,952:Z,966:L*4,976:Z,990:L*4,1000:Z,1014:L*4,1024:Z,1038:L*4,"
    "1048:Z,1062:L*4,1072:Z,1086:L*4,1096:Z,1110:L*4,1120:Z,1134:L*4,1144:Z,1158:L*4,1168:Z,1182:L*4,1192:Z,"
    "1206:L*4,1216:Z,1230:L*4,1240:Z,1254:L*4,1264:Z,1278:L*4,1288:Z,1302:L*4,1312:Z,1326:L*4,1336:Z,1350:L*4,"
    "1360:Z,1374:L*4,1384:Z,1398:L*4,1408:Z,1422:L*4,1432:Z,1446:L*4,1456:Z,1470:L*4,1480:Z,1494:L*4,1504:Z,"
    "1518:L*4,1528:Z,1542:L*4,1552:Z,1566:L*4,1576:Z,1590:L*4,1600:Z,1614:L*4,1624:Z,1638:L*4,1648:Z,1662:L*4,"
    "1672:Z,1686:L*4,1696:Z,1710:L*4,1720:Z,1734:L*4,1744:Z,1758:L*4,1768:Z,1782:L*4,1792:Z,1806:L*4,1816:Z,"
    "1830:L*4,1840:Z,1854:L*4,1864:Z,1878:L*4,1888:Z,1902:L*4,1912:Z,1926:L*4,1936:Z,1950:L*4,1960:Z,1974:L*4,"
    "1984:Z,1998:L*4,2008:Z,2022:L*4,2032:Z,2046:L*4,2056:Z,2150:R*120,2290:D*60,2370:L*100,2490:U*60,2720:R*500,"
    "3000:Z,3015:Z,3030:Z,3045:Z,3060:Z,3075:Z,3090:Z,3105:Z,3120:Z,3135:Z,3150:Z,3165:Z,3180:Z,3195:Z,3210:Z,"
    "3225:Z,3240:Z,3255:Z,3270:Z,3285:Z,3300:Z,3315:Z,3330:Z,3345:Z,3360:Z,3375:Z,3390:Z,3405:Z,3420:Z,3435:Z,"
    "3450:Z,3465:Z,3480:Z,3495:Z,3510:Z,3525:Z,3540:Z,3555:Z,3570:Z,3585:Z";

static const UtJump g_jumps[] = {
    { UT_DR_YARD - 1, -1, "plot=60,interact=0" }, /* past the scene that would otherwise start in the yard */
    { UT_DR_YARD, 7, NULL },
    { UT_DR_FIELD - 1, -1, "darkzone=1,char[1]=2,char[2]=3,plot=60,interact=0" },
    { UT_DR_FIELD, 52, NULL },
};

static const UtBenchSection g_sections[] = {
    { "opening text 320x240", 1700 },
    { "creation screens 320", 2100 },
    { "town yard 320x240", 2600 },
    { "field (640x480)", 3050 },
    { "battle (640x480)", 3600 },
};
/* The audio pack is about 43 MB against Undertale's 134; the read tests use the same offsets, scaled. */
#define UT_IO_MB(n) ((n) / 3u)
#define UT_IO_ASYNC_MB(i) (35u + (i))
#else
#define UT_BENCH_TITLE "Undertale"

/* Key presses that walk from boot to the first battle; see runInputScript()
 * in of_platform.c for the syntax. Frame numbers assume a fresh save state. */
static const char g_script[] =
    "30:Z,100:Z,160:Z,220:Z,230:D,238:D,246:D,254:D,262:D,270:D,278:D,286:D,300:R,308:R,330:Z,370:R,380:Z,700:R*400,1100:L*10,1120:U*400,1560:Z,1605:Z,1650:Z,1695:Z,1740:Z,1785:Z,1830:Z,1875:Z,1920:Z,1965:Z,2010:Z,2055:Z,2100:Z,2145:Z,2190:Z,2235:Z,2280:Z,2325:Z,2370:Z,2415:Z,2460:Z,2505:Z,2550:Z,2595:Z,2640:Z,2685:Z";

/* Sections end on these frames; they follow the script above. */
static const UtBenchSection g_sections[] = {
    { "intro, menu, naming", 650 },
    { "first room 320x240", 1220 },
    { "Flowey talk 320x240", 2060 },
    { "Flowey battle (640x480)", 2600 },
};
#define UT_IO_MB(n) (n)
#define UT_IO_ASYNC_MB(i) (110u + 2u * (i))
#endif
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
#ifdef UT_GAME_DELTARUNE
    utPlatformSetJumps(g_jumps, (int) (sizeof(g_jumps) / sizeof(g_jumps[0])));
#endif
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
    uint8_t *buffer = malloc(UT_IO_BYTES);
    if (buffer == NULL) return 0;

    FILE *file = fopen(UT_IO_FILE, "rb");
    if (file == NULL) {
        free(buffer);
        return 0;
    }
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
    free(buffer);

    return micros > 0 ? (unsigned) ((uint64_t) done * 1000000u / 1024u / micros) : 0;
}

static void ioReport(void) {
    /* Offsets are spread through the second half of the pack. */
    utLogPrint("SD read, KB/s, 1 MB each from " UT_IO_FILE ":\n");
    utLogPrint("cold: 4K %u, 64K %u, 1M %u\n", ioTest(UT_IO_MB(70u), 4096), ioTest(UT_IO_MB(85u), 65536), ioTest(UT_IO_MB(100u), UT_IO_BYTES));
    utLogPrint("same 64K region again: %u\n", ioTest(UT_IO_MB(85u), 65536));
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
        uint32_t micros = asyncTime(slot, UT_IO_ASYNC_MB((uint32_t) i) * 1024u * 1024u, stage, false, sizes[i],
                                    sizes[i] * UT_ASYNC_READS_PER_SIZE);
        unsigned tenths = micros / (100u * UT_ASYNC_READS_PER_SIZE);
        at += snprintf(line + at, sizeof(line) - (size_t) at, " %uK %u.%u", (unsigned) (sizes[i] / 1024u), tenths / 10, tenths % 10);
    }
    utLogPrint("%s\n", line);

    uint32_t total = UT_IO_BYTES - UT_IO_BYTES % maxRead;
    uint32_t staged = asyncTime(slot, UT_IO_MB(118u) * 1024u * 1024u, stage, false, maxRead, total);
    uint8_t *heapBuffer = malloc(UT_IO_BYTES);
    uint32_t copied = heapBuffer != NULL ? asyncTime(slot, UT_IO_MB(120u) * 1024u * 1024u, heapBuffer, true, maxRead, total) : 0;
    free(heapBuffer);
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
    utLogPrint("=== " UT_BENCH_TITLE " benchmark ===\n");
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
