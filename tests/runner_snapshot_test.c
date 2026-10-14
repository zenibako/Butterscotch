/* Core-only synthetic fixture for the Undertale (WAD16) snapshot contract. */
#include "runner_snapshot.h"
#include "stb_ds.h"
#include "log.h"
#include "spatial_grid.h"
#include "miniz.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Host adapter for the core test, with no Pocket platform dependency. */
void platformLog(logType type, const char *format, va_list args) {
    if (type >= LOG_TYPE_WARNING) vfprintf(stderr, format, args);
}

static bool noSurface(Renderer *renderer, int32_t id) {
    (void)renderer; (void)id;
    return false;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void write32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t hashBytes(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static uint8_t *decodeRaw(const uint8_t *bytes, size_t size, uint32_t *rawLenOut) {
    uint32_t rawLen = le32(bytes + 24);
    uint8_t *raw = malloc(rawLen);
    assert(raw);
    if (le32(bytes + 4) >= 4) {
        assert(ZSTD_decompress(raw, rawLen, bytes + 32, size - 32) == rawLen);
    } else {
        mz_ulong n = rawLen;
        assert(le32(bytes + 4) == 3);
        assert(mz_uncompress(raw, &n, bytes + 32, size - 32) == MZ_OK && n == rawLen);
    }
    *rawLenOut = rawLen;
    return raw;
}
static void encodeRaw(uint8_t **bytes, size_t *size, const uint8_t *raw,
                      uint32_t rawLen, uint32_t version) {
    size_t cap = version >= 4 ? ZSTD_compressBound(rawLen) : mz_compressBound(rawLen);
    uint8_t *out = malloc(cap + 32);
    assert(out);
    if (version >= 4) {
        cap = ZSTD_compress(out + 32, cap, raw, rawLen, 1);
        assert(!ZSTD_isError(cap));
    } else {
        mz_ulong n = cap;
        assert(mz_compress2(out + 32, &n, raw, rawLen, MZ_DEFAULT_COMPRESSION) == MZ_OK);
        cap = n;
    }
    memcpy(out, *bytes, 32);
    write32(out + 4, version);
    write32(out + 16, (uint32_t)cap + 8);
    write32(out + 24, rawLen);
    write32(out + 28, hashBytes(raw, rawLen));
    write32(out + 20, hashBytes(out + 24, cap + 8));
    free(*bytes); *bytes = out; *size = cap + 32;
}
static size_t roomBlock(const uint8_t *p, size_t n, uint32_t rooms) {
    for (size_t i = 0; i + 14 < n; i++)
        if (le32(p + i) == rooms && le32(p + i + 4 + rooms) == 320 &&
            le32(p + i + 8 + rooms) == 240) return i;
    assert(false); return 0;
}
static void repack(uint8_t **bytes, size_t *size,
                   void (*change)(uint8_t *, size_t)) {
    uint32_t rawLen;
    uint8_t *raw = decodeRaw(*bytes, *size, &rawLen);
    change(raw, rawLen);
    encodeRaw(bytes, size, raw, rawLen, le32(*bytes + 4));
    free(raw);
}
static void invalidRoomFlag(uint8_t *raw, size_t n) {
    size_t at = roomBlock(raw, n, 1);
    raw[at + 4] = 2;
}
static void hugeRoom(uint8_t *raw, size_t n) {
    size_t at = roomBlock(raw, n, 1);
    write32(raw + at + 5, 4194240);
    write32(raw + at + 9, 4194240);
}
static void invalidArrayCountAfterRooms(uint8_t *raw, size_t n) {
    size_t at = roomBlock(raw, n, 2);
    write32(raw + at + 4 + 2 + 2 * 18, 65537);
}
static void oversizedArrayShape(uint8_t *raw, size_t n) {
    for (size_t i = 0; i + 20 < n; i++) {
        if (le32(raw + i + 4) == 3 && le32(raw + i + 8) == 32000 &&
            le32(raw + i + 12) == 32000 && le32(raw + i + 16) == 1) {
            write32(raw + i + 16, 32000);
            return;
        }
    }
    assert(false);
}
static void invalidIniMagic(uint8_t *raw, size_t n) {
    for (size_t i = n - 8; i > 0; i--)
        if (le32(raw + i) == 0x31494e49u) {
            write32(raw + i, 0);
            return;
        }
    assert(false);
}
static void removeIniAndMaybeDormantTail(uint8_t **bytes, size_t *size,
                                         size_t drop) {
    uint32_t rawLen;
    uint8_t *raw = decodeRaw(*bytes, *size, &rawLen);
    assert(drop <= rawLen);
    rawLen -= (uint32_t)drop;
    encodeRaw(bytes, size, raw, rawLen, le32(*bytes + 4));
    free(raw);
}

static void fixture(Runner *r, DataWin *dw, VMContext *vm, Renderer *renderer,
                    RendererVtable *vtable, Room *room, GameObject *object) {
    memset(r, 0, sizeof(*r));
    memset(dw, 0, sizeof(*dw));
    memset(vm, 0, sizeof(*vm));
    memset(renderer, 0, sizeof(*renderer));
    memset(vtable, 0, sizeof(*vtable));
    memset(room, 0, sizeof(*room));
    memset(object, 0, sizeof(*object));
    dw->gen8.wadVersion = 16;
    dw->room.rooms = room;
    dw->room.count = 1;
    dw->objt.objects = object;
    dw->objt.count = 1;
    room->width = 320;
    room->height = 240;
    room->payloadLoaded = true;
    r->dataWin = dw;
    r->vmContext = vm;
    r->renderer = renderer;
    r->currentRoom = room;
    r->currentRoomIndex = 0;
    r->currentRoomOrderPosition = 0;
    r->pendingRoom = -1;
    r->nextInstanceId = 100003;
    r->frameCount = 42;
    r->gameStartFired = true;
    r->spatialGrid = SpatialGrid_create(room->width, room->height);
    r->instancesByObject = calloc(1, sizeof(Instance **));
    r->instancesByExactObject = calloc(1, sizeof(Instance **));
    vm->runner = r;
    vm->dataWin = dw;
    vm->globalScopeInstance = Instance_create(0, STRUCT_OBJECT_INDEX, 0, 0);
    vtable->surfaceExists = noSurface;
    renderer->vtable = vtable;
    renderer->runner = r;
}

static void destroyFixture(Runner *r) {
    Instance_free(r->vmContext->globalScopeInstance);
    for (ptrdiff_t i = 0; i < arrlen(r->instances); i++) Instance_free(r->instances[i]);
    arrfree(r->instances);
    for (ptrdiff_t i = 0; i < arrlen(r->dsMapPool); i++) {
        for (ptrdiff_t j = 0; j < shlen(r->dsMapPool[i]); j++) {
            free(r->dsMapPool[i][j].key);
            RValue_free(&r->dsMapPool[i][j].value);
        }
        shfree(r->dsMapPool[i]);
    }
    arrfree(r->dsMapPool);
    for (ptrdiff_t i = 0; i < arrlen(r->dsListPool); i++) {
        for (ptrdiff_t j = 0; j < arrlen(r->dsListPool[i].items); j++)
            RValue_free(&r->dsListPool[i].items[j]);
        arrfree(r->dsListPool[i].items);
    }
    arrfree(r->dsListPool);
    arrfree(r->dsQueuePool);
    arrfree(r->dsStackPool); arrfree(r->dsPriorityPool); arrfree(r->dsGridPool);
    arrfree(r->gmlBufferPool); arrfree(r->mpGridPool);
    arrfree(r->particleSystemPool); arrfree(r->particleTypePool);
    arrfree(r->audioEmitters); arrfree(r->vertexFormats); arrfree(r->vertexBuffers);
    for (ptrdiff_t i = 0; i < shlen(r->vmContext->varNameMap); i++)
        free(r->vmContext->varNameMap[i].key);
    shfree(r->vmContext->varNameMap);
    free(r->currentRoom->tiles);
    hmfree(r->tileLayerMap);
    hmfree(r->instancesById);
    for (uint32_t i = 0; i < r->dataWin->objt.count; i++) {
        arrfree(r->instancesByObject[i]);
        arrfree(r->instancesByExactObject[i]);
    }
    free(r->instancesByObject);
    free(r->instancesByExactObject);
    SpatialGrid_free(r->spatialGrid);
    Ini_free(r->currentIni); free(r->currentIniPath);
    Ini_free(r->cachedIni); free(r->cachedIniPath);
    arrfree(r->eventDispatchInstances);
    arrfree(r->cachedDrawables);
}

static void testRoundTrip(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    Instance *first = Instance_create(100001, 0, 11, 22);
    Instance *second = Instance_create(100002, 0, 33, 44);
    first->alarm[3] = 9;
    second->alarm[5] = 17;
    arrput(r.instances, first);
    arrput(r.instances, second);
    GMLArray *shared = GMLArray_create(&dw, 2);
    GMLArray_set(shared, 0, RValue_makeReal(73));
    Instance_setSelfVar(first, 10, RValue_makeArray(shared));
    Instance_setSelfVar(second, 11, RValue_makeArray(shared));
    Instance_setSelfVar(vm.globalScopeInstance, 12, RValue_makeReal(91));
    shput(vm.varNameMap, strdup("custom_state"), 12);
    vm.nextDynamicVarID = 130;
    DsMapEntry *map = NULL;
    shput(map, strdup("flag"), RValue_makeBool(true));
    arrput(r.dsMapPool, map);
    DsList list = {0};
    arrput(list.items, RValue_makeReal(17));
    arrput(r.dsListPool, list);
    room.tileCount = 1;
    room.tiles = calloc(1, sizeof(RoomTile));
    room.tiles[0].instanceID = 400;
    room.tiles[0].x = 19;
    RValue *owner = IntRValueHashMap_findSlot(&first->selfVars, 10);
    shared->owner = owner;
    GMLArray_decRef(shared);
    r.random = Random_create(123);
    uint32_t expectedRandom = r.random.state[0];
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCheck(&r) == NULL);
    assert(Runner_snapshotCapture(&r, 0x1234, &bytes, &size, &reason));
    assert(bytes && size > 0);
    RunnerSnapshot *prepared = Runner_snapshotPrepare(&r, 0x1234, bytes, size, &reason);
    assert(prepared);
    first->x = 700;
    first->alarm[3] = -1;
    r.frameCount = 100;
    r.random.state[0] = 0;
    vm.nextDynamicVarID = 900;
    room.tiles[0].x = 900;
    r.dsMapPool[0][0].value.int32 = 0;
    r.dsListPool[0].items[0].real = 900;
    Runner_snapshotApply(&r, prepared);
    Runner_snapshotFree(prepared);
    assert(r.frameCount == 42 && r.nextInstanceId == 100003);
    assert(r.instances[0]->instanceId == 100001 && r.instances[1]->instanceId == 100002);
    assert(r.instances[0]->x == 11 && r.instances[0]->alarm[3] == 9);
    assert(r.instances[1]->alarm[5] == 17 && r.random.state[0] == expectedRandom);
    RValue a = Instance_getSelfVar(r.instances[0], 10);
    RValue b = Instance_getSelfVar(r.instances[1], 11);
    assert(a.type == RVALUE_ARRAY && b.type == RVALUE_ARRAY && a.array == b.array);
    assert(a.array->owner == IntRValueHashMap_findSlot(&r.instances[0]->selfVars, 10));
    assert(GMLArray_get(a.array, 0).real == 73);
    assert(Instance_getSelfVar(vm.globalScopeInstance, 12).real == 91);
    assert(vm.nextDynamicVarID == 130);
    assert(shget(vm.varNameMap, "custom_state") == 12);
    assert(r.dsMapPool[0][0].value.int32 == 1);
    assert(r.dsListPool[0].items[0].real == 17);
    assert(room.tiles[0].x == 19);
    free(bytes);
    destroyFixture(&r);
}

static void testRejectsBadInputAndLiveResources(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    arrsetcap(r.dsQueuePool, 4); /* Allocated capacity without a live queue. */
    arrput(r.eventDispatchInstances, NULL); /* Scratch survives completed dispatch. */
    assert(Runner_snapshotCheck(&r) == NULL);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 7, &bytes, &size, &reason));
    assert(Runner_snapshotPrepare(&r, 8, bytes, size, &reason) == NULL);
    assert(Runner_snapshotPrepare(&r, 7, bytes, size - 1, &reason) == NULL);
    bytes[size / 2] ^= 0x80;
    assert(Runner_snapshotPrepare(&r, 7, bytes, size, &reason) == NULL);
    assert(r.frameCount == 42);
    free(bytes);
    r.openTextFiles[0].isOpen = true;
    assert(Runner_snapshotCheck(&r) != NULL);
    assert(!Runner_snapshotCapture(&r, 7, &bytes, &size, &reason));
    assert(reason != NULL);
    destroyFixture(&r);
}

