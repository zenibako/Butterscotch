#include "ut_savestate.h"
#include "ut_snapshot_parts.h"
#include "ut_snapshot_envelope.h"
#include "ut_snapshot_bytes.h"
#include "runner_snapshot.h"
#include "of.h"
#include "stb_ds.h"
#include "log.h"
#include "gettime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if defined(UT_NATIVE_SAVESTATES) && !defined(OF_PC)
#include "of_savestate.h"
#include "of_timer.h"
#include "of_caps.h"
#include "of_perf.h"
#endif

static uint64_t g_gameId;
/* Native snapshot work can stall before a refusal can be presented. Publish
 * the current stage first; desktop captures retain their original output. */
static void captureProgress(const char *stage) {
#if defined(UT_NATIVE_SAVESTATES) && !defined(OF_PC)
    utPlatformSnapshotNotice(stage);
#else
    (void)stage;
#endif
}
#if defined(UT_NATIVE_SAVESTATES) && !defined(OF_PC)
static bool g_service;
/* UTS1 v2 stores its envelope checksum at byte 36. Logging it does not
 * replace validation; it identifies which captured blob a later wake used. */
static uint32_t stateEnvelopeId(const uint8_t *data, size_t size) {
    if (!data || size < 40) return 0;
    return (uint32_t)data[36] | (uint32_t)data[37] << 8 |
        (uint32_t)data[38] << 16 | (uint32_t)data[39] << 24;
}
static void stateServiceTrace(const char *stage) {
    const struct of_capabilities *caps = of_get_caps();
    logInfo("State service %s: enabled %u clock %u status %x size %u\n", stage,
        g_service ? 1u : 0u, caps ? caps->cpu_freq_hz : 0u,
        *(volatile const uint32_t *)(uintptr_t)0x4e000004u,
        *(volatile const uint32_t *)(uintptr_t)0x4e000008u);
}
/* The HW-mixer runtime otherwise leaves the machine timer off. Its file
 * watchdog needs the 1 kHz tick; the app callback itself has no work. */
static void fileWatchdogTick(void) {}
static void finishLoadedState(void) { of_savestate_finish(1); }
#endif

void utSavestateStartup(void) {
#if defined(UT_NATIVE_SAVESTATES) && !defined(OF_PC) && !defined(UT_GAME_DELTARUNE)
    /* Mark support before the slow asset load, so a wake request survives the
     * OS pickup deadline. It remains pending until the completed first frame. */
    g_service = of_savestate_enable(UT_SNAPSHOT_LIMIT) == 0;
    if (g_service) {
        volatile const uint32_t *timerControl = (volatile const uint32_t *)(uintptr_t)0x400000b8u;
        uint32_t before = *timerControl;
        of_timer_set_callback(fileWatchdogTick, 1000);
        logInfo("State file watchdog: control %x -> %x, 1 kHz\n", before, *timerControl);
        stateServiceTrace("startup");
    }
#endif
}

bool utSavestateDeferFileReads(void) {
#if defined(UT_NATIVE_SAVESTATES) && !defined(OF_PC)
    if (!g_service) return false;
    /* Pinned openfpgaOS 7561ace, SS_STATUS at 0x4e000004: requests in
     * bits 0/1, Pocket menu in bit 2, completion toggles in bits 8/9.
     * Unlike of_savestate_poll(), this read never changes CRAM ownership. */
    uint32_t status = *(volatile const uint32_t *)(uintptr_t)0x4e000004u;
    bool deferred = (status & 4u) || ((status ^ (status >> 8)) & 3u);
    static bool wasDeferred;
    if (deferred != wasDeferred) {
        logInfo("State music reads: %s (status %x)\n", deferred ? "deferred" : "resumed", status);
        wasDeferred = deferred;
    }
    return deferred;
#else
    return false;
#endif
}

