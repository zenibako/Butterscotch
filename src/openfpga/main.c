/*
 * undertale — Butterscotch (GameMaker: Studio runner) on openfpgaOS
 *
 * Build: make
 * Copy:  make copy
 * Test:  make test   (desktop; expects data.win in the current directory)
 */

#include "of.h"

#include "loop.h"
#include "platform/of_diag.h"
#include "platform/ut_bench.h"
#include "platform/of_hooks.h"
#include "stb_ds.h"

#ifdef OF_PC
extern bool swrTileLayerFast;
extern bool swrMirrorMerge;      /* butterscotch/src/sw/sw_drawing.c */
extern int swrMirrorFaintAlpha;
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Data slots (see the instance JSON under dist/). Slot 4 holds the game's
 * data.win, slot 5 the optional texture pack built from it, slot 6 the
 * optional music pack. */
/* The Makefile names the game being built (GAME=...). */
#ifndef UT_GAME_NAME
#define UT_GAME_NAME "undertale"
#endif

#define UT_SLOT_DATA_WIN 4
#define UT_DATA_WIN_NAME "data.win"
#define UT_SLOT_TEXTURES 5
#define UT_TEXTURES_NAME "textures.bin"
#define UT_SLOT_MUSIC 6
#define UT_MUSIC_NAME "music.bin"

#ifdef OF_PC
/* In loop.c, which has no header for it: writes the input recording out. */
void saveInputRecording(void);
#endif

int main(int argc, char **argv) {
#ifndef OF_PC
    of_file_slot_register(UT_SLOT_DATA_WIN, UT_DATA_WIN_NAME);
    of_file_slot_register(UT_SLOT_TEXTURES, UT_TEXTURES_NAME);
    of_file_slot_register(UT_SLOT_MUSIC, UT_MUSIC_NAME);
#endif
    utDiagInstall();
    utDiagCheckFile(UT_DATA_WIN_NAME);

    CommandLineArgs args = {0};
    args.exitAtFrame = -1;
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 0.0;
    args.osType = OS_WINDOWS;
    args.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    args.lazyRooms = true;
    args.lazyTextures = true;
    args.lazyAudio = true;
    args.renderer = SOFTWARE;
    args.dataWinPath = UT_DATA_WIN_NAME;
    for (int i = 0; i < argc; i++) {
        /* --bench measures accuracy mode and --bench-smooth speed mode,
         * whichever the player would start in. --bench-lowres keeps every
         * room at 320x240 with plain point sampling, for bitstreams whose
         * 640x480 mode misbehaves. */
        bool lowres = strcmp(argv[i], "--bench-lowres") == 0;
        bool smooth = strcmp(argv[i], "--bench-smooth") == 0;
        if (lowres || smooth || strcmp(argv[i], "--bench") == 0) {
            args.seed = 7;
            args.hasSeed = true;
            utPlatformSetSmoothLowres(smooth);
            if (lowres) utPlatformSetHiresAllowed(false);
            utBenchStart();
        }
        if (strcmp(argv[i], "--debug") == 0) utPlatformSetDebugMode(true);
        if (strcmp(argv[i], "--mute") == 0) utAudioSetMuted(true); /* as Select + Down in debug mode */
        /* Game-script times in the log every 150 frames (see utPerfScriptReport): which scripts a section's
         * "step" and unexplained "draw" time are. Timing every script call slows the run, so a benchmark
         * with this on is for the names, not for its totals. */
        if (strcmp(argv[i], "--scripts") == 0) utPlatformSetScriptProfile(150);
        /* --draw-every <n>: only every nth frame is drawn, the rest skipped as speed mode skips them. With a
         * benchmark and a large n this times everything but the drawing: what a skipped frame costs. */
        if (strcmp(argv[i], "--draw-every") == 0 && i + 1 < argc) utPlatformSetForcedSkip(atoi(argv[++i]));
    }

#ifdef OF_PC
    if (getenv("UT_SCRIPT") != NULL) utPlatformSetInputScript(getenv("UT_SCRIPT"));
    if (getenv("UT_UNCAPPED") != NULL) utPlatformSetUncapped(true);
    if (getenv("UT_SMOOTH") != NULL) utPlatformSetSmoothLowres(true);
    /* UT_NO_MIRROR=1 draws mirrored layers the ordinary way; UT_MIRROR_FAINT=<alpha of 256> sets how faint
     * a mirrored layer has to be to be left out (4 leaves none out). For comparing frames. */
    if (getenv("UT_NO_MIRROR") != NULL) swrMirrorMerge = false;
    if (getenv("UT_NO_TILELAYER") != NULL) swrTileLayerFast = false;
    if (getenv("UT_MIRROR_FAINT") != NULL) utPlatformSetMirrorFaint(atoi(getenv("UT_MIRROR_FAINT")));
    /* UT_SKIP=<n> draws only every nth frame, to check that a skipped frame leaves the game where a drawn
     * one would. */
    if (getenv("UT_SKIP") != NULL) utPlatformSetForcedSkip(atoi(getenv("UT_SKIP")));
    if (getenv("UT_DEBUG") != NULL) utPlatformSetDebugMode(true);
    /* UT_PROFILE=<frames> logs the heaviest game scripts every that many frames (the report the menu's script times give
     * on the device). */
    if (getenv("UT_PROFILE") != NULL) utPlatformSetScriptProfile(atoi(getenv("UT_PROFILE")));
#ifdef ENABLE_VM_OPCODE_PROFILER
    /* `make ops` builds with this: the ranking of bytecode instructions executed, printed on the way out. */
    args.opcodeProfiler = true;
#endif
    /* UT_RECORD=<file> writes every key press and release, by frame, when the run ends; UT_PLAYBACK=<file>
     * replays one. Both together replay and then go on recording, to extend a recording. */
    args.recordInputsPath = getenv("UT_RECORD");
    args.playbackInputsPath = getenv("UT_PLAYBACK");
    if (args.recordInputsPath != NULL) atexit(saveInputRecording);
    /* UT_EXIT_FRAME=<n> leaves the main loop at that frame, the ordinary way out: the opcode ranking of
     * `make ops` is only printed on that path (a frame dump exits on the spot). */
    if (getenv("UT_EXIT_FRAME") != NULL) args.exitAtFrame = atoi(getenv("UT_EXIT_FRAME"));
    /* UT_DUMP_STATE=<frame> prints every instance and its variables at that
     * frame; UT_DISASM=<code entry name, or *> prints its bytecode at start. */
    if (getenv("UT_DUMP_STATE") != NULL) {
        int frame = atoi(getenv("UT_DUMP_STATE"));
        hmput(args.dumpFrames, frame, true);
    }
    if (getenv("UT_DISASM") != NULL) shput(args.disassemble, getenv("UT_DISASM"), true);
    /* UT_SEED=<n> fixes the game's RNG so desktop runs are repeatable. */
    const char *seed = getenv("UT_SEED");
    if (seed != NULL) {
        args.seed = atoi(seed);
        args.hasSeed = true;
    }
#endif
#ifdef UT_TRACE_FRAMES
    args.traceFrames = true;
#endif

    int ret = loop(args, UT_GAME_NAME);
    freeCommandLineArgs(&args);
    return ret;
}
