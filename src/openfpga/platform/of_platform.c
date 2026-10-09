/*
 * openfpgaOS platform backend for Butterscotch.
 *
 * Implements the platform* hooks declared in butterscotch/src/platformdefs.h
 * on top of the of_* API. The software renderer draws X1R5G5B5 pixels, which
 * is exactly OF_VIDEO_MODE_RGB555, at 320x240 or 640x480 depending on the
 * room, so presenting a frame is a mode check, one copy and a flip.
 */

#include "of.h"

#include "common.h"
#include "platformdefs.h"
#include "gettime.h"
#include "runner_keyboard.h"

#include "of_hooks.h"
#include "of_perf.h"
#include "profiler.h"
#include "runner.h"
#include "ut_bench.h"
#include "ut_strings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UT_SCREEN_W OF_SCREEN_W
#define UT_SCREEN_H OF_SCREEN_H
#define UT_HIRES_W 640
#define UT_HIRES_H 480

static Runner *g_runner = NULL;
static uint16_t *g_nextFb = NULL;
static int g_nextW = 0;
static int g_nextH = 0;
static bool g_showingFramebuffer = false;
static const char *g_inputScript = NULL;
static bool g_uncapped = false;
static bool g_hiresAvailable = true;
/* Alternative to 640x480: keep every room at 320x240 and let the renderer
 * average 2x2 texels when it shrinks. Faster, slightly soft small text. */
static bool g_smoothLowres = false;
#define UT_MODE_SHOWN_NANOS 2000000000ull
static uint64_t g_modeShownUntil = 0;
extern bool swrSmoothMinify; /* butterscotch/src/sw/sw_drawing.c */
static int g_modeW = 0; /* 0 until the first frame sets a mode */
static int g_modeH = 0;
static int g_modeStride = 0; /* bytes per row of the display surface */

/* Pad button -> GML virtual key. Undertale reads Z/X/C with Enter/Shift/Ctrl
 * as aliases; the d-pad maps to the arrow keys. Select is not Esc (holding
 * Esc quits Undertale); Select toggles the frame-time overlay, R the log overlay, and L switches
 * 640x480 rooms between native resolution and smoothed 320x240. */
static const struct {
    uint32_t button;
    int32_t key;
} g_keymap[] = {
    { OF_BTN_UP,     VK_UP },
    { OF_BTN_DOWN,   VK_DOWN },
    { OF_BTN_LEFT,   VK_LEFT },
    { OF_BTN_RIGHT,  VK_RIGHT },
    { OF_BTN_A,      'Z' },
    { OF_BTN_B,      'X' },
    { OF_BTN_X,      'C' },
    { OF_BTN_Y,      'C' },
    { OF_BTN_START,  VK_ENTER },
};
#define UT_KEYMAP_COUNT (sizeof(g_keymap) / sizeof(g_keymap[0]))

/* Debug mode: everything on screen or on the buttons that is for looking
 * into the port and not for playing. Off at start-up unless the game was
 * started with --debug; holding Select for two seconds switches it on or
 * off. (The Pocket's core menu cannot do it: the hardware gives a core no
 * menu variables of its own. interact.json entries can only write the
 * Analogizer registers and the app id that an instance file sets.) With it on:
 *   - Select toggles the frame-time overlay and R the log overlay;
 *   - 640x480 rooms keep showing which way L's speed/accuracy toggle is set;
 *   - Butterscotch's own debug hotkeys (see "Debug Features" in its README)
 *     are reached by holding Select and pressing another button, since a
 *     Pocket has no keyboard. Select then acts on release, and a button
 *     pressed with it held does not also reach the game.
 * Each action that would otherwise leave the screen as it was says so in
 * the top right corner: switching debug mode itself, changing room, clearing
 * global.interact, pausing ("Paused, frame N", which a step advances) and
 * resuming.
 * With it off none of that is drawn or reacts, and what was showing is hidden. */
#define UT_DEBUG_HOLD_NANOS 2000000000ull
/* `notice` is what the top right corner says for two seconds when the hotkey
 * is used. Pause and step have none: a paused game says "Paused, frame N"
 * there for as long as it is paused, and a step changes N. */