void utSavestateInit(Runner *runner) {
    DataWin *dw = runner->dataWin;
    uint64_t h = BS_FNV64_OFFSET;
#define HASH_BYTES(p,n) do { const uint8_t *b_ = (const uint8_t *)(p); \
    for (size_t i_ = 0; i_ < (n); i_++) h = (h ^ b_[i_]) * BS_FNV64_PRIME; } while (0)
    HASH_BYTES(&dw->gen8.gameID, sizeof(dw->gen8.gameID));
    HASH_BYTES(&dw->gen8.timestamp, sizeof(dw->gen8.timestamp));
    HASH_BYTES(dw->gen8.licenseMD5, sizeof(dw->gen8.licenseMD5));
    for (uint32_t i = 0; i < dw->code.count; i++) {
        CodeEntry *c = &dw->code.entries[i];
        if (!c->present) continue;
        HASH_BYTES(c->name, strlen(c->name));
        HASH_BYTES(dw->bytecodeBuffer + c->bytecodeAbsoluteOffset - dw->bytecodeBufferBase, c->length);
    }
#ifdef UT_STATE_GAME_ID
    uint64_t packaged = UT_STATE_GAME_ID;
    HASH_BYTES(&packaged, sizeof(packaged));
#endif
#undef HASH_BYTES
    g_gameId = h;
}

static int32_t variableId(Runner *r, const char *name) {
    ptrdiff_t at = shgeti(r->vmContext->varNameMap, name);
    if (at >= 0) return r->vmContext->varNameMap[at].value;
    for (uint32_t i = 0; i < r->dataWin->vari.variableCount; i++)
        if (!strcmp(r->dataWin->vari.variables[i].name, name))
            return r->dataWin->vari.variables[i].varID;
    return -1;
}

static GMLReal number(Runner *r, Instance *instance, const char *name) {
    int32_t id = variableId(r, name);
    if (id < 0) return 0;
    RValue v = Instance_getSelfVar(instance, id);
    return v.type == RVALUE_REAL || v.type == RVALUE_INT32 || v.type == RVALUE_BOOL ? RValue_toReal(v) : 0;
}

const char *utSavestateReason(Runner *r) {
#if !defined(OF_PC) && !defined(UT_GAME_DELTARUNE)
#ifdef UT_NATIVE_SAVESTATES
    if (!g_service) return "Save-state service is unavailable";
#else
    return "This build has no save states";
#endif
#endif
#ifdef UT_GAME_DELTARUNE
    (void)r; return "Deltarune saves aren't ready";
#else
    if (!r || !r->currentRoom) return "The game is still loading";
    if (r->dataWin->gen8.wadVersion != 16 || strcmp(r->dataWin->gen8.name, "UNDERTALE"))
        return "Unsupported game version";
    bool overworld = false;
    for (ptrdiff_t i = 0; i < arrlen(r->instances); i++) {
        Instance *inst = r->instances[i];
        if (!inst->active || inst->destroyed || inst->objectIndex < 0 ||
            (uint32_t)inst->objectIndex >= r->dataWin->objt.count) continue;
        const char *name = r->dataWin->objt.objects[inst->objectIndex].name;
        if (!strcmp(name, "obj_overworldcontroller")) overworld = true;
        if (strstr(name, "battle") || strstr(name, "battler")) return "Can't save during battles";
        if (!strncmp(name, "obj_writer", 10) || strstr(name, "dialogue")) return "Can't save during dialogue";
        if (!strcmp(name, "obj_mainchara") && number(r, inst, "cutscene") != 0)
            return "Can't save during cutscenes";
    }
    if (!overworld) return "Save in the overworld or menu";
    /* The Undertale v1.08 overworld menu sets interact=5. Retain the writer,
     * battle and player-cutscene checks even for that menu value. */
    GMLReal interact = number(r, r->vmContext->globalScopeInstance, "interact");
    if (interact != 0 && interact != 5) return "Can't save during dialogue";
    return Runner_snapshotCheck(r);
#endif
}