static bool listed(const char **list, int count, const char *text) {
    for (int i = 0; i < count; i++) if (list[i] && !strcmp(list[i], text)) return true;
    return false;
}

static void testBlockersListsEveryRefusal(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    const char *why[8] = {0};
    assert(Runner_snapshotBlockers(&r, why, 8) == 0);
    assert(why[0] == NULL);

    r.openTextFiles[0].isOpen = true;
    r.paused = true;
    /* The preflight stops at its first refusal; the list has both. */
    const char *first = Runner_snapshotCheck(&r);
    assert(first != NULL);
    assert(Runner_snapshotBlockers(&r, why, 8) == 2);
    assert(listed(why, 2, "Open text file"));
    assert(listed(why, 2, "Paused runner is unsupported for capture"));
    assert(listed(why, 2, first));
    assert(why[2] == NULL);

    /* A repeated refusal is listed once, and the count is the total even
     * when the caller's array is shorter. */
    r.openTextFiles[1].isOpen = true;
    const char *one[1] = {0};
    assert(Runner_snapshotBlockers(&r, one, 1) == 2);
    assert(one[0] != NULL);
    assert(Runner_snapshotBlockers(&r, NULL, 0) == 2);

    /* Listing changes nothing: the runner captures again once it is clear. */
    r.openTextFiles[0].isOpen = false;
    r.openTextFiles[1].isOpen = false;
    r.paused = false;
    assert(Runner_snapshotBlockers(&r, why, 8) == 0);
    assert(Runner_snapshotCheck(&r) == NULL);

    /* The vertex buffer table is a plain array sized by vertexBufferCount,
     * not an stb_ds array: the preflight must bound it by that count. */
    r.vertexBufferCount = 4;
    r.vertexBuffers = calloc(4, sizeof(*r.vertexBuffers));
    assert(r.vertexBuffers);
    assert(Runner_snapshotBlockers(&r, why, 8) == 1);
    assert(listed(why, 1, "Vertex buffer count is unsupported"));
    r.vertexBuffers[3] = (Buffer_Vertex *)&r;   /* any non-null entry is a live buffer */
    assert(Runner_snapshotBlockers(&r, why, 8) == 2);
    assert(listed(why, 2, "Live vertex buffer is unsupported"));
    assert(Runner_snapshotCheck(&r) != NULL);
    free(r.vertexBuffers);
    r.vertexBuffers = NULL;
    r.vertexBufferCount = 0;
    assert(Runner_snapshotBlockers(&r, why, 8) == 0);

    const char *none[2] = {0};
    assert(Runner_snapshotBlockers(NULL, none, 2) == 1);
    assert(none[0] && !strcmp(none[0], "Runner is not ready"));
    destroyFixture(&r);
}

