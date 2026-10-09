#ifndef UT_BENCH_H
#define UT_BENCH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Built-in benchmark: a fixed, scripted run through a few scenes of the game
 * the core was built for, with a fixed seed and no frame pacing. Undertale's
 * plays the opening (intro skip, naming, the first room, Flowey's dialogue
 * and the first battle); Deltarune's plays its opening, then jumps to the
 * town, a Dark World field and a battle there. It reports the average time per
 * frame for each section, so two builds, OS versions or CPU variants can be
 * compared on the device. Started with "--bench" in the app arguments.
 */
void utBenchStart(void);
/* Call once per presented frame. Prints the report and halts at the end. */
void utBenchFrame(void);

/* Time spent inside the display flip, reported separately from the rest. */
void utBenchAddFlipTime(uint64_t nanos);

/* Platform hooks the benchmark drives. */
bool utPlatformShowLogAndHalt(void);
void utPlatformSetHiresAllowed(bool allowed);
void utPlatformSetSmoothLowres(bool enabled);
void utSaveFsSetVolatile(bool enabled);
void utPlatformSetInputScript(const char *script);
/* On `frame` of a scripted run: set the globals in `set` ("name=1,other[2]=3", or NULL) and go to `room`
 * (an index, or -1 to stay). The table must outlive the run. */
typedef struct {
    int frame;
    int room;
    const char *set;
} UtJump;
void utPlatformSetJumps(const UtJump *jumps, int count);
/* Turns debug mode (overlays and Select-chord hotkeys) on from start-up; see of_platform.c. */
void utPlatformSetDebugMode(bool enabled);
/* Turns the script profiler on from the start, reporting every `frames` frames (0: the default). */
void utPlatformSetScriptProfile(int frames);
void utPlatformSetUncapped(bool uncapped);
void utPlatformSetMirrorFaint(int alpha);
void utPlatformSetForcedSkip(int every);

#endif /* UT_BENCH_H */