bool utSavestateCapture(Runner *r, uint8_t **data, size_t *size, const char **reason) {
    *data = NULL; *size = 0;
    const char *why = utSavestateReason(r);
#ifdef OF_PC
    if (r && r->vmContext && r->vmContext->globalScopeInstance)
        logInfo("Save-state profile: interact %.0f\n", (double)number(r, r->vmContext->globalScopeInstance, "interact"));
#endif
    if (why) { *reason = why; return false; }
    uint8_t *parts[3] = {0}; size_t sizes[3] = {0};
    utAudioSnapshotLock(true);
    uint64_t started = nowNanos();
    captureProgress("Saving: game state");
    bool ok = Runner_snapshotCapture(r, g_gameId, &parts[0], &sizes[0], reason);
    logInfo("State game: %zu bytes, %llu ms, %s\n", sizes[0],
        (unsigned long long)((nowNanos() - started) / 1000000), ok ? "ok" : "failed");
    if (ok) {
        captureProgress("Saving: audio");
        ok = utAudioSnapshot(r->audioSystem, &parts[1], &sizes[1], reason);
    }
    if (ok) {
        captureProgress("Saving: progress files");
        ok = utSaveFsSnapshot(r->fileSystem, &parts[2], &sizes[2], reason);
    }
    if (ok) {
        captureProgress("Saving: packing state");
        uint32_t values[3]; uint8_t platform[12];
        utPlatformSnapshotGet(values);
        for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 4; j++)
            platform[i * 4 + j] = (uint8_t)(values[i] >> (j * 8));
        UtSnapshotSection sections[4];
        for (unsigned i = 0; i < 3; i++) { sections[i].data = parts[i]; sections[i].size = sizes[i]; }
        sections[3].data = platform; sections[3].size = sizeof(platform);
        ok = utSnapshotPack(g_gameId, sections, data, size);
        if (!ok) *reason = "State is too large to save";
    }
    for (unsigned i = 0; i < 3; i++) free(parts[i]);
    logInfo("State capture: %zu bytes, %llu ms, audio %zu, files %zu, %s%s%s\n",
        *size, (unsigned long long)((nowNanos() - started) / 1000000), sizes[1], sizes[2],
        ok ? "ok" : "failed", !ok && *reason ? ": " : "", !ok && *reason ? *reason : "");
    utAudioSnapshotLock(false); return ok;
}

static bool restoreState(Runner *r, const uint8_t *data, size_t size,
                         const char **reason, void (*committed)(void)) {
    uint64_t phases[6] = {nowNanos(), 0, 0, 0, 0, 0};
    UtSnapshotSection parts[4];
    if (!utSnapshotUnpack(g_gameId, data, size, parts)) {
        *reason = data && size >= 4 && memcmp(data, "UTS1", 4) != 0
            ? "Not an Undertale save state" : "State is damaged or incompatible";
        return false;
    }
    UtSnapshotReader platform = {parts[3].data, parts[3].size, 0, true};
    uint32_t values[3];
    for (unsigned i = 0; i < 3; i++) values[i] = utSnapGetU32(&platform);
    if (!platform.ok || platform.offset != platform.size || values[0] > INT_MAX ||
        values[1] > INT_MAX || values[2] > 1) {
        *reason = "Invalid platform state"; return false;
    }
    utAudioSnapshotLock(true);
    uint64_t noticeStarted = nowNanos();
    captureProgress("Loading: game state");
    logInfo("State prepare notice: %llu ms\n",
        (unsigned long long)((nowNanos() - noticeStarted) / 1000000));
    RunnerSnapshot *core = Runner_snapshotPrepare(r, g_gameId, parts[0].data, parts[0].size, reason);
    phases[1] = nowNanos();
    void *audio = NULL, *files = NULL;
    if (core) {
        captureProgress("Loading: audio");
        audio = utAudioPrepareSnapshot(r->audioSystem, parts[1].data, parts[1].size, reason);
    }
    phases[2] = nowNanos();
    if (audio) {
        captureProgress("Loading: progress files");
        files = utSaveFsPrepareSnapshot(r->fileSystem, parts[2].data, parts[2].size, reason);
    }
    phases[3] = nowNanos();
    uint64_t applyPhases[5] = {phases[3], 0, 0, 0, 0};
    bool ok = core && audio && files;
    if (ok && !utSaveFsApplySnapshot(r->fileSystem, files)) {
        *reason = "Could not restore progress files"; ok = false;
    }
    applyPhases[1] = nowNanos();
    if (ok) Runner_snapshotApply(r, core);
    applyPhases[2] = nowNanos();
    if (ok) utAudioApplySnapshot(r->audioSystem, audio);
    applyPhases[3] = nowNanos();
    if (ok) utPlatformSnapshotResumed(r, values);
    applyPhases[4] = nowNanos();
    phases[4] = applyPhases[4];
    /* Validation and all four sections have committed. Notify the host before
     * freeing the displaced graph. No operation after this point can refuse
     * the load; a new request during cleanup must retain its own completion. */
    if (ok && committed) committed();
    Runner_snapshotFree(core); utAudioFreeSnapshot(audio); utSaveFsFreeSnapshot(files);
    utAudioSnapshotLock(false);
    phases[5] = nowNanos();
    logInfo("State apply phases: archive %llu runner %llu audio %llu input/clock %llu ms\n",
        (unsigned long long)((applyPhases[1] - applyPhases[0]) / 1000000),
        (unsigned long long)((applyPhases[2] - applyPhases[1]) / 1000000),
        (unsigned long long)((applyPhases[3] - applyPhases[2]) / 1000000),
        (unsigned long long)((applyPhases[4] - applyPhases[3]) / 1000000));
    logInfo("State restore phases: game %llu audio %llu files %llu apply %llu cleanup %llu ms; %s%s%s\n",
        (unsigned long long)((phases[1] - phases[0]) / 1000000),
        (unsigned long long)((phases[2] - phases[1]) / 1000000),
        (unsigned long long)((phases[3] - phases[2]) / 1000000),
        (unsigned long long)((phases[4] - phases[3]) / 1000000),
        (unsigned long long)((phases[5] - phases[4]) / 1000000),
        ok ? "ok" : "refused", !ok && *reason ? ": " : "", !ok && *reason ? *reason : "");
    return ok;
}