static void testBlockersWithInvalidSpriteRegistry(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    const char *why[8] = {0};
    r.paused = true;
    dw.sprt.count = 1;
    assert(dw.sprt.sprites == NULL);
    assert(!strcmp(Runner_snapshotCheck(&r), "Invalid sprite registry"));
    assert(Runner_snapshotBlockers(&r, why, 8) == 2);
    assert(listed(why, 2, "Invalid sprite registry"));
    assert(listed(why, 2, "Paused runner is unsupported for capture"));
    assert(Runner_snapshotBlockers(&r, NULL, 0) == 2);

    dw.sprt.count = 0;
    dw.sprt.parsedCount = 1;
    assert(Runner_snapshotBlockers(&r, why, 8) == 2);
    assert(listed(why, 2, "Invalid sprite registry"));
    dw.sprt.parsedCount = 0;
    r.paused = false;
    assert(Runner_snapshotBlockers(&r, why, 8) == 0);
    destroyFixture(&r);
}

static void testDynamicSpriteCaptureEligibility(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    dw.sprt.sprites = calloc(1, sizeof(Sprite));
    assert(dw.sprt.sprites);
    dw.sprt.count = 1;
    dw.sprt.parsedCount = 1;
    dw.sprt.sprites[0].name = "__newsprite0"; /* A loaded asset's name is irrelevant. */
    dw.sprt.sprites[0].textureCount = 1;
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCheck(&r) == NULL);
    assert(Runner_snapshotCapture(&r, 7, &bytes, &size, &reason));
    uint32_t dynamic = DataWin_allocSpriteSlot(&dw, 1);
    assert(dynamic == 1);
    dw.sprt.sprites[dynamic].textureCount = 1;
    assert(Runner_snapshotCheck(&r) != NULL);
    const char *allocatedName = dw.sprt.sprites[dynamic].name;
    dw.sprt.sprites[dynamic].name = "renamed";
    assert(Runner_snapshotCheck(&r) != NULL);
    dw.sprt.sprites[dynamic].name = NULL;
    assert(Runner_snapshotCheck(&r) != NULL);
    dw.sprt.sprites[dynamic].name = allocatedName;
    uint8_t *rejected = NULL; size_t rejectedSize = 0;
    assert(!Runner_snapshotCapture(&r, 7, &rejected, &rejectedSize, &reason));
    assert(reason && rejected == NULL && rejectedSize == 0);
    assert(Runner_snapshotPrepare(&r, 7, bytes, size, &reason) == NULL);
    dw.sprt.sprites[dynamic].textureCount = 0; /* Deleted sprite leaves a reusable slot. */
    assert(Runner_snapshotCheck(&r) == NULL);
    dw.sprt.parsedCount = dw.sprt.count + 1;
    assert(Runner_snapshotCheck(&r) != NULL);
    dw.sprt.parsedCount = 1;
    RunnerSnapshot *prepared = Runner_snapshotPrepare(&r, 7, bytes, size, &reason);
    assert(prepared);
    Runner_snapshotFree(prepared);
    free((void *)dw.sprt.sprites[dynamic].name);
    free(dw.sprt.sprites);
    free(bytes);
    destroyFixture(&r);
}

static void testPausedCaptureEligibility(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 7, &bytes, &size, &reason));
    r.paused = true;
    assert(Runner_snapshotCheck(&r) != NULL);
    uint8_t *rejected = NULL; size_t rejectedSize = 0;
    assert(!Runner_snapshotCapture(&r, 7, &rejected, &rejectedSize, &reason));
    assert(reason && rejected == NULL && rejectedSize == 0);
    assert(Runner_snapshotPrepare(&r, 7, bytes, size - 1, &reason) == NULL);
    assert(r.paused); /* A rejected prepare leaves the target unchanged. */
    RunnerSnapshot *prepared = Runner_snapshotPrepare(&r, 7, bytes, size, &reason);
    assert(prepared);
    assert(r.paused); /* Prepare does not change the live runner. */
    Runner_snapshotApply(&r, prepared);
    Runner_snapshotFree(prepared);
    assert(!r.paused); /* Supported snapshots were captured while unpaused. */
    free(bytes);
    destroyFixture(&r);
}

