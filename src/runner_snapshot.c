#include "runner_snapshot.h"
#include "gettime.h"
#include "ini.h"
#include "spatial_grid.h"
#include "stb_ds.h"
#include "miniz.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#ifdef PLATFORM_BUSY_TICK
#include "log.h"
#endif
#ifdef ENABLE_SW_RENDERER
#include "sw/sw_renderer.h"
#endif
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* This is a same-build, WAD16-only wire format. Never write live pointers. */
#define SNAP_MAGIC 0x31504e53u /* SNP1 */
#define SNAP_VERSION 5u
#define SNAP_ZSTD_LEGACY_VERSION 4u
#define SNAP_LEGACY_VERSION 3u
#define SNAP_OUTPUT_LIMIT (1024u * 1024u)
#define SNAP_LIMIT (4u * 1024u * 1024u)
#define SNAP_MAX_ITEMS 65536u
#define SNAP_OWNER_NONE UINT32_MAX
#define SNAP_OWNER_ORPHAN (UINT32_MAX - 1u)
#define SNAP_DORMANT_POOL_COUNT 11u
#define SNAP_DORMANT_SLOT_LIMIT 4096u
#define SNAP_INI_MAGIC 0x31494e49u /* INI1 */
#define SNAP_STATIC_STRING_LIMIT 131072u
#define SNAP_STRING_REF 0x80000000u