static const struct {
    uint32_t button;
    int32_t key;
    const char *what;
    const char *notice;
} g_debugChords[] = {
    { OF_BTN_RIGHT, VK_PAGEUP,   "next room",                     "Next room" },
    { OF_BTN_LEFT,  VK_PAGEDOWN, "previous room",                 "Previous room" },
    { OF_BTN_START, VK_F8,       "pause on/off",                  NULL },
    { OF_BTN_A,     'O',         "step one frame (while paused)", NULL },
    { OF_BTN_B,     VK_F10,      "clear global.interact",         "interact = 0" },
};
#define UT_DEBUG_CHORD_COUNT (sizeof(g_debugChords) / sizeof(g_debugChords[0]))
static bool g_debugRequested = false;
static bool g_debugMode = false;

/* A few words in the top right corner for two seconds: what a button that
 * changes nothing else on screen just did. */
#define UT_NOTICE_NANOS 2000000000ull
static char g_notice[32];
static uint64_t g_noticeUntil = 0;

static void showNotice(const char *text) {
    snprintf(g_notice, sizeof(g_notice), "%s", text);
    g_noticeUntil = nowNanos() + UT_NOTICE_NANOS;
}

void utPlatformSetDebugMode(bool enabled) {
    g_debugRequested = enabled;
}

/* Script times (Select + X in debug mode, UT_PROFILE on desktop): Butterscotch's
 * GML profiler, reported to the log every so many frames. Timing every script
 * call costs time itself, so it is off until asked for. */
#define UT_PROFILE_FRAMES 60
static int g_profileFrames = 0;
static bool g_profileRequested = false;

void utPlatformSetScriptProfile(int frames) {
    g_profileFrames = frames > 0 ? frames : UT_PROFILE_FRAMES;
    g_profileRequested = true;
}

static bool scriptProfileOn(void) {
    return g_runner != NULL && g_runner->vmContext->profiler != NULL;
}

static void setScriptProfile(bool enabled) {
    if (g_runner == NULL) return;
    if (g_profileFrames <= 0) g_profileFrames = UT_PROFILE_FRAMES;
    Profiler_setEnabled(&g_runner->vmContext->profiler, enabled);
}

/* Once per presented frame. */
static void scriptProfileFrame(void) {
    static int frames = 0;
    if (!scriptProfileOn()) {
        frames = 0;
        return;
    }
    if (++frames < g_profileFrames) return;
    utPerfScriptReport(g_runner->vmContext->profiler, frames);
    Profiler_reset(g_runner->vmContext->profiler);
    frames = 0;
}

void utPlatformSetInputScript(const char *script) {
    g_inputScript = script;
}

void utPlatformSetSmoothLowres(bool enabled) {
    g_smoothLowres = enabled;
}

void utPlatformSetHiresAllowed(bool allowed) {
    g_hiresAvailable = allowed;
}

void utPlatformSetUncapped(bool uncapped) {
    g_uncapped = uncapped;
}

bool platformInit(int32_t reqW, int32_t reqH, const char *title, bool headless) {
    (void) reqW;
    (void) reqH;
    (void) title;
    (void) headless;

    /* Stay on the text terminal until the first frame is ready, so load-time
     * log lines (and any early failure) are visible on the device. */
    of_video_init();
    of_video_set_color_mode(OF_VIDEO_MODE_RGB555);
    of_video_set_display_mode(OF_DISPLAY_TERMINAL);
    return true;
}

void platformExit(void) {
}

void platformInitFunctions(Runner *runner) {
    g_runner = runner;
    utPatchStrings(runner->dataWin);
    runner->setCursor = NULL;
    runner->currentCursor = GML_CR_DEFAULT;
}

/* Width of what the current room shows, in game pixels: the view if the room
 * uses one, the whole room otherwise. */
static int32_t visibleWidth(Runner *runner) {
    if (runner == NULL || runner->currentRoom == NULL) return UT_SCREEN_W;

    if (runner->viewsEnabled) {
        for (int i = 0; i < MAX_VIEWS; i++) {
            if (!runner->views[i].enabled) continue;
            GMLCamera *camera = Runner_getCameraForView(runner, i);
            return camera != NULL ? camera->viewWidth : UT_HIRES_W;
        }
    }
    return (int32_t) runner->currentRoom->width;
}

/* The renderer sizes its framebuffer from this. Undertale's window is always
 * 640x480, but overworld rooms show a 320x240 view scaled up 2x, so drawing
 * them at 320x240 loses nothing. Battles and menus use all 640x480 with small
 * fonts, and need the full resolution to stay legible. */