static void testAllLoadedRoomsAndDefinitions(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room rooms[2]; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &rooms[0], &object);
    memset(&rooms[1], 0, sizeof(rooms[1]));
    dw.room.count = 2;
    rooms[1].payloadLoaded = true;
    rooms[1].width = 640;
    rooms[1].height = 480;
    rooms[1].speed = 30;
    rooms[1].tileCount = 1;
    rooms[1].tiles = calloc(1, sizeof(RoomTile));
    rooms[1].tiles[0].x = 17;
    dw.gen8.gms2FPS = 30;
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 45, &bytes, &size, &reason));
    rooms[0].width = 999;
    rooms[0].persistent = true; /* A room_set_persistent after capture. */
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 45, bytes, size, &reason);
    assert(s);
    rooms[1].tiles[0].x = 999;
    rooms[1].width = 123;
    rooms[1].height = 124;
    rooms[1].speed = 60;
    rooms[1].persistent = true;
    dw.gen8.gms2FPS = 60;
    Runner_snapshotApply(&r, s);
    Runner_snapshotFree(s);
    assert(rooms[1].tiles[0].x == 17);
    assert(rooms[0].width == 320 && !rooms[0].persistent);
    assert(rooms[1].width == 640 && rooms[1].height == 480);
    assert(rooms[1].speed == 30 && !rooms[1].persistent);
    assert(dw.gen8.gms2FPS == 30);
    rooms[1].width = rooms[1].height = 4194240;
    assert(Runner_snapshotCheck(&r) != NULL);
    free(bytes);
    free(rooms[1].tiles);
    destroyFixture(&r);
}

static void testRestoreDiscardsLiveSavedRoomResources(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room rooms[3]; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &rooms[0], &object);
    memset(&rooms[1], 0, sizeof(rooms[1]));
    memset(&rooms[2], 0, sizeof(rooms[2]));
    dw.room.count = 3;
    rooms[1].width = 320; rooms[1].height = 240;
    rooms[1].payloadLoaded = true; rooms[1].persistent = true;
    rooms[2].width = 320; rooms[2].height = 240;
    rooms[2].payloadLoaded = true;
    r.savedRoomStates = calloc(3, sizeof(SavedRoomState));
    assert(r.savedRoomStates);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 94, &bytes, &size, &reason));

    r.currentRoom = &rooms[2]; r.currentRoomIndex = 2;
    Instance *battle = Instance_create(100005, 0, 2, 3);
    arrput(r.instances, battle);
    hmput(r.instancesById, battle->instanceId, battle);

    SavedRoomState *state = &r.savedRoomStates[1];
    state->initialized = true;
    Instance *saved = Instance_create(100004, 0, 7, 8);
    arrput(state->instances, saved);
    hmput(r.instancesById, saved->instanceId, saved);
    TileLayerState tileLayer = {0};
    int tileDepth = 100;
    hmput(state->tileLayerMap, tileDepth, tileLayer);
    RuntimeLayer layer = {0}; layer.dynamicName = strdup("saved layer");
    arrput(state->runtimeLayers, layer);
    assert(Runner_snapshotCheck(&r) != NULL);
    uint8_t *refused = NULL; size_t refusedSize = 0;
    assert(!Runner_snapshotCapture(&r, 95, &refused, &refusedSize, &reason));
    assert(Runner_snapshotPrepare(&r, 94, bytes, size - 1, &reason) == NULL);
    assert(state->instances[0] == saved && hmlen(state->tileLayerMap) == 1);
    RunnerSnapshot *prepared = Runner_snapshotPrepare(&r, 94, bytes, size, &reason);
    assert(prepared);
    assert(state->instances[0] == saved && state->runtimeLayers[0].dynamicName);
    r.frameCount = 99;
    Runner_snapshotApply(&r, prepared); Runner_snapshotFree(prepared);
    assert(r.frameCount == 42 && r.currentRoom == &rooms[0] && !arrlen(r.instances));
    assert(!state->initialized && !state->instances && !state->tileLayerMap &&
           !state->runtimeLayers);
    int savedId = 100004;
    assert(hmgeti(r.instancesById, savedId) < 0);
    int battleId = 100005;
    assert(hmgeti(r.instancesById, battleId) < 0);
    state->initialized = true;
    assert(Runner_snapshotCheck(&r) != NULL);
    uint8_t *invalid = NULL; size_t invalidSize = 0;
    assert(!Runner_snapshotCapture(&r, 96, &invalid, &invalidSize, &reason));
    state->initialized = false;
    free(bytes); free(r.savedRoomStates);
    destroyFixture(&r);
}

static void testDormantPoolHoleRefuses(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    DsQueue freed = {.freed = true};
    arrput(r.dsQueuePool, freed);
    DsStack stack = {.freed = true}; arrput(r.dsStackPool, stack);
    DsPriority priority = {.freed = true}; arrput(r.dsPriorityPool, priority);
    DsGrid grid = {.freed = true}; arrput(r.dsGridPool, grid);
    GmlBuffer buffer = {0}; arrput(r.gmlBufferPool, buffer);
    MpGrid mp = {0}; arrput(r.mpGridPool, mp);
    ParticleSystem ps = {0}; arrput(r.particleSystemPool, ps);
    ParticleType pt = {0}; arrput(r.particleTypePool, pt);
    AudioEmitter emitter = {0}; arrput(r.audioEmitters, emitter);
    VmVertexFormat *format = NULL; arrput(r.vertexFormats, format);
    Buffer_Vertex *vertex = NULL; arrput(r.vertexBuffers, vertex);
    assert(Runner_snapshotCheck(&r) == NULL);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 53, &bytes, &size, &reason));
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 53, bytes, size, &reason);
    assert(s);
    arrput(r.dsQueuePool, freed); /* New timeline consumed id 1. */
    Runner_snapshotApply(&r, s); Runner_snapshotFree(s);
    assert(arrlen(r.dsQueuePool) == 1 && r.dsQueuePool[0].freed);
    assert(arrlen(r.dsStackPool) == 1 && r.dsStackPool[0].freed);
    assert(arrlen(r.dsPriorityPool) == 1 && r.dsPriorityPool[0].freed);
    assert(arrlen(r.dsGridPool) == 1 && r.dsGridPool[0].freed);
    assert(arrlen(r.gmlBufferPool) == 1 && !r.gmlBufferPool[0].isValid);
    assert(arrlen(r.mpGridPool) == 1 && !r.mpGridPool[0].inUse);
    assert(arrlen(r.particleSystemPool) == 1 && !r.particleSystemPool[0].used);
    assert(arrlen(r.particleTypePool) == 1 && !r.particleTypePool[0].used);
    assert(arrlen(r.audioEmitters) == 1 && !r.audioEmitters[0].active);
    assert(arrlen(r.vertexFormats) == 1 && !r.vertexFormats[0]);
    assert(arrlen(r.vertexBuffers) == 1 && !r.vertexBuffers[0]);
    size_t nextQueueId = 0;
    while (nextQueueId < (size_t)arrlen(r.dsQueuePool) && !r.dsQueuePool[nextQueueId].freed)
        nextQueueId++;
    assert(nextQueueId == 0);
    r.dsQueuePool[0].freed = false;
    assert(Runner_snapshotCheck(&r) != NULL);
    free(bytes);
    destroyFixture(&r);
}