bool utSavestateRestore(Runner *r, const uint8_t *data, size_t size, const char **reason) {
    return restoreState(r, data, size, reason, NULL);
}

#ifdef OF_PC
static bool hostRequest(Runner *r, bool load, const char *path) {
    const char *reason = NULL; uint8_t *data = NULL; size_t size = 0;
    bool ok = false;
    if (load) {
        FILE *f = fopen(path, "rb");
        if (f && fseek(f, 0, SEEK_END) == 0) {
            long len = ftell(f);
            if (len > 0 && len <= (long)UT_SNAPSHOT_LIMIT && fseek(f, 0, SEEK_SET) == 0) {
                size = (size_t)len; data = malloc(size);
                if (data && fread(data, 1, size, f) == size) ok = utSavestateRestore(r, data, size, &reason);
            }
        }
        if (f) fclose(f);
    } else if (utSavestateCapture(r, &data, &size, &reason)) {
        FILE *f = fopen(path, "wb");
        ok = f && fwrite(data, 1, size, f) == size;
        if (f && fclose(f) != 0) ok = false;
    }
    logInfo("Save state %s: %s (%zu bytes)%s%s\n", load ? "load" : "save", ok ? "ok" : "refused",
        size, reason ? ": " : "", reason ? reason : "");
    if (!ok) utPlatformSnapshotNotice(reason ? reason : "State request failed");
    free(data); return ok;
}
#endif