bool platformGetWindowSize(int32_t *outW, int32_t *outH) {
    if (!outW || !outH) return false;
    int32_t shown = visibleWidth(g_runner);
    static int32_t lastShown = 0;
    if (shown != lastShown) {
        logInfo("Video: room shows %d px across\n", (int) shown);
        lastShown = shown;
    }
    bool hires = g_hiresAvailable && !g_smoothLowres && shown > UT_SCREEN_W;
    swrSmoothMinify = g_smoothLowres;
    *outW = hires ? UT_HIRES_W : UT_SCREEN_W;
    *outH = hires ? UT_HIRES_H : UT_SCREEN_H;
    return true;
}

/* Switches the display to match the frame about to be presented. */
static void matchVideoMode(int width, int height) {
    if (width == g_modeW && height == g_modeH) return;

    of_video_mode_t want = { (uint16_t) width, (uint16_t) height, 0, OF_VIDEO_MODE_RGB555, 0 };
    if (of_video_set_mode(&want) < 0) {
        if (width > UT_SCREEN_W) {
            logWarn("Video: %dx%d is not available, staying at %dx%d.\n", width, height, UT_SCREEN_W, UT_SCREEN_H);
            g_hiresAvailable = false;
            return;
        }
        /* An OS without mode setting still has the boot 320x240 mode. */
        g_modeStride = UT_SCREEN_W * (int) sizeof(uint16_t);
    } else {
        of_video_mode_t got;
        of_video_get_mode(&got);
        g_modeStride = got.stride;
        logInfo("Video: %ux%u, stride %u\n", (unsigned) got.width, (unsigned) got.height, (unsigned) got.stride);
    }
    g_modeW = width;
    g_modeH = height;
}

bool platformGetScaledWindowSize(int32_t *outW, int32_t *outH) {
    return platformGetWindowSize(outW, outH);
}

void platformSetWindowSize(int32_t width, int32_t height) {
    (void) width;
    (void) height;
}

void platformSetWindowTitle(const char *title) {
    (void) title;
}

void platformGetMousePos(double *xPos, double *yPos) {
    if (xPos) *xPos = 0.0;
    if (yPos) *yPos = 0.0;
}

/* The renderer draws each frame straight into the display's back buffer, so
 * presenting is just a flip. Returns NULL (renderer keeps its own buffer and
 * the frame is copied) if the mode is unavailable or its rows are padded. */
uint16_t *platformAcquireFramebuffer(int width, int height) {
    matchVideoMode(width, height);
    if (width != g_modeW || height != g_modeH) return NULL;
    if (g_modeStride != width * (int) sizeof(uint16_t)) return NULL;
    return (uint16_t *) (void *) of_video_surface();
}

void platformSetNextFramebuffer(uint16_t *framebuffer, int width, int height, int bpp) {
    if (bpp != 16) {
        g_nextFb = NULL;
        return;
    }
    g_nextFb = framebuffer;
    g_nextW = width;
    g_nextH = height;
}

#ifdef OF_PC
/* Desktop-only verification aid: UT_DUMP_FRAME=<n> writes frame n of the
 * RGB555 output to UT_DUMP_PATH (default frame.ppm) and exits. */