typedef struct {
    uint8_t *p; size_t n, cap; bool ok; const char *failure;
    const Strg *strg; uint32_t *stringIndex; uint32_t stringMask;
    size_t stringBytes;
} Writer;
typedef struct {
    const uint8_t *p; size_t n, pos; bool ok;
    const Strg *strg; bool stringRefs;
    size_t stringBytes;
} Reader;
static void put(Writer *w, const void *p, size_t n) {
    if (!w->ok) return;
    if (n > SNAP_LIMIT - w->n) {
        w->ok = false; w->failure = "Runner snapshot exceeds the size limit"; return;
    }
    if (n == 0) return;
    if (w->n + n > w->cap) {
        size_t cap = w->cap ? w->cap : 1024;
        while (cap < w->n + n) cap *= 2;
        if (cap > SNAP_LIMIT) cap = SNAP_LIMIT;
        uint8_t *next = (uint8_t *)realloc(w->p, cap);
        if (!next) { w->ok = false; w->failure = "Out of memory"; return; }
        w->p = next; w->cap = cap;
    }
    memcpy(w->p + w->n, p, n); w->n += n;
}
static void get(Reader *r, void *p, size_t n) {
    if (!r->ok || n > r->n - r->pos) { r->ok = false; return; }
    if (n == 0) return;
    memcpy(p, r->p + r->pos, n); r->pos += n;
}
static void put32(Writer *w, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(w, b, 4);
}
static uint32_t get32(Reader *r) {
    uint8_t b[4] = {0}; get(r, b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static void put64(Writer *w, uint64_t v) { put32(w, (uint32_t)v); put32(w, (uint32_t)(v >> 32)); }
static uint64_t get64(Reader *r) { uint64_t lo = get32(r), hi = get32(r); return lo | hi << 32; }
static uint32_t checksum(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static bool countOK(uint32_t n) { return n <= SNAP_MAX_ITEMS; }

/* Pointer-free scalar portions of Instance. The omitted gaps contain the
 * static-parent pointer, collision-cell cache, self-vars and physics body. */
#define I_A offsetof(Instance, x)
#define I_B offsetof(Instance, collisionCells)
#define I_C offsetof(Instance, spriteIndex)
#define I_D offsetof(Instance, physicsBody)
#define I_E offsetof(Instance, physicsContact)
typedef struct {
    uint32_t id;
    int32_t object, room;
    uint8_t a[I_B - I_A], b[I_D - I_C], c[sizeof(Instance) - I_E];
} InstancePlain;
static InstancePlain instancePlain(const Instance *inst) {
    InstancePlain p = {0};
    p.id = inst->instanceId; p.object = inst->objectIndex; p.room = inst->roomIndex;
    memcpy(p.a, (const uint8_t *)inst + I_A, sizeof(p.a));
    memcpy(p.b, (const uint8_t *)inst + I_C, sizeof(p.b));
    memcpy(p.c, (const uint8_t *)inst + I_E, sizeof(p.c));
    return p;
}
static void setInstancePlain(Instance *inst, const InstancePlain *p) {
    inst->instanceId = p->id; inst->objectIndex = p->object; inst->roomIndex = p->room;
    memcpy((uint8_t *)inst + I_A, p->a, sizeof(p->a));
    memcpy((uint8_t *)inst + I_C, p->b, sizeof(p->b));
    memcpy((uint8_t *)inst + I_E, p->c, sizeof(p->c));
    inst->spatialGridDirty = false;
    inst->lastCollisionQueryId = 0;
}

typedef struct {
    int32_t roomIndex, roomOrder, frame, pendingRoom;
    uint32_t nextInstanceId, collisionQueryCounter;
    bool gameStartFired, viewsEnabled, drawBackgroundColor, appSurfaceEnabled;
    bool appSurfaceAutoDraw, usingAppSurface, drawEnabled, dateTimeLocal;
    bool collisionCompatibilityMode, applyOffsetForPrimitives;
    int32_t applicationWidth, applicationHeight, oldApplicationWidth, oldApplicationHeight;
    int32_t guiWidth, guiHeight, cursorSprite, cursorSpriteSubimage, currentCursor;
    int32_t nextLayerId, nextLayerDrawOrder, nextCallLaterId;
    int32_t asyncBufferNextRequestId, xboxAsyncIdCounter;
    int32_t lastMusicInstance, viewCurrent;
    int32_t nextDynamicVarID, currentVertexFormatId, vertexBufferCount;
    uint32_t vertexFormatBit;
    int32_t surfaceStack[MAX_SURFACES];
    uint32_t backgroundColor;
    GMLReal score, lives, health, deltaTime, gameSpeedOverride;
    GMLReal mpPotMaxrot, mpPotStep, mpPotAhead;
    bool mpPotOnSpot;
    Random random;
    uint64_t elapsedGameNanos;
    float gms2FPS;
    RuntimeView views[MAX_VIEWS];
    GMLCamera defaultCameras[MAX_DEFAULT_ROOM_CAMERAS], userCameras[MAX_USER_CAMERAS];
    GMLCamera surfaceCamera, guiCamera;
    RuntimeBackground backgrounds[8];
    RunnerKeyboardState keyboard;
    RunnerGamepadState gamepads;
    RunnerMouseState mouse;
    uint8_t rendererBefore[offsetof(Renderer, runner) - offsetof(Renderer, drawColor)];
    uint8_t rendererAfter[sizeof(Renderer) - offsetof(Renderer, gmlMatrices)];
#ifdef ENABLE_SW_RENDERER
    SWRendererSnapshotState swState;
#endif
} RunnerPlain;

static RunnerPlain makePlain(const Runner *r) {
    RunnerPlain p = {0};
    p.roomIndex = r->currentRoomIndex; p.roomOrder = r->currentRoomOrderPosition;
    p.frame = r->frameCount; p.pendingRoom = r->pendingRoom;
    p.nextInstanceId = r->nextInstanceId; p.collisionQueryCounter = r->collisionQueryCounter;
    p.gameStartFired = r->gameStartFired; p.viewsEnabled = r->viewsEnabled;
    p.drawBackgroundColor = r->drawBackgroundColor; p.backgroundColor = r->backgroundColor;
    p.appSurfaceEnabled = r->appSurfaceEnabled; p.appSurfaceAutoDraw = r->appSurfaceAutoDraw;
    p.usingAppSurface = r->usingAppSurface; p.drawEnabled = r->drawEnabled;
    p.dateTimeLocal = r->dateTimeLocal; p.collisionCompatibilityMode = r->collisionCompatibilityMode;
    p.applyOffsetForPrimitives = r->applyOffsetForPrimitives;
    p.applicationWidth = r->applicationWidth; p.applicationHeight = r->applicationHeight;
    p.oldApplicationWidth = r->oldApplicationWidth; p.oldApplicationHeight = r->oldApplicationHeight;
    p.guiWidth = r->guiWidth; p.guiHeight = r->guiHeight;
    p.cursorSprite = r->cursorSprite; p.cursorSpriteSubimage = r->cursorSpriteSubimage;
    p.currentCursor = r->currentCursor; p.nextLayerId = r->nextLayerId;
    p.nextLayerDrawOrder = r->nextLayerDrawOrder; p.nextCallLaterId = r->nextCallLaterId;
    p.asyncBufferNextRequestId = r->asyncBufferNextRequestId;
    p.xboxAsyncIdCounter = r->xboxAsyncIdCounter;
    p.lastMusicInstance = r->lastMusicInstance; p.viewCurrent = r->viewCurrent;
    p.nextDynamicVarID = r->vmContext->nextDynamicVarID;
    p.currentVertexFormatId = r->currentVertexFormatId;
    p.vertexBufferCount = r->vertexBufferCount;
    p.vertexFormatBit = r->vertexFormatBit;
    memcpy(p.surfaceStack, r->surfaceStack, sizeof(p.surfaceStack));
    p.score = r->score; p.lives = r->lives; p.health = r->health;
    p.deltaTime = r->deltaTime; p.gameSpeedOverride = r->gameSpeedOverride;
    p.mpPotMaxrot = r->mpPotMaxrot; p.mpPotStep = r->mpPotStep;
    p.mpPotAhead = r->mpPotAhead; p.mpPotOnSpot = r->mpPotOnSpot;
    p.random = r->random;
    uint64_t now = nowNanos();
    p.elapsedGameNanos = now >= r->gameStartTime ? now - r->gameStartTime : 0;
    p.gms2FPS = r->dataWin->gen8.gms2FPS;
    memcpy(p.views, r->views, sizeof(p.views));
    memcpy(p.defaultCameras, r->defaultCameras, sizeof(p.defaultCameras));
    memcpy(p.userCameras, r->userCameras, sizeof(p.userCameras));
    p.surfaceCamera = r->surfaceCamera; p.guiCamera = r->guiCamera;
    memcpy(p.backgrounds, r->backgrounds, sizeof(p.backgrounds));
    if (r->keyboard) p.keyboard = *r->keyboard;
    if (r->gamepads) p.gamepads = *r->gamepads;
    if (r->mouse) p.mouse = *r->mouse;
    memcpy(p.rendererBefore, (const uint8_t *)r->renderer + offsetof(Renderer, drawColor), sizeof(p.rendererBefore));
    memcpy(p.rendererAfter, (const uint8_t *)r->renderer + offsetof(Renderer, gmlMatrices), sizeof(p.rendererAfter));
#ifdef ENABLE_SW_RENDERER
    SWRenderer_snapshotGetState(r->renderer, &p.swState);
#endif
    return p;
}

typedef enum { SLOT_GLOBAL, SLOT_INSTANCE, SLOT_MAP, SLOT_LIST, SLOT_ARRAY } SlotKind;
typedef struct { SlotKind kind; uint32_t container, index; char *key; } SlotRef;
typedef struct { GMLArray *array; uint32_t ownerSlot; } ArrayRef;
typedef struct { char *key; int32_t value; } SnapshotVarName;
typedef struct {
    uint32_t width, height, speed, tileCount;
    bool persistent, payloadLoaded;
    RoomTile *tiles, *displacedTiles;
    Room stagedPayload;
    bool stagedLoaded;
} SnapshotRoom;

struct RunnerSnapshot {
    bool applied;
    RunnerPlain plain;
    Room *room;
    Instance *global;
    Instance **instances;
    DsMapEntry **maps;
    DsList *lists;
    IniFile *currentIni, *cachedIni;
    char *currentIniPath, *cachedIniPath;
    bool currentIniDirty;
    DsQueue *queues;
    DsStack *stacks;
    DsPriority *priorities;
    DsGrid *grids;
    GmlBuffer *buffers;
    MpGrid *mpGrids;
    ParticleSystem *particleSystems;
    ParticleType *particleTypes;
    AudioEmitter *emitters;
    VmVertexFormat **vertexFormats;
    Buffer_Vertex **vertexBuffers;
    SnapshotVarName *varNames;
    GMLArray **arrays; /* one temporary strong ref per array */
    uint32_t *owners;
    SlotRef *slots;
    SnapshotRoom *roomStates;
    uint32_t roomCount;
    TileLayerMapEntry *tileLayers;
    bool *roomInitialized;
    InstanceIdEntry *byId;
    Instance ***byObject, ***byExact;
    SpatialGrid *grid;
    /* After apply these are the displaced live allocations, freed by Free. */
    Instance *oldGlobal;
    Instance **oldInstances;
    DsMapEntry **oldMaps;
    DsList *oldLists;
    IniFile *oldCurrentIni;
    char *oldCurrentIniPath;
    DsQueue *oldQueues;
    DsStack *oldStacks;
    DsPriority *oldPriorities;
    DsGrid *oldGrids;
    GmlBuffer *oldBuffers;
    MpGrid *oldMpGrids;
    ParticleSystem *oldParticleSystems;
    ParticleType *oldParticleTypes;
    AudioEmitter *oldEmitters;
    VmVertexFormat **oldVertexFormats;
    Buffer_Vertex **oldVertexBuffers;
    SnapshotVarName *oldVarNames;
    TileLayerMapEntry *oldTileLayers;
    InstanceIdEntry *oldById;
    Instance ***oldByObject, ***oldByExact;
    SpatialGrid *oldGrid;
    IniFile *oldCachedIni;
    char *oldCachedIniPath;
    uint32_t objectCount;
};

static void freeValues(Instance *global, Instance **instances, DsMapEntry **maps, DsList *lists) {
    /* Arrays can refer across containers, so release every owner before the
     * registry's final array refs are dropped. */
    if (global) Instance_free(global);
    for (ptrdiff_t i = 0; i < arrlen(instances); i++) Instance_free(instances[i]);
    arrfree(instances);
    for (ptrdiff_t i = 0; i < arrlen(maps); i++) {
        DsMapEntry *map = maps[i];
        for (ptrdiff_t j = 0; j < shlen(map); j++) {
            free(map[j].key); RValue_free(&map[j].value);
        }
        shfree(map);
    }
    arrfree(maps);
    for (ptrdiff_t i = 0; i < arrlen(lists); i++) {
        for (ptrdiff_t j = 0; j < arrlen(lists[i].items); j++) RValue_free(&lists[i].items[j]);
        arrfree(lists[i].items);
    }
    arrfree(lists);
}
static void freeDormantPools(DsQueue *queues, DsStack *stacks,
                             DsPriority *priorities, DsGrid *grids,
                             GmlBuffer *buffers, MpGrid *mpGrids,
                             ParticleSystem *systems, ParticleType *types,
                             AudioEmitter *emitters, VmVertexFormat **formats,
                             Buffer_Vertex **vertexBuffers) {
    for (ptrdiff_t i = 0; i < arrlen(queues); i++) arrfree(queues[i].items);
    for (ptrdiff_t i = 0; i < arrlen(stacks); i++) arrfree(stacks[i].items);
    for (ptrdiff_t i = 0; i < arrlen(priorities); i++) arrfree(priorities[i].items);
    for (ptrdiff_t i = 0; i < arrlen(grids); i++) free(grids[i].items);
    for (ptrdiff_t i = 0; i < arrlen(buffers); i++) free(buffers[i].data);
    for (ptrdiff_t i = 0; i < arrlen(mpGrids); i++) free(mpGrids[i].cells);
    for (ptrdiff_t i = 0; i < arrlen(systems); i++) {
        arrfree(systems[i].particles); arrfree(systems[i].emitters);
    }
    for (ptrdiff_t i = 0; i < arrlen(emitters); i++) arrfree(emitters[i].voices);
    arrfree(queues); arrfree(stacks); arrfree(priorities); arrfree(grids);
    arrfree(buffers); arrfree(mpGrids); arrfree(systems); arrfree(types);
    arrfree(emitters); arrfree(formats); arrfree(vertexBuffers);
}
static void freeBuckets(Instance ***buckets, uint32_t count) {
    if (!buckets) return;
    for (uint32_t i = 0; i < count; i++) arrfree(buckets[i]);
    free(buckets);
}
static void freeVarNames(SnapshotVarName *names) {
    for (ptrdiff_t i = 0; i < shlen(names); i++) free(names[i].key);
    shfree(names);
}
void Runner_snapshotFree(RunnerSnapshot *s) {
    if (!s) return;
    if (s->applied) {
        freeValues(s->oldGlobal, s->oldInstances, s->oldMaps, s->oldLists);
        freeDormantPools(s->oldQueues, s->oldStacks, s->oldPriorities,
                         s->oldGrids, s->oldBuffers, s->oldMpGrids,
                         s->oldParticleSystems, s->oldParticleTypes,
                         s->oldEmitters, s->oldVertexFormats, s->oldVertexBuffers);
        freeVarNames(s->oldVarNames);
        hmfree(s->oldTileLayers); hmfree(s->oldById);
        freeBuckets(s->oldByObject, s->objectCount);
        freeBuckets(s->oldByExact, s->objectCount);
        if (s->oldGrid) SpatialGrid_free(s->oldGrid);
        if (s->oldCurrentIni) Ini_free(s->oldCurrentIni);
        free(s->oldCurrentIniPath);
        if (s->oldCachedIni) Ini_free(s->oldCachedIni);
        free(s->oldCachedIniPath);
    } else {
        freeValues(s->global, s->instances, s->maps, s->lists);
        freeDormantPools(s->queues, s->stacks, s->priorities, s->grids,
                         s->buffers, s->mpGrids, s->particleSystems,
                         s->particleTypes, s->emitters, s->vertexFormats,
                         s->vertexBuffers);
        freeVarNames(s->varNames);
        hmfree(s->tileLayers); hmfree(s->byId);
        freeBuckets(s->byObject, s->objectCount);
        freeBuckets(s->byExact, s->objectCount);
        if (s->grid) SpatialGrid_free(s->grid);
        if (s->currentIni) Ini_free(s->currentIni);
        free(s->currentIniPath);
        if (s->cachedIni) Ini_free(s->cachedIni);
        free(s->cachedIniPath);
    }
    for (ptrdiff_t i = 0; i < arrlen(s->arrays); i++) GMLArray_decRef(s->arrays[i]);
    arrfree(s->arrays); arrfree(s->owners); arrfree(s->slots);
    if (s->roomStates) for (uint32_t i = 0; i < s->roomCount; i++) {
        SnapshotRoom *state = &s->roomStates[i];
        if (state->stagedLoaded) DataWin_freeRoomPayload(&state->stagedPayload);
        free(s->applied ? state->displacedTiles : state->tiles);
    }
    free(s->roomStates);
    free(s->roomInitialized); free(s);
}

static bool eligible(Runner *r, const char **reason, bool forCapture) {
#define REFUSE(msg) do { if (reason) *reason = msg; return false; } while (0)
    if (!r || !r->dataWin || !r->vmContext || !r->renderer || !r->currentRoom)
        REFUSE("Runner is not ready");
    if (r->dataWin->gen8.wadVersion != 16) REFUSE("Only WAD16 Undertale state is supported");
    if (r->currentRoomIndex < 0 || (uint32_t)r->currentRoomIndex >= r->dataWin->room.count)
        REFUSE("Invalid room");
    if (forCapture && r->currentRoom->persistent) REFUSE("Persistent room is unsupported");
    if (forCapture && !r->currentRoom->payloadLoaded) REFUSE("Current room payload is unavailable");
    if (forCapture && !SpatialGrid_dimensionsValid(r->currentRoom->width,
                                                    r->currentRoom->height))
        REFUSE("Room dimensions exceed the collision grid limit");
    if (r->pendingRoom != -1) REFUSE("Room transition is pending");
    /* These visual-only offsets live outside RunnerPlain. Ordinary gameplay
     * uses the defaults; refuse a debug camera rather than restore it wrong. */
    if (r->freeCamPanX != 0.0f || r->freeCamPanY != 0.0f ||
        (r->freeCamZoom != 0.0f && r->freeCamZoom != 1.0f))
        REFUSE("Free camera offset is unsupported");
    if (r->vmContext->callStack || r->vmContext->callDepth || r->vmContext->envStack ||
        r->vmContext->stack.top || r->vmContext->localVars || r->vmContext->scriptArgs)
        REFUSE("Game script is still executing");
    if (!r->vmContext->globalScopeInstance) REFUSE("Global scope is missing");
    if (arrlen(r->instanceSnapshots))
        REFUSE("Instance dispatch is still active");
    if (r->physics) REFUSE("Active physics is unsupported");
    if (r->physicsRooms) {
        for (uint32_t i = 0; i < r->dataWin->room.count; i++)
            if (r->physicsRooms[i]) REFUSE("Physics room is unsupported");
    }
    if (arrlen(r->structInstances)) REFUSE("Live GML structs are unsupported");
    if (r->vmContext->staticStructs) {
        for (uint32_t i = 0; i < r->dataWin->code.count; i++)
            if (r->vmContext->staticStructs[i]) REFUSE("Static structs are unsupported");
    }
    for (uint32_t i = 0; i < r->dataWin->room.count; i++) {
        SavedRoomState *st = r->savedRoomStates ? &r->savedRoomStates[i] : NULL;
        Room *room = &r->dataWin->room.rooms[i];
        if (forCapture && st && (arrlen(st->instances) || arrlen(st->runtimeLayers) ||
                                 hmlen(st->tileLayerMap)))
            REFUSE("Saved room resources are unsupported");
        if (forCapture && st && st->initialized && room->persistent)
            REFUSE("Persistent saved room state is unsupported");
        if (forCapture && !SpatialGrid_dimensionsValid(room->width, room->height))
            REFUSE("Room dimensions exceed the collision grid limit");
        if (forCapture && room->payloadLoaded && room->tileCount && !room->tiles)
            REFUSE("Loaded room tiles are unavailable");
    }
    if (arrlen(r->runtimeLayers)) REFUSE("Runtime layers are unsupported");
    if (r->currentIni && r->currentIni == r->cachedIni)
        REFUSE("Aliased INI cache is unsupported");
    for (int i = 0; i < MAX_OPEN_TEXT_FILES; i++) if (r->openTextFiles[i].isOpen) REFUSE("Open text file");
    for (int i = 0; i < MAX_OPEN_BINARY_FILES; i++) if (r->openBinaryFiles[i].isOpen) REFUSE("Open binary file");
    if (arrlen(r->fileFindResults)) REFUSE("File enumeration is active");
    if (arrlen(r->asyncBufferGroupOps) || arrlen(r->asyncSaveLoadQueue) ||
        r->asyncBufferGroupActive || r->asyncBufferGroupName)
        REFUSE("Async file operation is pending");
    if (r->xboxAccountPickerPendingId >= 0 && r->xboxAsyncIdCounter != 0)
        REFUSE("Async dialog is pending");
    if (arrlen(r->callLaterEntries)) REFUSE("Pending callbacks are unsupported");
    if (shlen(r->disabledObjects)) REFUSE("Object filtering is unsupported");
    size_t dormantSlots = (size_t)arrlen(r->dsQueuePool) + arrlen(r->dsStackPool) +
        arrlen(r->dsPriorityPool) + arrlen(r->dsGridPool) + arrlen(r->gmlBufferPool) +
        arrlen(r->mpGridPool) + arrlen(r->particleSystemPool) +
        arrlen(r->particleTypePool) + arrlen(r->audioEmitters) +
        arrlen(r->vertexFormats) + arrlen(r->vertexBuffers);
    if (dormantSlots > SNAP_DORMANT_SLOT_LIMIT) REFUSE("Dormant pool history is too large");
    for (ptrdiff_t i = 0; i < arrlen(r->dsQueuePool); i++)
        if (!r->dsQueuePool[i].freed || arrlen(r->dsQueuePool[i].items)) REFUSE("Live DS queue is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->dsStackPool); i++)
        if (!r->dsStackPool[i].freed || arrlen(r->dsStackPool[i].items)) REFUSE("Live DS stack is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->dsPriorityPool); i++)
        if (!r->dsPriorityPool[i].freed || arrlen(r->dsPriorityPool[i].items)) REFUSE("Live DS priority is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->dsGridPool); i++)
        if (!r->dsGridPool[i].freed || r->dsGridPool[i].items) REFUSE("Live DS grid is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->gmlBufferPool); i++)
        if (r->gmlBufferPool[i].isValid || r->gmlBufferPool[i].data) REFUSE("Live GML buffer is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->mpGridPool); i++)
        if (r->mpGridPool[i].inUse || r->mpGridPool[i].cells) REFUSE("Live motion grid is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->particleSystemPool); i++)
        if (r->particleSystemPool[i].used || arrlen(r->particleSystemPool[i].particles) ||
            arrlen(r->particleSystemPool[i].emitters)) REFUSE("Live particle system is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->particleTypePool); i++)
        if (r->particleTypePool[i].used || r->particleTypePool[i].refCount > 0)
            REFUSE("Live particle type is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->audioEmitters); i++)
        if (r->audioEmitters[i].active || arrlen(r->audioEmitters[i].voices))
            REFUSE("Live audio emitter is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->vertexFormats); i++)
        if (r->vertexFormats[i]) REFUSE("Live vertex format is unsupported");
    for (ptrdiff_t i = 0; i < arrlen(r->vertexBuffers); i++)
        if (r->vertexBuffers[i]) REFUSE("Live vertex buffer is unsupported");
    if (r->vertexBufferCount) REFUSE("Vertex buffer count is unsupported");
    if (r->newVertexFormat) REFUSE("Vertex format construction is active");
#ifdef ENABLE_SW_RENDERER
    SWRendererSnapshotState swState;
    if (!SWRenderer_snapshotGetState(r->renderer, &swState))
        REFUSE("Software renderer has unfinished draw work");
#endif
    if (r->renderer->vtable && r->renderer->vtable->surfaceExists) {
        for (int32_t i = 0; i < 64; i++)
            if (i != r->applicationSurfaceId && r->renderer->vtable->surfaceExists(r->renderer, i))
                REFUSE("User surface is unsupported");
    }
    for (ptrdiff_t i = 0; i < arrlen(r->instances); i++) {
        Instance *inst = r->instances[i];
        if (!inst || inst->destroyed || inst->objectIndex < 0 ||
            (uint32_t)inst->objectIndex >= r->dataWin->objt.count || inst->physicsBody)
            REFUSE("Unsupported live instance");
    }
    return true;
#undef REFUSE
}
const char *Runner_snapshotCheck(Runner *r) {
    const char *reason = NULL;
    return eligible(r, &reason, true) ? NULL : reason;
}

typedef struct { RValue **slots; GMLArray **arrays; } Graph;
static int arrayIndex(const Graph *g, GMLArray *array) {
    for (ptrdiff_t i = 0; i < arrlen(g->arrays); i++) if (g->arrays[i] == array) return (int)i;
    return -1;
}
static bool collect(Graph *g, RValue *slot) {
    if (arrlen(g->slots) >= SNAP_MAX_ITEMS) return false;
    arrput(g->slots, slot);
    switch (slot->type) {
        case RVALUE_UNDEFINED: case RVALUE_REAL: case RVALUE_INT32:
        case RVALUE_BOOL: case RVALUE_ASSETREF: return true;
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64: return true;
#endif
        case RVALUE_STRING: return slot->string != NULL && strlen(slot->string) < SNAP_LIMIT;
        case RVALUE_ARRAY:
            if (!slot->array || slot->array->type != GML_LEGACY_ARRAY) return false;
            if (arrayIndex(g, slot->array) < 0) {
                if (arrlen(g->arrays) >= SNAP_MAX_ITEMS) return false;
                arrput(g->arrays, slot->array);
            }
            return true;
        default: return false;
    }
}
static bool collectMap(Graph *g, IntRValueHashMap *map) {
    for (uint32_t i = 0; i < map->capacity; i++)
        if (map->entries[i].key != INT_RVALUE_HASHMAP_EMPTY_KEY &&
            !collect(g, &map->entries[i].value)) return false;
    return true;
}
static bool collectGraph(Runner *r, Graph *g) {
    for (ptrdiff_t i = 0; i < arrlen(r->instances); i++)
        if (!collectMap(g, &r->instances[i]->selfVars)) return false;
    if (!collectMap(g, &r->vmContext->globalScopeInstance->selfVars)) return false;
    for (ptrdiff_t i = 0; i < arrlen(r->dsMapPool); i++)
        for (ptrdiff_t j = 0; j < shlen(r->dsMapPool[i]); j++)
            if (!collect(g, &r->dsMapPool[i][j].value)) return false;
    for (ptrdiff_t i = 0; i < arrlen(r->dsListPool); i++)
        for (ptrdiff_t j = 0; j < arrlen(r->dsListPool[i].items); j++)
            if (!collect(g, &r->dsListPool[i].items[j])) return false;
    for (ptrdiff_t a = 0; a < arrlen(g->arrays); a++) {
        GMLArray *array = g->arrays[a];
        if (array->legacy.rowCount < 0 || array->legacy.rowCount > 1024) return false;
        for (int32_t row = 0; row < array->legacy.rowCount; row++) {
            GMLArrayRow *rr = &array->legacy.rows[row];
            if (rr->length < 0 || rr->length > 32000) return false;
            for (int32_t col = 0; col < rr->length; col++)
                if (!collect(g, &rr->data[col])) return false;
        }
    }
    return true;
}
static uint32_t slotIndex(const Graph *g, const void *slot) {
    if (!slot) return SNAP_OWNER_NONE;
    for (ptrdiff_t i = 0; i < arrlen(g->slots); i++)
        if (g->slots[i] == slot) return (uint32_t)i;
    return SNAP_OWNER_NONE;
}
static bool buildStringIndex(Writer *w, const Strg *strg) {
    if (strg->count > SNAP_STATIC_STRING_LIMIT ||
        (strg->count && !strg->strings)) return false;
    if (!strg->count) return true;
    uint32_t capacity = 1;
    while (capacity < strg->count * 2u) capacity *= 2u;
    w->stringIndex = (uint32_t *)calloc(capacity, sizeof(uint32_t));
    if (!w->stringIndex) return false;
    w->strg = strg;
    w->stringMask = capacity - 1u;
    for (uint32_t i = 0; i < strg->count; i++) {
        const char *s = strg->strings[i];
        if (!s) continue;
        uint32_t bucket = checksum((const uint8_t *)s, strlen(s)) & w->stringMask;
        while (w->stringIndex[bucket]) {
            const char *other = strg->strings[w->stringIndex[bucket] - 1u];
            if (strcmp(s, other) == 0) break;
            bucket = (bucket + 1u) & w->stringMask;
        }
        if (!w->stringIndex[bucket]) w->stringIndex[bucket] = i + 1u;
    }
    return true;
}
static void putString(Writer *w, const char *s) {
    if (!w->ok) return;
    size_t n = s ? strlen(s) : 0;
    if (n >= SNAP_LIMIT - w->stringBytes) {
        w->ok = false; w->failure = "Runner snapshot strings exceed the size limit"; return;
    }
    w->stringBytes += n + 1;
    if (s && w->stringIndex) {
        uint32_t bucket = checksum((const uint8_t *)s, n) & w->stringMask;
        while (w->stringIndex[bucket]) {
            uint32_t index = w->stringIndex[bucket] - 1u;
            if (strcmp(s, w->strg->strings[index]) == 0) {
                put32(w, SNAP_STRING_REF | index); return;
            }
            bucket = (bucket + 1u) & w->stringMask;
        }
    }
    put32(w, (uint32_t)n); put(w, s, n);
}
static void putIni(Writer *w, const IniFile *ini, uint32_t *entryBudget) {
    if (!ini || ini->count < 0 || !countOK((uint32_t)ini->count) ||
        (ini->count && !ini->sections)) {
        w->ok = false; w->failure = "Invalid INI section count"; return;
    }
    if ((uint32_t)ini->count > *entryBudget) {
        w->ok = false; w->failure = "INI entry budget exceeded"; return;
    }
    *entryBudget -= (uint32_t)ini->count;
    put32(w, (uint32_t)ini->count);
    for (int i = 0; i < ini->count && w->ok; i++) {
        const IniSection *section = &ini->sections[i];
        if (!section->name || section->count < 0 ||
            !countOK((uint32_t)section->count) ||
            (section->count && (!section->keys || !section->values))) {
            w->ok = false; w->failure = "Invalid INI entry count"; return;
        }
        if ((uint32_t)section->count > *entryBudget) {
            w->ok = false; w->failure = "INI entry budget exceeded"; return;
        }
        *entryBudget -= (uint32_t)section->count;
        putString(w, section->name);
        put32(w, (uint32_t)section->count);
        for (int j = 0; j < section->count && w->ok; j++) {
            if (!section->keys[j] || !section->values[j]) {
                w->ok = false; w->failure = "Invalid INI entry"; return;
            }
            putString(w, section->keys[j]);
            putString(w, section->values[j]);
        }
    }
}
static void putValue(Writer *w, const Graph *g, RValue *v) {
    put32(w, (uint32_t)v->type | ((uint32_t)v->gmlStackType << 8) |
             ((uint32_t)v->assetRefType << 16));
    switch (v->type) {
        case RVALUE_UNDEFINED: break;
        case RVALUE_INT32: case RVALUE_BOOL: case RVALUE_ASSETREF: put32(w, (uint32_t)v->int32); break;
        case RVALUE_REAL: { double d = (double)v->real; put(w, &d, sizeof(d)); break; }
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64: put64(w, (uint64_t)v->int64); break;
#endif
        case RVALUE_STRING: putString(w, v->string); break;
        case RVALUE_ARRAY: put32(w, (uint32_t)arrayIndex(g, v->array)); break;
        default: w->ok = false; w->failure = "Unsupported snapshot value"; break;
    }
}
static void putMap(Writer *w, const Graph *g, IntRValueHashMap *map) {
    put32(w, map->count);
    for (uint32_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].key == INT_RVALUE_HASHMAP_EMPTY_KEY) continue;
        put32(w, (uint32_t)map->entries[i].key);
        putValue(w, g, &map->entries[i].value);
    }
}

bool Runner_snapshotCapture(Runner *r, uint64_t gameId, uint8_t **data,
                            size_t *size, const char **reason) {
    if (data) *data = NULL; if (size) *size = 0;
    if (reason) *reason = NULL;
    if (!data || !size) { if (reason) *reason = "Invalid output"; return false; }
#ifdef PLATFORM_BUSY_TICK
    uint64_t phase[6]; phase[0] = nowNanos();
#endif
    if (!eligible(r, reason, true)) return false;
    Graph g = {0};
    if (!collectGraph(r, &g)) {
        if (reason) *reason = "Unsupported value graph";
        arrfree(g.slots); arrfree(g.arrays); return false;
    }
#ifdef PLATFORM_BUSY_TICK
    phase[1] = nowNanos();
#endif
    Writer w = {.ok = true};
    if (!buildStringIndex(&w, &r->dataWin->strg)) {
        if (reason) *reason = "Runner snapshot string dictionary unavailable";
        free(w.stringIndex);
        arrfree(g.slots); arrfree(g.arrays); return false;
    }
    put32(&w, SNAP_MAGIC); put32(&w, SNAP_VERSION); put64(&w, gameId);
    put32(&w, 0); put32(&w, 0); /* length and checksum filled below */
    RunnerPlain plain = makePlain(r); put(&w, &plain, sizeof(plain));
    put32(&w, r->dataWin->room.count);
    for (uint32_t i = 0; i < r->dataWin->room.count; i++) {
        uint8_t initialized = r->savedRoomStates && r->savedRoomStates[i].initialized;
        put(&w, &initialized, 1);
    }
    for (uint32_t i = 0; i < r->dataWin->room.count; i++) {
        const Room *room = &r->dataWin->room.rooms[i];
        uint8_t persistent = room->persistent, loaded = room->payloadLoaded;
        put32(&w, room->width); put32(&w, room->height); put32(&w, room->speed);
        put(&w, &persistent, 1); put(&w, &loaded, 1);
        put32(&w, loaded ? room->tileCount : 0);
        if (loaded) put(&w, room->tiles, (size_t)room->tileCount * sizeof(RoomTile));
    }
    put32(&w, (uint32_t)arrlen(g.arrays));
    for (ptrdiff_t i = 0; i < arrlen(g.arrays); i++) {
        GMLArray *a = g.arrays[i];
        uint32_t owner = slotIndex(&g, a->owner);
        /* A dead legacy owner slot must remain non-null and distinct from
         * every live slot, so a shared array still forks on the next write. */
        if (a->owner && (owner == SNAP_OWNER_NONE ||
            ((RValue *)a->owner)->type != RVALUE_ARRAY ||
            ((RValue *)a->owner)->array != a))
            owner = SNAP_OWNER_ORPHAN;
        put32(&w, owner);
        put32(&w, (uint32_t)a->legacy.rowCount);
        for (int32_t row = 0; row < a->legacy.rowCount; row++)
            put32(&w, (uint32_t)a->legacy.rows[row].length);
    }
    put32(&w, (uint32_t)hmlen(r->tileLayerMap));
    for (ptrdiff_t i = 0; i < hmlen(r->tileLayerMap); i++)
        put(&w, &r->tileLayerMap[i], sizeof(TileLayerMapEntry));
    put32(&w, (uint32_t)arrlen(r->instances));
    for (ptrdiff_t i = 0; i < arrlen(r->instances); i++) {
        InstancePlain ip = instancePlain(r->instances[i]); put(&w, &ip, sizeof(ip));
        putMap(&w, &g, &r->instances[i]->selfVars);
    }
    putMap(&w, &g, &r->vmContext->globalScopeInstance->selfVars);
    put32(&w, (uint32_t)shlen(r->vmContext->varNameMap));
    for (ptrdiff_t i = 0; i < shlen(r->vmContext->varNameMap); i++) {
        putString(&w, r->vmContext->varNameMap[i].key);
        put32(&w, (uint32_t)r->vmContext->varNameMap[i].value);
    }
    put32(&w, (uint32_t)arrlen(r->dsMapPool));
    for (ptrdiff_t i = 0; i < arrlen(r->dsMapPool); i++) {
        DsMapEntry *m = r->dsMapPool[i]; put32(&w, (uint32_t)shlen(m));
        for (ptrdiff_t j = 0; j < shlen(m); j++) {
            putString(&w, m[j].key); putValue(&w, &g, &m[j].value);
        }
    }
    put32(&w, (uint32_t)arrlen(r->dsListPool));
    for (ptrdiff_t i = 0; i < arrlen(r->dsListPool); i++) {
        DsList *list = &r->dsListPool[i]; uint8_t freed = list->freed;
        put(&w, &freed, 1); put32(&w, (uint32_t)arrlen(list->items));
        for (ptrdiff_t j = 0; j < arrlen(list->items); j++) putValue(&w, &g, &list->items[j]);
    }
    for (ptrdiff_t a = 0; a < arrlen(g.arrays); a++) {
        GMLArray *array = g.arrays[a];
        for (int32_t row = 0; row < array->legacy.rowCount; row++)
            for (int32_t col = 0; col < array->legacy.rows[row].length; col++)
                putValue(&w, &g, &array->legacy.rows[row].data[col]);
    }
    put32(&w, (uint32_t)arrlen(r->dsQueuePool));
    put32(&w, (uint32_t)arrlen(r->dsStackPool));
    put32(&w, (uint32_t)arrlen(r->dsPriorityPool));
    put32(&w, (uint32_t)arrlen(r->dsGridPool));
    put32(&w, (uint32_t)arrlen(r->gmlBufferPool));
    put32(&w, (uint32_t)arrlen(r->mpGridPool));
    put32(&w, (uint32_t)arrlen(r->particleSystemPool));
    put32(&w, (uint32_t)arrlen(r->particleTypePool));
    put32(&w, (uint32_t)arrlen(r->audioEmitters));
    put32(&w, (uint32_t)arrlen(r->vertexFormats));
    put32(&w, (uint32_t)arrlen(r->vertexBuffers));
    put32(&w, SNAP_INI_MAGIC);
    uint32_t iniFlags = (r->currentIni ? 1u : 0u) |
        (r->cachedIni ? 2u : 0u) |
        (r->currentIniPath ? 4u : 0u) |
        (r->cachedIniPath ? 8u : 0u) |
        (r->currentIniDirty ? 16u : 0u);
    put32(&w, iniFlags);
    uint32_t iniEntryBudget = SNAP_MAX_ITEMS;
    if (r->currentIniPath) putString(&w, r->currentIniPath);
    if (r->currentIni) putIni(&w, r->currentIni, &iniEntryBudget);
    if (r->cachedIniPath) putString(&w, r->cachedIniPath);
    if (r->cachedIni) putIni(&w, r->cachedIni, &iniEntryBudget);
    free(w.stringIndex);
    arrfree(g.slots); arrfree(g.arrays);
    if (!w.ok || w.n > SNAP_LIMIT) {
        if (reason) *reason = w.failure ? w.failure : "Runner snapshot encoding failed";
        free(w.p); return false;
    }
    uint32_t rawLen = (uint32_t)(w.n - 24);
#ifdef PLATFORM_BUSY_TICK
    phase[2] = nowNanos();
#endif
    uint32_t rawCrc = checksum(w.p + 24, rawLen);
#ifdef PLATFORM_BUSY_TICK
    phase[3] = nowNanos();
#endif
    size_t compressedCap = ZSTD_compressBound(rawLen);
    uint8_t *compressed = (uint8_t *)malloc(compressedCap);
    ZSTD_CCtx *compressor = ZSTD_createCCtx();
    size_t compressedLen = 0;
    bool compressedOk = compressed && compressor;
    if (compressedOk) {
        compressedLen = ZSTD_CCtx_setParameter(compressor, ZSTD_c_compressionLevel, 1);
        compressedOk = !ZSTD_isError(compressedLen);
    }
    if (compressedOk) {
        compressedLen = ZSTD_CCtx_setParameter(compressor, ZSTD_c_minMatch, 4);
        compressedOk = !ZSTD_isError(compressedLen);
    }
    if (compressedOk) {
        compressedLen = ZSTD_compress2(compressor, compressed, compressedCap, w.p + 24, rawLen);
        compressedOk = !ZSTD_isError(compressedLen);
    }
    ZSTD_freeCCtx(compressor);
    if (!compressedOk) {
        if (reason) *reason = "Runner snapshot compression failed";
        free(compressed); free(w.p); return false;
    }
    compressedCap = compressedLen;
#ifdef PLATFORM_BUSY_TICK
    phase[4] = nowNanos();
#endif
    if (compressedCap > SNAP_OUTPUT_LIMIT - 32) {
        if (reason) *reason = "Compressed runner snapshot exceeds the size limit";
        free(compressed); free(w.p); return false;
    }
    Writer out = {.ok = true};
    put32(&out, SNAP_MAGIC); put32(&out, SNAP_VERSION); put64(&out, gameId);
    put32(&out, (uint32_t)compressedCap + 8); put32(&out, 0);
    put32(&out, rawLen); put32(&out, rawCrc);
    put(&out, compressed, compressedCap);
    free(compressed); free(w.p);
    if (!out.ok || out.n > SNAP_OUTPUT_LIMIT) {
        if (reason) *reason = out.failure ? out.failure : "Runner snapshot output failed";
        free(out.p); return false;
    }
    uint32_t crc = checksum(out.p + 24, out.n - 24);
#ifdef PLATFORM_BUSY_TICK
    phase[5] = nowNanos();
    logInfo("State core phases: graph %llu encode %llu crc %llu zstd %llu pack %llu ms; raw %u compressed %lu\n",
        (unsigned long long)((phase[1] - phase[0]) / 1000000),
        (unsigned long long)((phase[2] - phase[1]) / 1000000),
        (unsigned long long)((phase[3] - phase[2]) / 1000000),
        (unsigned long long)((phase[4] - phase[3]) / 1000000),
        (unsigned long long)((phase[5] - phase[4]) / 1000000), rawLen, (unsigned long)compressedCap);
#endif
    out.p[20] = (uint8_t)crc; out.p[21] = (uint8_t)(crc >> 8);
    out.p[22] = (uint8_t)(crc >> 16); out.p[23] = (uint8_t)(crc >> 24);
    *data = out.p; *size = out.n; return true;
}

static char *getString(Reader *rd) {
    uint32_t len = get32(rd);
    if (rd->stringRefs && (len & SNAP_STRING_REF)) {
        uint32_t index = len & ~SNAP_STRING_REF;
        if (!rd->ok || !rd->strg || index >= rd->strg->count ||
            !rd->strg->strings || !rd->strg->strings[index]) {
            rd->ok = false; return NULL;
        }
        const char *source = rd->strg->strings[index];
        size_t n = strlen(source);
        if (n >= SNAP_LIMIT - rd->stringBytes) { rd->ok = false; return NULL; }
        rd->stringBytes += n + 1;
        char *copy = (char *)malloc(n + 1);
        if (!copy) { rd->ok = false; return NULL; }
        memcpy(copy, source, n + 1);
        return copy;
    }
    if (!rd->ok || len >= SNAP_LIMIT - rd->stringBytes ||
        len > rd->n - rd->pos) { rd->ok = false; return NULL; }
    rd->stringBytes += (size_t)len + 1;
    char *out = (char *)malloc((size_t)len + 1);
    if (!out) { rd->ok = false; return NULL; }
    get(rd, out, len); out[len] = 0;
    if (strlen(out) != len) { rd->ok = false; free(out); return NULL; }
    return out;
}
static IniFile *getIni(Reader *rd, uint32_t *entryBudget) {
    uint32_t sections = get32(rd);
    if (!rd->ok || !countOK(sections) || sections > (rd->n - rd->pos) / 8 ||
        sections > *entryBudget) { rd->ok = false; return NULL; }
    *entryBudget -= sections;
    IniFile *ini = (IniFile *)calloc(1, sizeof(*ini));
    if (!ini) { rd->ok = false; return NULL; }
    ini->sections = (IniSection *)calloc(sections ? sections : 1, sizeof(IniSection));
    if (!ini->sections) { free(ini); rd->ok = false; return NULL; }
    ini->count = ini->capacity = (int)sections;
    for (uint32_t i = 0; i < sections && rd->ok; i++) {
        IniSection *section = &ini->sections[i];
        section->name = getString(rd);
        uint32_t count = get32(rd);
        if (!rd->ok || !section->name || !countOK(count) ||
            count > *entryBudget || count > (rd->n - rd->pos) / 8) {
            rd->ok = false; break;
        }
        *entryBudget -= count;
        section->keys = (char **)calloc(count ? count : 1, sizeof(char *));
        section->values = (char **)calloc(count ? count : 1, sizeof(char *));
        if (!section->keys || !section->values) {
            rd->ok = false; break;
        }
        section->count = section->capacity = (int)count;
        for (uint32_t j = 0; j < count && rd->ok; j++) {
            section->keys[j] = getString(rd);
            section->values[j] = getString(rd);
            if (!section->keys[j] || !section->values[j]) rd->ok = false;
        }
    }
    if (!rd->ok) { Ini_free(ini); return NULL; }
    return ini;
}
static RValue *slotPointer(RunnerSnapshot *s, SlotRef ref) {
    switch (ref.kind) {
        case SLOT_GLOBAL: return IntRValueHashMap_findSlot(&s->global->selfVars, (int32_t)ref.index);
        case SLOT_INSTANCE:
            if (ref.container >= (uint32_t)arrlen(s->instances)) return NULL;
            return IntRValueHashMap_findSlot(&s->instances[ref.container]->selfVars, (int32_t)ref.index);
        case SLOT_MAP: {
            if (ref.container >= (uint32_t)arrlen(s->maps)) return NULL;
            DsMapEntry *map = s->maps[ref.container];
            ptrdiff_t at = shgeti(map, ref.key);
            return at >= 0 ? &map[at].value : NULL;
        }
        case SLOT_LIST:
            if (ref.container >= (uint32_t)arrlen(s->lists) ||
                ref.index >= (uint32_t)arrlen(s->lists[ref.container].items)) return NULL;
            return &s->lists[ref.container].items[ref.index];
        case SLOT_ARRAY:
            if (ref.container >= (uint32_t)arrlen(s->arrays)) return NULL;
            return GMLArray_slot(s->arrays[ref.container], (int32_t)ref.index);
    }
    return NULL;
}
static void getValue(Reader *rd, RunnerSnapshot *s, RValue *out, SlotRef ref) {
    if (arrlen(s->slots) >= SNAP_MAX_ITEMS) { rd->ok = false; return; }
    uint32_t tag = get32(rd), type = tag & 255u;
    if (!rd->ok) return;
    RValue value = RValue_makeUndefined();
    value.type = (uint8_t)type;
    value.gmlStackType = (uint8_t)(tag >> 8);
    value.assetRefType = (uint8_t)(tag >> 16);
    switch (type) {
        case RVALUE_UNDEFINED: break;
        case RVALUE_INT32: case RVALUE_BOOL: case RVALUE_ASSETREF:
            value.int32 = (int32_t)get32(rd); break;
        case RVALUE_REAL: { double d = 0; get(rd, &d, sizeof(d)); value.real = (GMLReal)d; break; }
#ifndef NO_RVALUE_INT64
        case RVALUE_INT64: value.int64 = (int64_t)get64(rd); break;
#endif
        case RVALUE_STRING:
            value.string = getString(rd); value.ownsReference = true; break;
        case RVALUE_ARRAY: {
            uint32_t id = get32(rd);
            if (id >= (uint32_t)arrlen(s->arrays)) { rd->ok = false; break; }
            value.array = s->arrays[id]; value.ownsReference = true;
            GMLArray_incRef(value.array);
            break;
        }
        default: rd->ok = false; break;
    }
    if (!rd->ok) { if (value.type == RVALUE_STRING) free((void *)value.string); return; }
    *out = value;
    arrput(s->slots, ref);
}
static void getMap(Reader *rd, RunnerSnapshot *s, IntRValueHashMap *map,
                   SlotKind kind, uint32_t container) {
    uint32_t count = get32(rd);
    if (!rd->ok || !countOK(count)) { rd->ok = false; return; }
    for (uint32_t i = 0; i < count && rd->ok; i++) {
        int32_t key = (int32_t)get32(rd);
        if (key == INT_RVALUE_HASHMAP_EMPTY_KEY || IntRValueHashMap_contains(map, key)) {
            rd->ok = false; break;
        }
        RValue *slot = IntRValueHashMap_getOrInsertUndefined(map, key);
        SlotRef ref = {kind, container, (uint32_t)key, NULL};
        getValue(rd, s, slot, ref);
    }
}
static void breakArrayCycles(RunnerSnapshot *s) {
    for (ptrdiff_t i = 0; i < arrlen(s->arrays); i++) {
        GMLArray *a = s->arrays[i];
        for (int32_t row = 0; row < a->legacy.rowCount; row++)
            for (int32_t col = 0; col < a->legacy.rows[row].length; col++) {
                RValue *v = &a->legacy.rows[row].data[col];
                if (v->type == RVALUE_ARRAY) { RValue_free(v); *v = RValue_makeUndefined(); }
            }
    }
}
static bool visitArray(RunnerSnapshot *s, uint32_t id, uint8_t *colors, unsigned depth) {
    if (depth > 128 || colors[id] == 1) return false;
    if (colors[id] == 2) return true;
    colors[id] = 1;
    GMLArray *a = s->arrays[id];
    for (int32_t row = 0; row < a->legacy.rowCount; row++)
        for (int32_t col = 0; col < a->legacy.rows[row].length; col++) {
            RValue *v = &a->legacy.rows[row].data[col];
            if (v->type != RVALUE_ARRAY) continue;
            uint32_t child = UINT32_MAX;
            for (ptrdiff_t i = 0; i < arrlen(s->arrays); i++)
                if (s->arrays[i] == v->array) { child = (uint32_t)i; break; }
            if (child == UINT32_MAX || !visitArray(s, child, colors, depth + 1)) return false;
        }
    colors[id] = 2;
    return true;
}

static bool buildIndexes(RunnerSnapshot *s, Runner *r) {
    s->objectCount = r->dataWin->objt.count;
    s->byObject = (Instance ***)calloc(s->objectCount ? s->objectCount : 1, sizeof(Instance **));
    s->byExact = (Instance ***)calloc(s->objectCount ? s->objectCount : 1, sizeof(Instance **));
    if (!s->byObject || !s->byExact) return false;
    SnapshotRoom *targetRoom = &s->roomStates[s->plain.roomIndex];
    s->grid = SpatialGrid_tryCreate(targetRoom->width, targetRoom->height);
    if (!s->grid) return false;
    for (ptrdiff_t i = 0; i < arrlen(s->instances); i++) {
        Instance *inst = s->instances[i];
        int id = (int)inst->instanceId;
        if (hmgeti(s->byId, id) >= 0) return false;
        hmput(s->byId, id, inst);
        int32_t object = inst->objectIndex;
        unsigned depth = 0;
        while (object >= 0 && (uint32_t)object < s->objectCount && depth++ < 32) {
            arrput(s->byObject[object], inst);
            object = r->dataWin->objt.objects[object].parentId;
        }
        arrput(s->byExact[inst->objectIndex], inst);
        SpatialGrid_markInstanceAsDirty(s->grid, inst);
    }
    return true;
}

RunnerSnapshot *Runner_snapshotPrepare(Runner *r, uint64_t gameId,
                                       const uint8_t *data, size_t size,
                                       const char **reason) {
#ifdef PLATFORM_BUSY_TICK
    uint64_t prep[6] = {nowNanos(), 0, 0, 0, 0, 0};
#endif
    if (reason) *reason = NULL;
    if (!r || !r->dataWin || !r->vmContext || !data || size < 32 || size > SNAP_OUTPUT_LIMIT) {
        if (reason) *reason = "Invalid runner snapshot"; return NULL;
    }
    if (!eligible(r, reason, false)) return NULL;
    Reader hdr = {data, size, 0, true};
    uint32_t magic = get32(&hdr), version = get32(&hdr);
    uint64_t id = get64(&hdr);
    uint32_t bodyLen = get32(&hdr), crc = get32(&hdr);
    uint32_t rawLen = get32(&hdr), rawCrc = get32(&hdr);
    if (!hdr.ok || magic != SNAP_MAGIC ||
        (version != SNAP_VERSION && version != SNAP_ZSTD_LEGACY_VERSION && version != SNAP_LEGACY_VERSION) ||
        id != gameId ||
        bodyLen != size - 24 || crc != checksum(data + 24, bodyLen) ||
        rawLen < sizeof(RunnerPlain) || rawLen > SNAP_LIMIT - 24) {
        if (reason) *reason = "Runner snapshot header or checksum mismatch"; return NULL;
    }
    uint8_t *raw = (uint8_t *)malloc(rawLen);
    bool decoded = false;
    if (raw && version == SNAP_LEGACY_VERSION) {
        mz_ulong decompressedLen = rawLen;
        decoded = mz_uncompress(raw, &decompressedLen, data + 32, size - 32) == MZ_OK &&
                  decompressedLen == rawLen;
    } else if (raw) {
        ZSTD_frameHeader frame = {0};
        size_t headerResult = ZSTD_getFrameHeader(&frame, data + 32, size - 32);
        size_t frameLen = ZSTD_findFrameCompressedSize(data + 32, size - 32);
        if (headerResult == 0 && frame.frameType == ZSTD_frame &&
            frame.frameContentSize == rawLen && frame.windowSize <= SNAP_LIMIT &&
            !ZSTD_isError(frameLen) && frameLen == size - 32) {
            ZSTD_DCtx *ctx = ZSTD_createDCtx();
            if (ctx) {
                size_t bounded = ZSTD_DCtx_setParameter(ctx, ZSTD_d_windowLogMax, 22);
                if (!ZSTD_isError(bounded)) {
                    size_t decodedLen = ZSTD_decompressDCtx(ctx, raw, rawLen,
                                                               data + 32, size - 32);
                    decoded = !ZSTD_isError(decodedLen) && decodedLen == rawLen;
                }
                ZSTD_freeDCtx(ctx);
            }
        }
    }
    if (!decoded || checksum(raw, rawLen) != rawCrc) {
        if (reason) *reason = "Runner snapshot decompression failed";
        free(raw); return NULL;
    }
#ifdef PLATFORM_BUSY_TICK
    prep[1] = nowNanos();
#endif
    Reader rd = {raw, rawLen, 0, true};
    rd.strg = &r->dataWin->strg;
    rd.stringRefs = version == SNAP_VERSION;
    if (rd.stringRefs && rd.strg->count > SNAP_STATIC_STRING_LIMIT) {
        if (reason) *reason = "Runner snapshot string dictionary unavailable";
        free(raw); return NULL;
    }
    RunnerSnapshot *s = (RunnerSnapshot *)calloc(1, sizeof(*s));
    if (!s) { if (reason) *reason = "Out of memory"; free(raw); return NULL; }
    Room *shadowRooms = NULL;
    const char *failure = NULL;
    get(&rd, &s->plain, sizeof(s->plain));
    if (!rd.ok || s->plain.roomIndex < 0 ||
        (uint32_t)s->plain.roomIndex >= r->dataWin->room.count ||
        s->plain.pendingRoom != -1) goto bad;
    s->room = &r->dataWin->room.rooms[s->plain.roomIndex];
    uint32_t roomCount = get32(&rd);
    if (roomCount != r->dataWin->room.count || !countOK(roomCount)) goto bad;
    s->roomCount = roomCount;
    s->roomInitialized = (bool *)calloc(roomCount ? roomCount : 1, sizeof(bool));
    if (!s->roomInitialized) goto bad;
    for (uint32_t i = 0; i < roomCount; i++) {
        uint8_t b = 0; get(&rd, &b, 1);
        if (b > 1) goto bad;
        s->roomInitialized[i] = b != 0;
    }
    s->roomStates = (SnapshotRoom *)calloc(roomCount ? roomCount : 1, sizeof(SnapshotRoom));
    if (!s->roomStates) goto bad;
    for (uint32_t i = 0; i < roomCount && rd.ok; i++) {
        SnapshotRoom *state = &s->roomStates[i];
        uint8_t persistent = 0, loaded = 0;
        state->width = get32(&rd); state->height = get32(&rd); state->speed = get32(&rd);
        get(&rd, &persistent, 1); get(&rd, &loaded, 1);
        state->tileCount = get32(&rd);
        if (!rd.ok || persistent > 1 || loaded > 1 ||
            !countOK(state->tileCount) || (!loaded && state->tileCount) ||
            (size_t)state->tileCount > (rd.n - rd.pos) / sizeof(RoomTile)) goto bad;
        if (!SpatialGrid_dimensionsValid(state->width, state->height)) goto bad;
        state->persistent = persistent != 0;
        state->payloadLoaded = loaded != 0;
        if (s->roomInitialized[i] && state->persistent) goto bad;
        if (!state->payloadLoaded) continue;
        Room *room = &r->dataWin->room.rooms[i];
        if (!room->payloadLoaded && r->dataWin->lazyLoadRooms && !r->dataWin->lazyLoadFile)
            goto bad;
        if (!room->payloadLoaded && r->dataWin->lazyLoadRooms) {
            if (!shadowRooms) {
                shadowRooms = (Room *)malloc((size_t)roomCount * sizeof(Room));
                if (!shadowRooms) goto bad;
                memcpy(shadowRooms, r->dataWin->room.rooms,
                       (size_t)roomCount * sizeof(Room));
            }
            DataWin shadow = *r->dataWin;
            shadow.room.rooms = shadowRooms;
#ifdef PLATFORM_BUSY_TICK
            uint64_t roomStarted = nowNanos();
#endif
            DataWin_loadRoomPayload(&shadow, (int32_t)i);
#ifdef PLATFORM_BUSY_TICK
            logInfo("State room payload: room %u, %llu ms\n", i,
                (unsigned long long)((nowNanos() - roomStarted) / 1000000));
#endif
            state->stagedPayload = shadowRooms[i];
            state->stagedLoaded = true;
        } else if (!room->payloadLoaded) goto bad;
        state->tiles = (RoomTile *)malloc((state->tileCount ? state->tileCount : 1) * sizeof(RoomTile));
        if (!state->tiles) goto bad;
        get(&rd, state->tiles, (size_t)state->tileCount * sizeof(RoomTile));
    }
    free(shadowRooms); shadowRooms = NULL;
#ifdef PLATFORM_BUSY_TICK
    prep[2] = nowNanos();
#endif
    if (!s->roomStates[s->plain.roomIndex].payloadLoaded ||
        s->roomStates[s->plain.roomIndex].persistent) goto bad;
    uint32_t arrayCount = get32(&rd);
    if (!rd.ok || !countOK(arrayCount)) goto bad;
    uint32_t arrayElements = 0;
    for (uint32_t i = 0; i < arrayCount && rd.ok; i++) {
        uint32_t owner = get32(&rd), rowCount = get32(&rd);
        if (!countOK(rowCount) || rowCount > 1024) goto bad;
        GMLArray *a = (GMLArray *)calloc(1, sizeof(GMLArray));
        if (!a) goto bad;
        a->type = GML_LEGACY_ARRAY; a->refCount = 1;
        a->legacy.rowCount = a->legacy.rowCapacity = (int32_t)rowCount;
        a->legacy.rows = (GMLArrayRow *)calloc(rowCount ? rowCount : 1, sizeof(GMLArrayRow));
        if (!a->legacy.rows) { free(a); goto bad; }
        arrput(s->arrays, a); arrput(s->owners, owner);
        for (uint32_t row = 0; row < rowCount; row++) {
            uint32_t len = get32(&rd);
            if (!rd.ok || len > 32000) goto bad;
            if (len > SNAP_MAX_ITEMS - arrayElements ||
                (size_t)len > (rd.n - rd.pos) / 4) {
                failure = "Array element budget exceeded"; goto bad;
            }
            arrayElements += len;
            GMLArrayRow *rr = &a->legacy.rows[row];
            rr->data = (RValue *)calloc(len ? len : 1, sizeof(RValue));
            if (!rr->data) goto bad;
            rr->length = rr->capacity = (int32_t)len;
        }
    }
    uint32_t tileLayerCount = get32(&rd);
    if (!rd.ok || !countOK(tileLayerCount)) goto bad;
    for (uint32_t i = 0; i < tileLayerCount && rd.ok; i++) {
        TileLayerMapEntry item; get(&rd, &item, sizeof(item));
        if (hmgeti(s->tileLayers, item.key) >= 0) goto bad;
        hmput(s->tileLayers, item.key, item.value);
    }
    uint32_t instanceCount = get32(&rd);
    if (!rd.ok || !countOK(instanceCount)) goto bad;
    for (uint32_t i = 0; i < instanceCount && rd.ok; i++) {
        InstancePlain p; get(&rd, &p, sizeof(p));
        if (!rd.ok || p.object < 0 || (uint32_t)p.object >= r->dataWin->objt.count ||
            p.room < -1 || (p.room >= 0 && (uint32_t)p.room >= r->dataWin->room.count)) goto bad;
        Instance *inst = Instance_create(p.id, p.object, 0, 0);
        setInstancePlain(inst, &p);
        arrput(s->instances, inst);
        getMap(&rd, s, &inst->selfVars, SLOT_INSTANCE, i);
    }
    s->global = Instance_create(0, STRUCT_OBJECT_INDEX, 0, 0);
    getMap(&rd, s, &s->global->selfVars, SLOT_GLOBAL, 0);
    uint32_t varNameCount = get32(&rd);
    if (!rd.ok || !countOK(varNameCount)) goto bad;
    for (uint32_t i = 0; i < varNameCount && rd.ok; i++) {
        char *key = getString(&rd);
        if (!rd.ok || !key) goto bad;
        if (shgeti(s->varNames, key) >= 0) { free(key); goto bad; }
        int32_t value = (int32_t)get32(&rd);
        shput(s->varNames, key, value);
    }
    uint32_t mapPoolLen = get32(&rd);
    if (!rd.ok || !countOK(mapPoolLen)) goto bad;
    for (uint32_t i = 0; i < mapPoolLen && rd.ok; i++) {
        DsMapEntry *map = NULL; arrput(s->maps, map);
        uint32_t count = get32(&rd);
        if (!rd.ok || !countOK(count)) goto bad;
        for (uint32_t j = 0; j < count && rd.ok; j++) {
            char *key = getString(&rd);
            if (!rd.ok || !key) goto bad;
            if (shgeti(s->maps[i], key) >= 0) { free(key); goto bad; }
            RValue value = RValue_makeUndefined();
            shput(s->maps[i], key, value);
            ptrdiff_t at = shgeti(s->maps[i], key);
            SlotRef ref = {SLOT_MAP, i, 0, key};
            getValue(&rd, s, &s->maps[i][at].value, ref);
        }
    }
    uint32_t listPoolLen = get32(&rd);
    if (!rd.ok || !countOK(listPoolLen)) goto bad;
    for (uint32_t i = 0; i < listPoolLen && rd.ok; i++) {
        DsList list = {0}; arrput(s->lists, list);
        uint8_t freed = 0; get(&rd, &freed, 1);
        uint32_t count = get32(&rd);
        if (!rd.ok || freed > 1 || !countOK(count) || (freed && count)) goto bad;
        s->lists[i].freed = freed != 0;
        for (uint32_t j = 0; j < count && rd.ok; j++) {
            RValue value = RValue_makeUndefined(); arrput(s->lists[i].items, value);
            SlotRef ref = {SLOT_LIST, i, j, NULL};
            getValue(&rd, s, &s->lists[i].items[j], ref);
        }
    }
    for (uint32_t a = 0; a < arrayCount && rd.ok; a++) {
        GMLArray *array = s->arrays[a];
        for (int32_t row = 0; row < array->legacy.rowCount; row++)
            for (int32_t col = 0; col < array->legacy.rows[row].length && rd.ok; col++) {
                SlotRef ref = {SLOT_ARRAY, a, (uint32_t)row * GML_LEGACY_ARRAY_STRIDE + (uint32_t)col, NULL};
                getValue(&rd, s, &array->legacy.rows[row].data[col], ref);
            }
    }
    uint32_t dormant[SNAP_DORMANT_POOL_COUNT] = {0};
    if (rd.ok && rd.pos != rd.n) {
        if (rd.n - rd.pos < sizeof(dormant)) goto bad;
        uint32_t total = 0;
        for (uint32_t i = 0; i < SNAP_DORMANT_POOL_COUNT; i++) {
            dormant[i] = get32(&rd);
            if (!rd.ok || dormant[i] > SNAP_DORMANT_SLOT_LIMIT - total) goto bad;
            total += dormant[i];
        }
    }
    /* All recorded slots are tombstones. Their lengths alone reproduce the
     * resource ID allocator's next reuse choice. */
#define MAKE_DORMANT(field, type, index) do { \
    if (dormant[index]) { \
        arrsetlen(s->field, dormant[index]); \
        memset(s->field, 0, (size_t)dormant[index] * sizeof(type)); \
    } \
} while (0)
    MAKE_DORMANT(queues, DsQueue, 0);
    MAKE_DORMANT(stacks, DsStack, 1);
    MAKE_DORMANT(priorities, DsPriority, 2);
    MAKE_DORMANT(grids, DsGrid, 3);
    MAKE_DORMANT(buffers, GmlBuffer, 4);
    MAKE_DORMANT(mpGrids, MpGrid, 5);
    MAKE_DORMANT(particleSystems, ParticleSystem, 6);
    MAKE_DORMANT(particleTypes, ParticleType, 7);
    MAKE_DORMANT(emitters, AudioEmitter, 8);
    MAKE_DORMANT(vertexFormats, VmVertexFormat *, 9);
    MAKE_DORMANT(vertexBuffers, Buffer_Vertex *, 10);
#undef MAKE_DORMANT
    for (ptrdiff_t i = 0; i < arrlen(s->queues); i++) s->queues[i].freed = true;
    for (ptrdiff_t i = 0; i < arrlen(s->stacks); i++) s->stacks[i].freed = true;
    for (ptrdiff_t i = 0; i < arrlen(s->priorities); i++) s->priorities[i].freed = true;
    for (ptrdiff_t i = 0; i < arrlen(s->grids); i++) s->grids[i].freed = true;
    if (rd.ok && rd.pos != rd.n) {
        uint32_t magic = get32(&rd), flags = get32(&rd);
        if (!rd.ok || magic != SNAP_INI_MAGIC || (flags & ~31u)) goto bad;
        uint32_t entries = SNAP_MAX_ITEMS;
        if (flags & 4u) s->currentIniPath = getString(&rd);
        if (flags & 1u) s->currentIni = getIni(&rd, &entries);
        if (flags & 8u) s->cachedIniPath = getString(&rd);
        if (flags & 2u) s->cachedIni = getIni(&rd, &entries);
        if (!rd.ok || ((flags & 4u) && !s->currentIniPath) ||
            ((flags & 1u) && !s->currentIni) ||
            ((flags & 8u) && !s->cachedIniPath) ||
            ((flags & 2u) && !s->cachedIni)) goto bad;
        s->currentIniDirty = (flags & 16u) != 0;
    }
    if (!rd.ok || rd.pos != rd.n) goto bad;
    for (uint32_t a = 0; a < arrayCount; a++) {
        uint32_t owner = s->owners[a];
        if (owner == SNAP_OWNER_NONE) continue;
        if (owner == SNAP_OWNER_ORPHAN) {
            s->arrays[a]->owner = s->arrays[a]; /* stable, non-slot identity */
            continue;
        }
        if (owner >= (uint32_t)arrlen(s->slots)) goto bad;
        RValue *slot = slotPointer(s, s->slots[owner]);
        /* Earlier captures could name a live slot whose address had been
         * recycled after the array lost its owner. It is still an orphan. */
        s->arrays[a]->owner = slot && slot->type == RVALUE_ARRAY &&
            slot->array == s->arrays[a] ? (void *)slot : (void *)s->arrays[a];
    }
#ifdef PLATFORM_BUSY_TICK
    prep[3] = nowNanos();
#endif
    uint8_t *colors = (uint8_t *)calloc(arrayCount ? arrayCount : 1, 1);
    if (!colors) goto bad;
    bool acyclic = true;
    for (uint32_t a = 0; a < arrayCount; a++)
        if (!visitArray(s, a, colors, 0)) { acyclic = false; break; }
    free(colors);
    if (!acyclic || !buildIndexes(s, r)) goto bad;
#ifdef PLATFORM_BUSY_TICK
    prep[4] = nowNanos();
#endif
    free(raw);
#ifdef PLATFORM_BUSY_TICK
    prep[5] = nowNanos();
    logInfo("State prepare phases: decode %llu rooms %llu values %llu indexes %llu free %llu ms\n",
        (unsigned long long)((prep[1] - prep[0]) / 1000000),
        (unsigned long long)((prep[2] - prep[1]) / 1000000),
        (unsigned long long)((prep[3] - prep[2]) / 1000000),
        (unsigned long long)((prep[4] - prep[3]) / 1000000),
        (unsigned long long)((prep[5] - prep[4]) / 1000000));
#endif
    return s;
bad:
    free(shadowRooms);
    if (reason) *reason = failure ? failure : "Malformed or unsupported runner snapshot";
    breakArrayCycles(s);
    Runner_snapshotFree(s);
    free(raw);
    return NULL;
}

static void applyPlain(Runner *r, const RunnerPlain *p) {
    r->currentRoomIndex = p->roomIndex; r->currentRoomOrderPosition = p->roomOrder;
    r->frameCount = p->frame; r->pendingRoom = p->pendingRoom;
    r->nextInstanceId = p->nextInstanceId;
    r->collisionQueryCounter = p->collisionQueryCounter;
    r->gameStartFired = p->gameStartFired; r->viewsEnabled = p->viewsEnabled;
    r->drawBackgroundColor = p->drawBackgroundColor; r->backgroundColor = p->backgroundColor;
    r->appSurfaceEnabled = p->appSurfaceEnabled; r->appSurfaceAutoDraw = p->appSurfaceAutoDraw;
    r->usingAppSurface = p->usingAppSurface; r->drawEnabled = p->drawEnabled;
    r->dateTimeLocal = p->dateTimeLocal;
    r->collisionCompatibilityMode = p->collisionCompatibilityMode;
    r->applyOffsetForPrimitives = p->applyOffsetForPrimitives;
    r->applicationWidth = p->applicationWidth; r->applicationHeight = p->applicationHeight;
    r->oldApplicationWidth = p->oldApplicationWidth;
    r->oldApplicationHeight = p->oldApplicationHeight;
    r->guiWidth = p->guiWidth; r->guiHeight = p->guiHeight;
    r->cursorSprite = p->cursorSprite; r->cursorSpriteSubimage = p->cursorSpriteSubimage;
    r->currentCursor = p->currentCursor; r->nextLayerId = p->nextLayerId;
    r->nextLayerDrawOrder = p->nextLayerDrawOrder; r->nextCallLaterId = p->nextCallLaterId;
    r->asyncBufferNextRequestId = p->asyncBufferNextRequestId;
    r->xboxAsyncIdCounter = p->xboxAsyncIdCounter;
    r->lastMusicInstance = p->lastMusicInstance; r->viewCurrent = p->viewCurrent;
    r->vmContext->nextDynamicVarID = p->nextDynamicVarID;
    r->currentVertexFormatId = p->currentVertexFormatId;
    r->vertexBufferCount = p->vertexBufferCount;
    r->vertexFormatBit = p->vertexFormatBit;
    memcpy(r->surfaceStack, p->surfaceStack, sizeof(r->surfaceStack));
    r->score = p->score; r->lives = p->lives; r->health = p->health;
    r->deltaTime = p->deltaTime; r->gameSpeedOverride = p->gameSpeedOverride;
    r->mpPotMaxrot = p->mpPotMaxrot; r->mpPotStep = p->mpPotStep;
    r->mpPotAhead = p->mpPotAhead; r->mpPotOnSpot = p->mpPotOnSpot;
    r->random = p->random;
    r->dataWin->gen8.gms2FPS = p->gms2FPS;
    uint64_t now = nowNanos();
    r->gameStartTime = now >= p->elapsedGameNanos ? now - p->elapsedGameNanos : 0;
    r->fpsWindowStartNanos = now;
    r->fpsWindowStartFrame = r->frameCount;
    memcpy(r->views, p->views, sizeof(r->views));
    memcpy(r->defaultCameras, p->defaultCameras, sizeof(r->defaultCameras));
    memcpy(r->userCameras, p->userCameras, sizeof(r->userCameras));
    r->surfaceCamera = p->surfaceCamera; r->guiCamera = p->guiCamera;
    memcpy(r->backgrounds, p->backgrounds, sizeof(r->backgrounds));
    if (r->keyboard) *r->keyboard = p->keyboard;
    if (r->gamepads) *r->gamepads = p->gamepads;
    if (r->mouse) *r->mouse = p->mouse;
    memcpy((uint8_t *)r->renderer + offsetof(Renderer, drawColor),
           p->rendererBefore, sizeof(p->rendererBefore));
    memcpy((uint8_t *)r->renderer + offsetof(Renderer, gmlMatrices),
           p->rendererAfter, sizeof(p->rendererAfter));
#ifdef ENABLE_SW_RENDERER
    SWRenderer_snapshotApplyState(r->renderer, &p->swState);
#endif
}

void Runner_snapshotApply(Runner *r, RunnerSnapshot *s) {
    if (!r || !s || s->applied) return;
    s->oldGlobal = r->vmContext->globalScopeInstance;
    s->oldInstances = r->instances; s->oldMaps = r->dsMapPool;
    s->oldLists = r->dsListPool;
    s->oldQueues = r->dsQueuePool; s->oldStacks = r->dsStackPool;
    s->oldPriorities = r->dsPriorityPool; s->oldGrids = r->dsGridPool;
    s->oldBuffers = r->gmlBufferPool; s->oldMpGrids = r->mpGridPool;
    s->oldParticleSystems = r->particleSystemPool;
    s->oldParticleTypes = r->particleTypePool;
    s->oldEmitters = r->audioEmitters;
    s->oldVertexFormats = r->vertexFormats;
    s->oldVertexBuffers = r->vertexBuffers;
    s->oldVarNames = (SnapshotVarName *)r->vmContext->varNameMap;
    s->oldTileLayers = r->tileLayerMap;
    s->oldById = r->instancesById;
    s->oldByObject = r->instancesByObject; s->oldByExact = r->instancesByExactObject;
    s->oldGrid = r->spatialGrid;
    s->oldCurrentIni = r->currentIni;
    s->oldCurrentIniPath = r->currentIniPath;
    s->oldCachedIni = r->cachedIni; s->oldCachedIniPath = r->cachedIniPath;

    r->vmContext->globalScopeInstance = s->global;
    r->vmContext->varNameMap = (void *)s->varNames;
    r->instances = s->instances; r->dsMapPool = s->maps; r->dsListPool = s->lists;
    r->dsQueuePool = s->queues; r->dsStackPool = s->stacks;
    r->dsPriorityPool = s->priorities; r->dsGridPool = s->grids;
    r->gmlBufferPool = s->buffers; r->mpGridPool = s->mpGrids;
    r->particleSystemPool = s->particleSystems;
    r->particleTypePool = s->particleTypes; r->audioEmitters = s->emitters;
    r->vertexFormats = s->vertexFormats; r->vertexBuffers = s->vertexBuffers;
    r->instancesById = s->byId;
    r->instancesByObject = s->byObject; r->instancesByExactObject = s->byExact;
    r->spatialGrid = s->grid; r->tileLayerMap = s->tileLayers;
    r->currentRoom = s->room;
    if (r->savedRoomStates) for (uint32_t i = 0; i < s->roomCount; i++)
        Runner_freeSavedRoomStateResources(&r->savedRoomStates[i]);
    for (uint32_t i = 0; i < s->roomCount; i++) {
        Room *room = &r->dataWin->room.rooms[i];
        SnapshotRoom *state = &s->roomStates[i];
        if (state->payloadLoaded) {
            if (state->stagedLoaded) {
                Room *staged = &state->stagedPayload;
                room->backgrounds = staged->backgrounds;
                room->views = staged->views;
                room->gameObjects = staged->gameObjects;
                room->gameObjectCount = staged->gameObjectCount;
                room->layers = staged->layers;
                room->layerCount = staged->layerCount;
                room->payloadLoaded = true;
                room->tiles = staged->tiles;
                room->tileCount = staged->tileCount;
                state->stagedLoaded = false;
            }
            state->displacedTiles = room->tiles;
            room->tiles = state->tiles;
            room->tileCount = state->tileCount;
        } else if (room->payloadLoaded && r->dataWin->lazyLoadRooms && !room->eagerlyLoaded) {
            DataWin_freeRoomPayload(room);
        }
        room->width = state->width; room->height = state->height;
        room->speed = state->speed; room->persistent = state->persistent;
    }
    r->currentIni = s->currentIni;
    r->currentIniPath = s->currentIniPath;
    r->currentIniDirty = s->currentIniDirty;
    r->cachedIni = s->cachedIni;
    r->cachedIniPath = s->cachedIniPath;
    for (uint32_t i = 0; i < r->dataWin->room.count; i++)
        if (r->savedRoomStates) r->savedRoomStates[i].initialized = s->roomInitialized[i];
    applyPlain(r, &s->plain);
    if (r->eventDispatchInstances) arrsetlen(r->eventDispatchInstances, 0);
    if (r->cachedDrawables) arrsetlen(r->cachedDrawables, 0);
    r->vmContext->currentInstance = NULL;
    r->vmContext->otherInstance = NULL;
    r->vmContext->currentArrayOwner = NULL;
    r->drawableListStructureDirty = true;
    r->drawableListSortDirty = false;
    s->applied = true;
}
