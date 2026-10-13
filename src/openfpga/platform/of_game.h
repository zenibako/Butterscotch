#ifndef OF_GAME_H
#define OF_GAME_H

/*
 * What differs between the games this port runs. Each game has one file,
 * game_<name>.c, and the Makefile compiles the one GAME names; everything
 * else in platform/ is the same for every game.
 */

#include <stddef.h>
#include <stdint.h>

#include "data_win.h"
#include "of_bench.h"
#include "vm.h"

/* A game string replaced as loaded (see of_strings.c). */
typedef struct {
    const char *from;
    const char *to;
} OfStringPatch;

/* A code entry or script written out in C (see of_native.c). It goes in only
 * if the game's bytecode for it has exactly this length and hash. */
typedef struct {
    const char *name;
    uint32_t length;
    uint64_t hash;
    VMNativeCode native;
} OfNativeCode;

typedef struct {
    const char *name;
    uint32_t length;
    uint64_t hash;
    VMNativeScript native;
} OfNativeScript;

/* A stretch of the benchmark run, ending on lastFrame. */
typedef struct {
    const char *name;
    int lastFrame;
    const char *tag; /* five letters at most, for the breakdown table */
} OfBenchSection;

#define OF_BENCH_MAX_SECTIONS 8

typedef struct {
    /* Prompts that name keyboard keys, beyond the ones every game shares. */
    const OfStringPatch *strings;
    size_t stringCount;
    /* Changes to the game's code, after its strings are patched (or NULL). */
    void (*patchCode)(DataWin *dw);

    const OfNativeCode *natives;
    size_t nativeCount;
    const OfNativeScript *nativeScripts;
    size_t nativeScriptCount;
    /* Called for each new VM (a game_change gives one), to forget its ids (or NULL). */
    void (*nativesForgetVm)(void);

    /* The built-in benchmark (see of_bench.h). */
    const char *benchTitle;
    const char *benchScript;
    const OfJump *benchJumps;
    int benchJumpCount;
    const OfBenchSection *benchSections;
    int benchSectionCount;
    /* The SD read tests read megabyte n of the audio pack as n / ioDivisor, and the
     * async ones megabyte ioAsyncBase + i * ioAsyncStep, to stay inside a smaller pack. */
    uint32_t ioDivisor;
    uint32_t ioAsyncBase;
    uint32_t ioAsyncStep;
} OfGame;

extern const OfGame ofGame;

#endif /* OF_GAME_H */