static void writeFrameTo(const char *path, bool thenExit) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) exit(1);
    fprintf(f, "P6\n%d %d\n255\n", g_nextW, g_nextH);
    for (int i = 0; i < g_nextW * g_nextH; i++) {
        uint16_t p = g_nextFb[i];
        uint8_t rgb[3] = {
            (uint8_t) (((p >> 10) & 0x1F) << 3),
            (uint8_t) (((p >> 5) & 0x1F) << 3),
            (uint8_t) ((p & 0x1F) << 3),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    if (thenExit) exit(0);
}

static void writeFrameDump(void) {
    const char *path = getenv("UT_DUMP_PATH");
    writeFrameTo(path != NULL ? path : "frame.ppm", true);
}

/* UT_DUMP_EVERY=<n> also writes every nth frame on the way there, to
 * <UT_DUMP_DIR>/f<frame>.ppm, to follow a long scripted run. */
static void dumpFrameIfRequested(void) {
    static int frame = 0;
    static int target = -2;
    static int every = 0;
    if (target == -2) {
        const char *env = getenv("UT_DUMP_FRAME");
        target = env != NULL ? atoi(env) : -1;
        env = getenv("UT_DUMP_EVERY");
        every = env != NULL ? atoi(env) : 0;
    }
    if (every > 0 && frame > 0 && frame % every == 0) {
        const char *dir = getenv("UT_DUMP_DIR");
        char path[512];
        snprintf(path, sizeof(path), "%s/f%06d.ppm", dir != NULL ? dir : ".", frame);
        writeFrameTo(path, false);
    }
    if (target < 0 || frame++ != target) return;
    writeFrameDump();
}
#endif

void platformSwapBuffers(void) {
    if (g_nextFb == NULL) return;
#ifdef OF_PC
    /* UT_OVERLAY=1 turns both overlays on, to check them in frame dumps. */
    static bool overlaysChecked = false;
    if (!overlaysChecked) {
        overlaysChecked = true;
        if (getenv("UT_OVERLAY") != NULL) {
            utPerfToggle();
            utPerfToggleLog();
        }
    }
#endif
    scriptProfileFrame();
    utPerfFrame(g_nextFb, g_nextW, g_nextH);
    /* L's setting, by the name the instruction screen gives the toggle:
     * for a moment after L is pressed, and in debug mode for as long as a
     * 640x480 room (the only kind it changes) is showing. */
    static bool wasPaused = false;
    bool paused = g_runner != NULL && g_runner->paused;
    if (wasPaused && !paused) showNotice("Resumed");
    wasPaused = paused;
    if (paused) {
        /* A paused game presents this frame and then no more until it is
         * stepped or resumed, so the words stay up with it. That is also
         * why this comes before a notice: one caught here would stay up
         * for as long as the pause. */
        char text[32];
        snprintf(text, sizeof(text), "Paused, frame %d", g_runner->frameCount);
        utPerfDrawMode(g_nextFb, g_nextW, g_nextH, text);
    } else if (nowNanos() < g_noticeUntil) {
        utPerfDrawMode(g_nextFb, g_nextW, g_nextH, g_notice);
    } else if (nowNanos() < g_modeShownUntil || (g_debugMode && visibleWidth(g_runner) > UT_SCREEN_W)) {
        utPerfDrawMode(g_nextFb, g_nextW, g_nextH, g_smoothLowres ? "Speed" : "Accuracy");
    }
    utBenchFrame();
#ifdef OF_PC
    dumpFrameIfRequested();
#endif

    if (!g_showingFramebuffer) {
        of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
        g_showingFramebuffer = true;
    }

    matchVideoMode(g_nextW, g_nextH);
    if (g_nextW != g_modeW || g_nextH != g_modeH) return; /* mode refused; the next frame is drawn at 320x240 */

    uint8_t *dst = of_video_surface();
    size_t rowBytes = (size_t) g_nextW * sizeof(uint16_t);
    if ((uint8_t *) g_nextFb == dst) {
        /* Drawn in place (platformAcquireFramebuffer): nothing to copy. */
    } else if ((size_t) g_modeStride == rowBytes) {
        memcpy(dst, g_nextFb, rowBytes * (size_t) g_nextH);
    } else {
        for (int y = 0; y < g_nextH; y++)
            memcpy(dst + (size_t) y * g_modeStride, g_nextFb + (size_t) y * g_nextW, rowBytes);
    }
    uint64_t flipStart = nowNanos();
#ifdef OF_PC
    /* UT_NOFLIP=1 never presents: the desktop window waits for vsync on every flip, which caps a long scripted
     * run at the display's refresh rate. */
    static int noFlip = -1;
    if (noFlip < 0) noFlip = getenv("UT_NOFLIP") != NULL;
    if (!noFlip)
#endif
    of_video_flip();
    utBenchAddFlipTime(nowNanos() - flipStart);
#ifndef OF_PC
    utLogSetConsole(false);
#endif
    utPerfPhase(UT_PHASE_OTHER);
}

/* Shows the recent log full-screen and never returns. Used for the benchmark
 * report and for fatal errors: the OS text terminal does not display once the
 * app has switched to its own 16-bit video modes, so the text is drawn into
 * the framebuffer instead. Returns false if no frame has been drawn yet, in
 * which case the OS terminal is still on screen and the caller can use it. */
bool utPlatformShowLogAndHalt(void) {
    if (g_nextFb == NULL || !g_showingFramebuffer) return false;

    matchVideoMode(UT_SCREEN_W, UT_SCREEN_H);
    if (g_modeW != UT_SCREEN_W || g_modeH != UT_SCREEN_H) return false;
    if (g_modeStride != UT_SCREEN_W * (int) sizeof(uint16_t)) return false;
    g_nextW = UT_SCREEN_W;
    g_nextH = UT_SCREEN_H;

    for (;;) {
        g_nextFb = (uint16_t *) (void *) of_video_surface();
        utPerfDrawLogScreen(g_nextFb, g_nextW, g_nextH);
#ifdef OF_PC
        if (getenv("UT_DUMP_PATH") != NULL) writeFrameDump();
        exit(0);
#endif
        of_video_flip();
        of_input_poll();
        usleep(100000);
    }
}

void *platformGetProcAddress(const char *name) {
    (void) name;
    return NULL;
}

/* Scripted input for repeatable runs (the benchmark, and UT_SCRIPT on
 * desktop):
 *   "300:Z,340:D,341:Z,400:R*90"
 * presses a key on the given frame and releases it two frames later, or
 * after N frames with "*N".
 * Keys: U D L R (arrows), Z X C, E (Enter). */
static void runInputScript(void) {
    static int frame = 0;
    const char *script = g_inputScript;
    frame++;
#ifdef OF_PC
    /* UT_GOTO="<frame>:<room index>" jumps to a room, to reach a scene without playing up to it. The game's own
     * state is whatever it was, so this only suits rooms that set themselves up. */
    const char *jump = getenv("UT_GOTO");
    if (jump != NULL && g_runner != NULL && frame == atoi(jump) && strchr(jump, ':') != NULL)
        g_runner->pendingRoom = atoi(strchr(jump, ':') + 1);
    /* UT_SET="<frame>:name=1,other[2]=3" sets numeric globals (or elements of existing global arrays). */
    const char *set = getenv("UT_SET");
    if (set != NULL && g_runner != NULL && frame == atoi(set) && strchr(set, ':') != NULL) {
        VMContext *vm = g_runner->vmContext;
        for (const char *p = strchr(set, ':') + 1; *p != '\0';) {
            char name[64];
            size_t n = strcspn(p, "=[");
            if (n == 0 || n >= sizeof(name)) break;
            memcpy(name, p, n);
            name[n] = '\0';
            int index = p[n] == '[' ? atoi(p + n + 1) : -1;
            const char *eq = strchr(p, '=');
            if (eq == NULL) break;
            RValue value = RValue_makeReal((GMLReal) atof(eq + 1));
            int32_t id = VM_getOrAllocateVarID(vm, name);
            if (index < 0) {
                Instance_setSelfVar(vm->globalScopeInstance, id, value);
            } else {
                RValue array = Instance_getSelfVar(vm->globalScopeInstance, id);
                if (array.type == RVALUE_ARRAY) GMLArray_set(array.array, index, value);
                else logWarn("UT_SET: global.%s is not an array\n", name);
            }
            const char *comma = strchr(eq, ',');
            if (comma == NULL) break;
            p = comma + 1;
        }
    }
#endif
    if (script == NULL || g_runner == NULL) return;

    for (const char *p = script; *p != '\0';) {
        int at = atoi(p);
        const char *colon = strchr(p, ':');
        if (colon == NULL) break;
        int32_t key = 0;
        switch (colon[1]) {
            case 'U': key = VK_UP; break;
            case 'D': key = VK_DOWN; break;
            case 'L': key = VK_LEFT; break;
            case 'R': key = VK_RIGHT; break;
            case 'E': key = VK_ENTER; break;
            default:  key = colon[1]; break;
        }
        int hold = colon[2] == '*' ? atoi(colon + 3) : 2;
        if (frame == at) RunnerKeyboard_onKeyDown(g_runner->keyboard, key);
        if (frame == at + hold) RunnerKeyboard_onKeyUp(g_runner->keyboard, key);
        const char *comma = strchr(colon, ',');
        if (comma == NULL) break;
        p = comma + 1;
    }
}

/* Returns true when the app should quit; a Pocket core never does. */
bool platformHandleEvents(void) {
    utPerfPhase(UT_PHASE_STEP);
    of_input_poll();

    static bool chordUsed = false;
    static int32_t keyToRelease = 0;
    static uint64_t selectDownAt = 0;
    bool debugNow = g_debugMode;
    bool byHold = false;
    if (g_debugRequested) {
        debugNow = true;
        g_debugRequested = false;
    }
    if (of_btn_pressed(OF_BTN_SELECT)) {
        selectDownAt = nowNanos();
        chordUsed = false;
    }
    if (of_btn(OF_BTN_SELECT) && !chordUsed && selectDownAt != 0 && nowNanos() - selectDownAt >= UT_DEBUG_HOLD_NANOS) {
        debugNow = !g_debugMode;
        byHold = true;
        chordUsed = true; /* this hold is spent: its release does nothing more */
    }
    if (debugNow != g_debugMode) {
        g_debugMode = debugNow;
        utPerfHideOverlays();
        if (!debugNow) setScriptProfile(false);
        /* The frame times coming up are the sign that the hold took. */
        if (debugNow && byHold) utPerfToggle();
        showNotice(debugNow ? "Debug mode on" : "Debug mode off");
        logInfo("Debug mode %s\n", debugNow ? "on: Select times, R log; Select + Right/Left room, Start pause, A step, B unstick, X script times" : "off");
    }
    /* Frame stepping is the one hotkey the runner itself gates on this. */
    if (g_runner != NULL) g_runner->debugMode = g_debugMode;
    if (g_runner != NULL && keyToRelease != 0) {
        RunnerKeyboard_onKeyUp(g_runner->keyboard, keyToRelease);
        keyToRelease = 0;
    }
    bool chording = g_debugMode && of_btn(OF_BTN_SELECT);
    if (g_debugMode) {
        if (of_btn_released(OF_BTN_SELECT) && !chordUsed) utPerfToggle();
        if (of_btn_pressed(OF_BTN_R1)) utPerfToggleLog();
    }
    if (of_btn_pressed(OF_BTN_L1)) {
        g_smoothLowres = !g_smoothLowres;
        g_modeShownUntil = nowNanos() + UT_MODE_SHOWN_NANOS;
        logInfo("Video: 640x480 rooms drawn at %s\n", g_smoothLowres ? "320x240, smoothed (speed)" : "640x480 (accuracy)");
    }
    runInputScript();
    if (g_runner == NULL) return false;
    if (g_profileRequested) {
        g_profileRequested = false;
        setScriptProfile(true);
    }

    if (chording && of_btn_pressed(OF_BTN_X)) {
        /* The report goes to the log, so bring that up with it. */
        bool enable = !scriptProfileOn();
        setScriptProfile(enable);
        if (enable) utPerfShowLog();
        chordUsed = true;
        showNotice(enable ? "Script times on" : "Script times off");
        logInfo("Debug: script times %s\n", enable ? "on, every 2 s" : "off");
    }
    if (chording) {
        /* One hotkey per frame; it is released on the next. */
        for (size_t i = 0; i < UT_DEBUG_CHORD_COUNT && keyToRelease == 0; i++) {
            if (!of_btn_pressed(g_debugChords[i].button)) continue;
            RunnerKeyboard_onKeyDown(g_runner->keyboard, g_debugChords[i].key);
            keyToRelease = g_debugChords[i].key;
            chordUsed = true;
            if (g_debugChords[i].notice != NULL) showNotice(g_debugChords[i].notice);
            if (g_debugChords[i].key == 'O' && !g_runner->paused) showNotice("Pause first: Select + Start");
            logInfo("Debug: %s\n", g_debugChords[i].what);
        }
    }

    for (size_t i = 0; i < UT_KEYMAP_COUNT; i++) {
        if (!chording && of_btn_pressed(g_keymap[i].button))
            RunnerKeyboard_onKeyDown(g_runner->keyboard, g_keymap[i].key);
        if (of_btn_released(g_keymap[i].button))
            RunnerKeyboard_onKeyUp(g_runner->keyboard, g_keymap[i].key);
    }
    return false;
}

void platformSleepUntil(uint64_t time) {
    if (g_uncapped) return;
    uint64_t start = nowNanos();
    int64_t remaining = (int64_t) time - (int64_t) start;
    if (remaining > 2000000)
        usleep((useconds_t) ((remaining - 1000000) / 1000));
    while (nowNanos() < time) {
    }
    utPerfAddSleep(nowNanos() - start);
}