static void testMalformedRoomCleanupAndDimensions(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 50, &bytes, &size, &reason));
    repack(&bytes, &size, invalidRoomFlag);
    assert(Runner_snapshotPrepare(&r, 50, bytes, size, &reason) == NULL);
    free(bytes); bytes = NULL;
    assert(Runner_snapshotCapture(&r, 50, &bytes, &size, &reason));
    repack(&bytes, &size, hugeRoom);
    assert(Runner_snapshotPrepare(&r, 50, bytes, size, &reason) == NULL);
    free(bytes);
    room.width = room.height = 4194240;
    assert(Runner_snapshotCheck(&r) != NULL);
    destroyFixture(&r);
}

static void testRejectedPrepareKeepsLazyRoomUnloaded(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room rooms[2]; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &rooms[0], &object);
    memset(&rooms[1], 0, sizeof(rooms[1]));
    dw.room.count = 2;
    dw.lazyLoadRooms = true;
    rooms[1].width = 320; rooms[1].height = 240;
    rooms[1].payloadLoaded = true;
    rooms[1].backgroundsFileOffset = 4;
    rooms[1].viewsFileOffset = 8;
    rooms[1].gameObjectsFileOffset = 12;
    rooms[1].tilesFileOffset = 16;
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 51, &bytes, &size, &reason));
    size_t validSize = size;
    uint8_t *validBytes = malloc(size);
    memcpy(validBytes, bytes, size);
    DataWin_freeRoomPayload(&rooms[1]);
    dw.lazyLoadFile = tmpfile();
    uint8_t emptyTables[20] = {0};
    assert(dw.lazyLoadFile && fwrite(emptyTables, 1, sizeof(emptyTables), dw.lazyLoadFile) == sizeof(emptyTables));
    fflush(dw.lazyLoadFile); dw.fileSize = sizeof(emptyTables);
    repack(&bytes, &size, invalidArrayCountAfterRooms);
    assert(Runner_snapshotPrepare(&r, 51, bytes, size, &reason) == NULL);
    assert(!rooms[1].payloadLoaded && !rooms[1].backgrounds && !rooms[1].views);
    assert(!rooms[1].tiles && !rooms[1].gameObjects);
    RunnerSnapshot *prepared = Runner_snapshotPrepare(&r, 51, validBytes, validSize, &reason);
    assert(prepared && !rooms[1].payloadLoaded);
    Runner_snapshotFree(prepared);
    assert(!rooms[1].payloadLoaded && !rooms[1].backgrounds);
    prepared = Runner_snapshotPrepare(&r, 51, validBytes, validSize, &reason);
    assert(prepared && !rooms[1].payloadLoaded);
    Runner_snapshotApply(&r, prepared); Runner_snapshotFree(prepared);
    assert(rooms[1].payloadLoaded && rooms[1].backgrounds && rooms[1].views);
    DataWin_freeRoomPayload(&rooms[1]);
    dw.roomChunkBuffer = malloc(sizeof(emptyTables));
    assert(dw.roomChunkBuffer);
    memcpy(dw.roomChunkBuffer, emptyTables, sizeof(emptyTables));
    dw.roomChunkBase = 0;
    dw.roomChunkSize = sizeof(emptyTables);
    fclose(dw.lazyLoadFile);
    dw.lazyLoadFile = NULL;
    prepared = Runner_snapshotPrepare(&r, 51, validBytes, validSize, &reason);
    assert(prepared && !rooms[1].payloadLoaded);
    Runner_snapshotApply(&r, prepared); Runner_snapshotFree(prepared);
    assert(rooms[1].payloadLoaded && rooms[1].backgrounds && rooms[1].views);
    DataWin_freeRoomPayload(&rooms[1]);
    free(dw.roomChunkBuffer);
    free(validBytes); free(bytes);
    destroyFixture(&r);
}

static void testCumulativeArrayBudget(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    GMLArray *array = GMLArray_create(&dw, 64001);
    GMLArray_set(array, 31999, RValue_makeReal(0));
    GMLArray_set(array, 63999, RValue_makeReal(0));
    Instance_setSelfVar(vm.globalScopeInstance, 10, RValue_makeArray(array));
    GMLArray_decRef(array);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 52, &bytes, &size, &reason));
    repack(&bytes, &size, oversizedArrayShape);
    assert(Runner_snapshotPrepare(&r, 52, bytes, size, &reason) == NULL);
    assert(reason && strcmp(reason, "Array element budget exceeded") == 0);
    free(bytes); destroyFixture(&r);
}