bool platformSnapshotBoundary(Runner *r) {
#if defined(UT_NATIVE_SAVESTATES) && !defined(OF_PC)
    if (!g_service) return false;
    int request = of_savestate_poll();
    if (request == OF_SAVESTATE_NONE) return false;
    uint64_t requestStarted = nowNanos();
    logInfo("State request: %s, room %d, status %x\n",
        request == OF_SAVESTATE_SAVE ? "save" : "load", r->currentRoomIndex,
        *(volatile const uint32_t *)(uintptr_t)0x4e000004u);
    const char *reason = NULL; uint8_t *data = NULL; size_t size = 0; bool ok = false;
    bool completionOwned = true;
    if (request == OF_SAVESTATE_SAVE) {
        stateServiceTrace("before capture");
        bool captured = utSavestateCapture(r, &data, &size, &reason);
        logInfo("State save prepared: %s, %zu bytes, id %08x%s%s\n", captured ? "ok" : "refused",
            size, stateEnvelopeId(data, size), !captured && reason ? ": " : "",
            !captured && reason ? reason : "");
        /* Sleep can power down as soon as the host has pulled the blob.
         * Record capture/refusal before signalling readiness, while this
         * app still owns the request. A diagnostic write failure must not
         * change request completion or prevent an otherwise valid save. */
        uint64_t auditStarted = nowNanos();
        bool audited = utLogDump();
        logInfo("State save audit: %s, %llu ms\n", audited ? "written" : "failed",
            (unsigned long long)((nowNanos() - auditStarted) / 1000000));
        if (captured) {
            captureProgress("Saving: Pocket transfer");
            int declared = of_savestate_enable((uint32_t)size);
            logInfo("State declare: result %d, %zu bytes\n", declared, size);
            uint64_t started = nowNanos();
            int result = of_savestate_save(data, (uint32_t)size);
            completionOwned = false; /* The service completed the save. */
            logInfo("State transfer: result %d, %llu ms\n", result,
                (unsigned long long)((nowNanos() - started) / 1000000));
            logInfo("State host read: %u words, expected %u; status %x\n",
                *(volatile const uint32_t *)(uintptr_t)0x4e00000cu,
                ((uint32_t)size + 32u + 3u) / 4u,
                *(volatile const uint32_t *)(uintptr_t)0x4e000004u);
            ok = result == 0;
            int rearmed = of_savestate_enable(UT_SNAPSHOT_LIMIT);
            logInfo("State rearm: result %d\n", rearmed);
            if (!ok) reason = "The Pocket could not save";
            else captureProgress("Save transfer returned");
        }
    } else if (request == OF_SAVESTATE_LOAD) {
        data = malloc(UT_SNAPSHOT_LIMIT);
        int n = data ? of_savestate_load(data, UT_SNAPSHOT_LIMIT) : -1;
        /* Receive errors are completed by the service. Allocation failure
         * never called it, so that refusal still belongs to this handler. */
        completionOwned = !data || n > 0;
        logInfo("State load receive: result %d, %llu ms, id %08x\n", n,
            (unsigned long long)((nowNanos() - requestStarted) / 1000000),
            stateEnvelopeId(data, n > 0 ? (size_t)n : 0));
        if (n > 0) {
            size = (size_t)n;
            ok = restoreState(r, data, size, &reason, finishLoadedState);
            if (ok) completionOwned = false;
        }
        else reason = "The Pocket's state was rejected";
    }
    if (!ok) utPlatformSnapshotNotice(reason ? reason : "State could not be restored");
    if (!ok && completionOwned) of_savestate_finish(0);
    logInfo("State result: %s %s, %zu bytes, %llu ms%s%s\n",
        request == OF_SAVESTATE_SAVE ? "save" : "load", ok ? "ok" : "refused", size,
        (unsigned long long)((nowNanos() - requestStarted) / 1000000),
        !ok && reason ? ": " : "", !ok && reason ? reason : "");
    if (request == OF_SAVESTATE_LOAD && ok) {
        stateServiceTrace("after load");
    }
    free(data); return true;
#elif defined(OF_PC)
    static bool saved = false, loaded = false;
    bool worked = false;
    const char *at = getenv("UT_STATE_SAVE_FRAME");
    if (!saved && at && r->frameCount == atoi(at)) {
        saved = true; const char *path = getenv("UT_STATE_SAVE_PATH");
        hostRequest(r, false, path ? path : "native-state.bin"); worked = true;
    }
    at = getenv("UT_STATE_LOAD_FRAME");
    if (!loaded && at && r->frameCount == atoi(at)) {
        loaded = true; const char *path = getenv("UT_STATE_LOAD_PATH");
        hostRequest(r, true, path ? path : "native-state.bin"); worked = true;
    }
    return worked;
#else
    (void)r; return false;
#endif
}

void utSavestateMenuPoll(void) {
#if defined(UT_NATIVE_SAVESTATES) && !defined(OF_PC)
    if (g_service && of_savestate_poll() != OF_SAVESTATE_NONE) {
        logInfo("State request refused: Butterscotch Select menu is open\n");
        utPlatformSnapshotNotice("Close this menu to save or load");
        of_savestate_finish(0);
    }
#endif
}