static void testOpenIniRoundTrip(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    r.currentIni = Ini_parse("[route]\nname=old\n[empty]\n");
    r.currentIniPath = strdup("snowdin.ini");
    Ini_setString(r.currentIni, "route", "name", "Snowdin\"line\nnext");
    r.currentIniDirty = true;
    r.cachedIni = Ini_parse("[cache]\nmarker=warm\n");
    r.cachedIniPath = strdup("prior.ini");
    IniFile *liveCurrent = r.currentIni, *liveCache = r.cachedIni;
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCheck(&r) == NULL);
    assert(Runner_snapshotCapture(&r, 54, &bytes, &size, &reason));
    uint8_t *invalid = malloc(size);
    memcpy(invalid, bytes, size);
    size_t invalidSize = size;
    repack(&invalid, &invalidSize, invalidIniMagic);
    assert(Runner_snapshotPrepare(&r, 54, invalid, invalidSize, &reason) == NULL);
    assert(r.currentIni == liveCurrent && r.cachedIni == liveCache && r.currentIniDirty);
    free(invalid);
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 54, bytes, size, &reason);
    assert(s && r.currentIni == liveCurrent && r.cachedIni == liveCache);
    Runner_snapshotFree(s);
    assert(r.currentIni == liveCurrent && r.cachedIni == liveCache);
    s = Runner_snapshotPrepare(&r, 54, bytes, size, &reason);
    assert(s);
    Ini_setString(r.currentIni, "route", "name", "later");
    r.currentIniDirty = false;
    Runner_snapshotApply(&r, s); Runner_snapshotFree(s);
    assert(r.currentIni && r.cachedIni && r.currentIni != r.cachedIni);
    assert(strcmp(r.currentIniPath, "snowdin.ini") == 0);
    assert(strcmp(r.cachedIniPath, "prior.ini") == 0);
    assert(r.currentIniDirty);
    assert(strcmp(Ini_getString(r.currentIni, "route", "name"), "Snowdin\"line\nnext") == 0);
    assert(Ini_hasSection(r.currentIni, "empty"));
    assert(strcmp(Ini_getString(r.cachedIni, "cache", "marker"), "warm") == 0);
    free(bytes);
    Ini_free(r.currentIni); free(r.currentIniPath);
    Ini_free(r.cachedIni); free(r.cachedIniPath);
    r.currentIni = Ini_parse("[string]\nkind=memory\n");
    r.currentIniPath = NULL; r.currentIniDirty = true;
    r.cachedIni = Ini_parse("[old]\nkind=string-cache\n");
    r.cachedIniPath = NULL;
    bytes = NULL; size = 0;
    assert(Runner_snapshotCapture(&r, 55, &bytes, &size, &reason));
    s = Runner_snapshotPrepare(&r, 55, bytes, size, &reason);
    assert(s); Runner_snapshotApply(&r, s); Runner_snapshotFree(s);
    assert(r.currentIni && r.currentIniPath == NULL && r.currentIniDirty);
    assert(r.cachedIni && r.cachedIniPath == NULL);
    assert(strcmp(Ini_getString(r.currentIni, "string", "kind"), "memory") == 0);
    assert(strcmp(Ini_getString(r.cachedIni, "old", "kind"), "string-cache") == 0);
    free(bytes); destroyFixture(&r);
}

static void testSharedIniEntryBudget(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    r.currentIni = calloc(1, sizeof(*r.currentIni));
    r.currentIni->sections = calloc(65536, sizeof(IniSection));
    r.currentIni->capacity = 65536;
    for (int i = 0; i < 65536; i++)
        r.currentIni->sections[i].name = strdup("s");
    r.currentIni->count = 65535;
    r.cachedIni = Ini_parse("[cached]\n");
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 57, &bytes, &size, &reason));
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 57, bytes, size, &reason);
    assert(s); Runner_snapshotFree(s); free(bytes);
    r.currentIni->count = 65536;
    bytes = NULL; size = 0; reason = NULL;
    assert(!Runner_snapshotCapture(&r, 57, &bytes, &size, &reason));
    assert(reason && strcmp(reason, "INI entry budget exceeded") == 0);
    destroyFixture(&r);
}

static void testOlderCoreV3Trailers(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 56, &bytes, &size, &reason));
    uint32_t rawLen;
    uint8_t *raw = decodeRaw(bytes, size, &rawLen);
    encodeRaw(&bytes, &size, raw, rawLen, 3);
    free(raw);
    removeIniAndMaybeDormantTail(&bytes, &size, 8);
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 56, bytes, size, &reason);
    assert(s); Runner_snapshotFree(s);
    removeIniAndMaybeDormantTail(&bytes, &size, 44);
    s = Runner_snapshotPrepare(&r, 56, bytes, size, &reason);
    assert(s); Runner_snapshotFree(s);
    free(bytes); destroyFixture(&r);
}

static void testZstdV4FrameValidation(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    assert(sizeof(r.score) == sizeof(uint32_t));
    const uint32_t scoreBits = 0x3eaaaaabu, livesBits = 0x80000000u;
    memcpy(&r.score, &scoreBits, sizeof(scoreBits));
    memcpy(&r.lives, &livesBits, sizeof(livesBits));
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 58, &bytes, &size, &reason));
    assert(le32(bytes + 4) == 5);
    uint32_t rawLen;
    uint8_t *raw = decodeRaw(bytes, size, &rawLen);
    encodeRaw(&bytes, &size, raw, rawLen, 4);
    assert(le32(bytes + 4) == 4);
    assert(ZSTD_getFrameContentSize(bytes + 32, size - 32) == le32(bytes + 24));
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 58, bytes, size, &reason);
    assert(s);
    r.score = 0; r.lives = 1;
    Runner_snapshotApply(&r, s); Runner_snapshotFree(s);
    uint32_t actualScore, actualLives;
    memcpy(&actualScore, &r.score, sizeof(actualScore));
    memcpy(&actualLives, &r.lives, sizeof(actualLives));
    assert(actualScore == scoreBits && actualLives == livesBits);
    assert(Runner_snapshotPrepare(&r, 58, bytes, size - 1, &reason) == NULL);
    uint8_t *bad = malloc(size + 1);
    /* A truncated frame with a repaired outer checksum must reach the codec. */
    memcpy(bad, bytes, size - 1);
    write32(bad + 16, (uint32_t)(size - 1 - 24));
    write32(bad + 20, hashBytes(bad + 24, size - 1 - 24));
    assert(Runner_snapshotPrepare(&r, 58, bad, size - 1, &reason) == NULL);
    memcpy(bad, bytes, size);
    bad[size] = 0;
    write32(bad + 16, (uint32_t)(size + 1 - 24));
    write32(bad + 20, hashBytes(bad + 24, size + 1 - 24));
    assert(Runner_snapshotPrepare(&r, 58, bad, size + 1, &reason) == NULL);
    memcpy(bad, bytes, size);
    bad[32] ^= 0x80;
    write32(bad + 20, hashBytes(bad + 24, size - 24));
    assert(Runner_snapshotPrepare(&r, 58, bad, size, &reason) == NULL);
    ZSTD_CCtx *ctx = ZSTD_createCCtx();
    assert(ctx);
    assert(!ZSTD_isError(ZSTD_CCtx_setParameter(ctx, ZSTD_c_contentSizeFlag, 0)));
    size_t cap = ZSTD_compressBound(rawLen);
    uint8_t *unknown = malloc(cap + 32);
    memcpy(unknown, bytes, 32);
    size_t compressed = ZSTD_compress2(ctx, unknown + 32, cap, raw, rawLen);
    assert(!ZSTD_isError(compressed));
    assert(ZSTD_getFrameContentSize(unknown + 32, compressed) == ZSTD_CONTENTSIZE_UNKNOWN);
    write32(unknown + 16, (uint32_t)compressed + 8);
    write32(unknown + 20, hashBytes(unknown + 24, compressed + 8));
    assert(Runner_snapshotPrepare(&r, 58, unknown, compressed + 32, &reason) == NULL);
    free(unknown); ZSTD_freeCCtx(ctx); free(raw);
    free(bad); free(bytes);
    destroyFixture(&r);
}

static void testFreeCameraRefuses(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    r.freeCamZoom = 1.0f;
    r.freeCamPanX = 0.25f;
    assert(Runner_snapshotCheck(&r) != NULL);
    destroyFixture(&r);
}

static void testRoomLoadedAfterCaptureIsDiscarded(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room rooms[2]; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &rooms[0], &object);
    memset(&rooms[1], 0, sizeof(rooms[1]));
    dw.room.count = 2;
    dw.lazyLoadRooms = true;
    rooms[1].width = 600;
    rooms[1].height = 240;
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 46, &bytes, &size, &reason));
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 46, bytes, size, &reason);
    assert(s);
    rooms[1].payloadLoaded = true;
    rooms[1].tileCount = 1;
    rooms[1].tiles = calloc(1, sizeof(RoomTile));
    rooms[1].tiles[0].x = 999;
    rooms[1].width = 900;
    Runner_snapshotApply(&r, s);
    Runner_snapshotFree(s);
    assert(!rooms[1].payloadLoaded && rooms[1].tileCount == 0 && rooms[1].tiles == NULL);
    assert(rooms[1].width == 600);
    free(bytes);
    destroyFixture(&r);
}

static void testOrphanedSharedArrayOwner(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    Instance *first = Instance_create(100001, 0, 0, 0);
    Instance *second = Instance_create(100002, 0, 0, 0);
    arrput(r.instances, first); arrput(r.instances, second);
    GMLArray *shared = GMLArray_create(&dw, 1);
    GMLArray_set(shared, 0, RValue_makeReal(42));
    Instance_setSelfVar(first, 10, RValue_makeArray(shared));
    Instance_setSelfVar(second, 11, RValue_makeArray(shared));
    RValue deadOwnerSlot = RValue_makeUndefined();
    shared->owner = &deadOwnerSlot; /* A former owner outside every live root. */
    GMLArray_decRef(shared);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 47, &bytes, &size, &reason));
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 47, bytes, size, &reason);
    assert(s);
    Runner_snapshotApply(&r, s);
    Runner_snapshotFree(s);
    RValue *a = IntRValueHashMap_findSlot(&r.instances[0]->selfVars, 10);
    RValue *b = IntRValueHashMap_findSlot(&r.instances[1]->selfVars, 11);
    assert(a->array == b->array && a->array->refCount == 2);
    assert(a->array->owner && a->array->owner != a && a->array->owner != b);
    free(bytes);
    destroyFixture(&r);
}

static void testReusedSharedArrayOwnerSlot(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    Instance *first = Instance_create(100001, 0, 0, 0);
    Instance *second = Instance_create(100002, 0, 0, 0);
    arrput(r.instances, first); arrput(r.instances, second);
    GMLArray *shared = GMLArray_create(&dw, 1);
    GMLArray_set(shared, 0, RValue_makeReal(42));
    Instance_setSelfVar(first, 10, RValue_makeArray(shared));
    Instance_setSelfVar(first, 12, RValue_makeReal(7));
    Instance_setSelfVar(second, 11, RValue_makeArray(shared));
    /* The former owner address has been reused by a live, unrelated value. */
    RValue *reused = IntRValueHashMap_findSlot(&first->selfVars, 12);
    shared->owner = reused;
    GMLArray_decRef(shared);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 49, &bytes, &size, &reason));
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 49, bytes, size, &reason);
    assert(s);
    Runner_snapshotApply(&r, s); Runner_snapshotFree(s);
    RValue *a = IntRValueHashMap_findSlot(&r.instances[0]->selfVars, 10);
    RValue *b = IntRValueHashMap_findSlot(&r.instances[1]->selfVars, 11);
    RValue *reusedAfter = IntRValueHashMap_findSlot(&r.instances[0]->selfVars, 12);
    assert(a->array == b->array && a->array->refCount == 2);
    assert(a->array->owner && a->array->owner != a && a->array->owner != b);
    assert(a->array->owner != reusedAfter);
    free(bytes);
    destroyFixture(&r);
}

static void testLargeCompressibleMap(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    DsMapEntry *map = NULL;
    char textValue[1025]; memset(textValue, 'x', 1024); textValue[1024] = 0;
    for (int i = 0; i < 1200; i++) {
        char key[32]; snprintf(key, sizeof(key), "key%d", i);
        shput(map, strdup(key), RValue_makeOwnedString(strdup(textValue)));
    }
    arrput(r.dsMapPool, map);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 48, &bytes, &size, &reason));
    assert(size < 1024 * 1024);
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 48, bytes, size, &reason);
    assert(s);
    Runner_snapshotApply(&r, s); Runner_snapshotFree(s);
    assert(shlen(r.dsMapPool[0]) == 1200);
    assert(strlen(shget(r.dsMapPool[0], "key1199").string) == 1024);
    free(bytes);
    destroyFixture(&r);
}

static void testStaticStringReferences(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    char staticText[129]; memset(staticText, 'q', 128); staticText[128] = 0;
    const char *strings[] = {staticText, "other"};
    dw.strg.strings = strings; dw.strg.count = 2;
    DsList list = {0};
    for (int i = 0; i < 64; i++)
        arrput(list.items, RValue_makeOwnedString(strdup(staticText)));
    arrput(list.items, RValue_makeOwnedString(strdup("dynamic-value")));
    arrput(r.dsListPool, list);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 91, &bytes, &size, &reason));
    assert(le32(bytes + 4) == 5);
    uint32_t rawLen;
    uint8_t *raw = decodeRaw(bytes, size, &rawLen);
    dw.strg.count = 0;
    uint8_t *literal = NULL; size_t literalSize = 0;
    assert(Runner_snapshotCapture(&r, 91, &literal, &literalSize, &reason));
    assert(le32(literal + 24) - rawLen >= 64u * 128u);
    free(literal);
    dw.strg.count = 2;
    size_t ref = rawLen;
    for (size_t i = 4; i + 4 <= rawLen; i++)
        if ((le32(raw + i - 4) & 255u) == RVALUE_STRING &&
            le32(raw + i) == 0x80000000u) {
            ref = i; break;
        }
    assert(ref < rawLen);
    assert(Runner_snapshotPrepare(&r, 91, bytes, size - 1, &reason) == NULL);
    RunnerSnapshot *s = Runner_snapshotPrepare(&r, 91, bytes, size, &reason);
    assert(s);
    Runner_snapshotApply(&r, s); Runner_snapshotFree(s);
    assert(arrlen(r.dsListPool[0].items) == 65);
    for (int i = 0; i < 64; i++) {
        RValue *value = &r.dsListPool[0].items[i];
        assert(value->type == RVALUE_STRING && value->ownsReference);
        assert(strcmp(value->string, staticText) == 0 && value->string != staticText);
        if (i) assert(value->string != r.dsListPool[0].items[i - 1].string);
    }
    assert(strcmp(r.dsListPool[0].items[64].string, "dynamic-value") == 0);
    strings[0] = NULL;
    assert(Runner_snapshotPrepare(&r, 91, bytes, size, &reason) == NULL);
    strings[0] = staticText;
    char *oversized = malloc(65537); assert(oversized);
    memset(oversized, 'x', 65536); oversized[65536] = 0;
    strings[0] = oversized;
    assert(Runner_snapshotPrepare(&r, 91, bytes, size, &reason) == NULL);
    strings[0] = staticText;
    free(oversized);
    dw.strg.count = 131073;
    assert(Runner_snapshotPrepare(&r, 91, bytes, size, &reason) == NULL);
    uint8_t *tooMany = NULL; size_t tooManySize = 0;
    assert(!Runner_snapshotCapture(&r, 91, &tooMany, &tooManySize, &reason));
    dw.strg.count = 2;
    uint8_t *bad = malloc(size); assert(bad); memcpy(bad, bytes, size);
    write32(raw + ref, 0x80000002u);
    size_t badSize = size;
    encodeRaw(&bad, &badSize, raw, rawLen, 5);
    assert(Runner_snapshotPrepare(&r, 91, bad, badSize, &reason) == NULL);
    free(bad); free(raw); free(bytes);
    destroyFixture(&r);
}

static void testStaticStringExpansionBudget(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    char *text = malloc(65537); assert(text);
    memset(text, 'x', 65536); text[65536] = 0;
    const char *strings[] = {text};
    dw.strg.strings = strings; dw.strg.count = 1;
    DsList list = {0};
    for (int i = 0; i < 64; i++)
        arrput(list.items, RValue_makeOwnedString(strdup(text)));
    arrput(r.dsListPool, list);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(!Runner_snapshotCapture(&r, 92, &bytes, &size, &reason));
    assert(bytes == NULL && size == 0);
    destroyFixture(&r);
    free(text);
}

static void testDecoderStaticStringExpansionBudget(void) {
    Runner r; DataWin dw; VMContext vm; Renderer renderer; RendererVtable vtable;
    Room room; GameObject object;
    fixture(&r, &dw, &vm, &renderer, &vtable, &room, &object);
    char *longText = malloc(65537); assert(longText);
    memset(longText, 'x', 65536); longText[65536] = 0;
    const char *strings[] = {"short", longText};
    dw.strg.strings = strings; dw.strg.count = 2;
    DsList list = {0};
    const char *liveStrings[64];
    for (int i = 0; i < 64; i++) {
        RValue value = RValue_makeOwnedString(strdup("short"));
        liveStrings[i] = value.string;
        arrput(list.items, value);
    }
    arrput(r.dsListPool, list);
    uint8_t *bytes = NULL; size_t size = 0; const char *reason = NULL;
    assert(Runner_snapshotCapture(&r, 93, &bytes, &size, &reason));
    uint32_t rawLen;
    uint8_t *raw = decodeRaw(bytes, size, &rawLen);
    size_t run = rawLen;
    uint32_t runs = 0;
    for (size_t at = 0; at + 64u * 8u <= rawLen; at++) {
        int j = 0;
        for (; j < 64; j++) {
            size_t value = at + (size_t)j * 8u;
            if ((le32(raw + value) & 255u) != RVALUE_STRING ||
                le32(raw + value + 4) != 0x80000000u) break;
        }
        if (j == 64) { run = at; runs++; }
    }
    assert(runs == 1); /* Exactly the 64 consecutive list values. */
    for (int j = 0; j < 64; j++)
        write32(raw + run + (size_t)j * 8u + 4u, 0x80000001u);
    encodeRaw(&bytes, &size, raw, rawLen, 5);
    assert(size < 1024u * 1024u);
    assert(Runner_snapshotPrepare(&r, 93, bytes, size, &reason) == NULL);
    assert(r.frameCount == 42 && arrlen(r.dsListPool[0].items) == 64);
    for (int i = 0; i < 64; i++) {
        assert(r.dsListPool[0].items[i].string == liveStrings[i]);
        assert(strcmp(liveStrings[i], "short") == 0);
    }
    longText[65535] = 0; /* 64 * (65535 + NUL) is exactly 4 MiB. */
    RunnerSnapshot *atLimit = Runner_snapshotPrepare(&r, 93, bytes, size, &reason);
    assert(atLimit);
    Runner_snapshotFree(atLimit);
    free(raw); free(bytes);
    destroyFixture(&r);
    free(longText);
}

int main(void) {
    testRestoreDiscardsLiveSavedRoomResources();
    testStaticStringReferences();
    testStaticStringExpansionBudget();
    testDecoderStaticStringExpansionBudget();
    testZstdV4FrameValidation();
    testRoundTrip();
    testRejectsBadInputAndLiveResources();
    testPausedCaptureEligibility();
    testDynamicSpriteCaptureEligibility();
    testBlockersListsEveryRefusal();
    testBlockersWithInvalidSpriteRegistry();
    testAllLoadedRoomsAndDefinitions();
    testDormantPoolHoleRefuses();
    testMalformedRoomCleanupAndDimensions();
    testRejectedPrepareKeepsLazyRoomUnloaded();
    testCumulativeArrayBudget();
    testOpenIniRoundTrip();
    testSharedIniEntryBudget();
    testOlderCoreV3Trailers();
    testFreeCameraRefuses();
    testRoomLoadedAfterCaptureIsDiscarded();
    testOrphanedSharedArrayOwner();
    testReusedSharedArrayOwnerSlot();
    testLargeCompressibleMap();
    return 0;
}
