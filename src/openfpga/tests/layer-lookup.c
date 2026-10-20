/* Exercise the real lookup and mutation paths. Count executed search-loop
 * bodies in this translation unit, without a production profiling hook. */
#include "../../runner.h"
#include "../../utils.h"
static unsigned loopBodies;
#undef repeat
#define repeat(n, it) for (TYPEOF(n) it = 0; it < (n) && (++loopBodies, 1); ++it)
#include "../../runner.c"
#undef repeat
#define repeat(n, it) for (TYPEOF(n) it = 0; it < (n); ++it)
#include "../../vm_builtins.h"
#include "../../runner_snapshot.h"
#include "../../sw/sw_renderer.h"
#include "../../noop_audio_system.h"
#include <assert.h>
#include <stdio.h>

#ifdef LAYER_LOOKUP_TEST_BASELINE
#define Runner_invalidateLayerElementLookup(r) ((void)(r))
#endif

static unsigned checks, workFailures;
static bool behaviorOnly;
typedef struct { Runner r; DataWin data; VMContext vm; Room room; RoomView views[MAX_VIEWS]; RoomBackground backgrounds[8]; } Fixture;

static Fixture *fixture(void) {
    Fixture *f = (Fixture *)calloc(1, sizeof(*f));
    assert(f);
#ifdef GAME_DELTARUNE
    f->data.gen8.wadVersion = 17;
#else
    f->data.gen8.wadVersion = 16;
#endif
    f->data.detectedFormat.major = 2;
    f->room.views = f->views;
    f->room.backgrounds = f->backgrounds;
    f->r.dataWin = &f->data;
    f->r.vmContext = &f->vm;
    f->r.currentRoom = &f->room;
    f->r.nextLayerId = 10000;
    f->vm.runner = &f->r;
    f->vm.dataWin = &f->data;
    VMBuiltins_registerAll(&f->vm);
    return f;
}
static void dispose(Fixture *f) {
    freeRuntimeLayersArray(&f->r.runtimeLayers);
    hmfree(f->r.instancesById);
    shfree(f->vm.builtinMap);
    free(f);
}
static void addLayer(Fixture *f, unsigned id) {
    RuntimeLayer layer = {0};
    layer.id = id;
    layer.beginScript = layer.endScript = layer.shaderIndex = -1;
    Runner_invalidateLayerElementLookup(&f->r);
    arrput(f->r.runtimeLayers, layer);
}
static void addElement(Fixture *f, int layer, unsigned id) {
    RuntimeLayerElement el = {0};
    el.id = id;
    Runner_invalidateLayerElementLookup(&f->r);
    arrput(f->r.runtimeLayers[layer].elements, el);
}
static void expect(Fixture *f, int id, int layer, int element) {
    RuntimeLayer *owner = (RuntimeLayer *)f; /* must be overwritten on misses */
    RuntimeLayerElement *got = Runner_findLayerElementById(&f->r, id, &owner);
    if (got != (layer < 0 ? NULL : &f->r.runtimeLayers[layer].elements[element]))
        fprintf(stderr, "lookup %d expected %d/%d, owner %d\n", id, layer, element, owner ? (int)owner->id : -1);
    assert(got == (layer < 0 ? NULL : &f->r.runtimeLayers[layer].elements[element]));
    assert(owner == (layer < 0 ? NULL : &f->r.runtimeLayers[layer]));
    assert(Runner_findLayerElementById(&f->r, id, NULL) == got);
    ++checks;
}
static RValue call(Fixture *f, const char *name, int count, RValue *args) {
    BuiltinFunc fn = VM_findBuiltin(&f->vm, name);
    assert(fn);
    return fn(&f->vm, args, count);
}
static void testSearch(void) {
    Fixture *f = fixture();
    expect(f, 42, -1, -1);
    for (int i = 0; i < 6; ++i) addLayer(f, i + 1);
    for (int i = 0; i < 250; ++i) addElement(f, 5, i + 1);
    expect(f, 249, 5, 248);
    loopBodies = 0;
    for (int i = 0; i < 20; ++i)
        assert(Runner_findLayerElementById(&f->r, 249, NULL) == &f->r.runtimeLayers[5].elements[248]);
    printf("20 repeated hits: %u search-loop bodies\n", loopBodies);
    if (!behaviorOnly && loopBodies > 20) ++workFailures;
    expect(f, 1, 5, 0);
    expect(f, -1, -1, -1);
    expect(f, 250, 5, 249);
    addElement(f, 0, 250); /* Earlier duplicate must win despite intact cached slot. */
    expect(f, 250, 0, 0);
    addElement(f, 0, 250);
    expect(f, 250, 0, 0);
    RuntimeLayerElement front = {0}; front.id = 250;
    Runner_invalidateLayerElementLookup(&f->r);
    arrins(f->r.runtimeLayers[0].elements, 0, front);
    expect(f, 250, 0, 0);
    Runner_invalidateLayerElementLookup(&f->r);
    arrdel(f->r.runtimeLayers[0].elements, 0);
    expect(f, 250, 0, 0);
    /* Relocate both live arrays while retaining order; no cached pointer may be read. */
    RuntimeLayerElement *oldElements = f->r.runtimeLayers[0].elements, *elements = NULL;
    for (int i = 0; i < arrlen(oldElements); ++i) arrput(elements, oldElements[i]);
    f->r.runtimeLayers[0].elements = elements;
    arrfree(oldElements);
    RuntimeLayer *oldLayers = f->r.runtimeLayers, *layers = NULL;
    for (int i = 0; i < arrlen(oldLayers); ++i) arrput(layers, oldLayers[i]);
    f->r.runtimeLayers = layers;
    arrfree(oldLayers);
    expect(f, 250, 0, 0);
#ifndef LAYER_LOOKUP_TEST_BASELINE
    f->r.layerElementLookupLayer = (size_t)-1;
    expect(f, 250, 0, 0);
    f->r.layerElementLookupElement = (size_t)-1;
    expect(f, 250, 0, 0);
#endif
    /* A detached/shrunken array or changed ID is a safely rejected hint. */
    arrsetlen(f->r.runtimeLayers[0].elements, 0);
    expect(f, 250, 5, 249);
    f->r.runtimeLayers[5].elements[249].id = 251;
    expect(f, 250, -1, -1);
    expect(f, 251, 5, 249);
    dispose(f);
}
static void testSequentialWalk(void) {
    Fixture *f = fixture();
    for (int i = 0; i < 3; ++i) addLayer(f, i + 1);
    for (int i = 0; i < 20; ++i) addElement(f, 0, 500 + i);
    for (int i = 0; i < 240; ++i) addElement(f, 2, 1000 + i);
    /* Four lookups per tile, as tile_get_x/x/get_y/y plus type do in a shift. */
    loopBodies = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < 240; ++i)
            for (int k = 0; k < 5; ++k) expect(f, 1000 + i, 2, i);
    printf("2 ascending 240-tile walks: %u search-loop bodies\n", loopBodies);
    /* Each walk pays one scan for its first tile and one prefix pass for its
     * second (about 25 bodies each); the other 238 tiles take the next slot. */
    if (!behaviorOnly && loopBodies > 4 * 30) ++workFailures;
    /* An earlier duplicate of the next slot's ID must still win. */
    addElement(f, 0, 1100);
    expect(f, 1099, 2, 99);
    expect(f, 1100, 0, 20);
    expect(f, 1099, 2, 99);
    expect(f, 1100, 0, 20);
    /* Same, with the duplicate's ID already above the recorded prefix. */
    expect(f, 1098, 2, 98);
    expect(f, 1099, 2, 99);
    expect(f, 1100, 0, 20);
    /* Descending IDs fall back to the full first-match search. */
    Fixture *g = fixture();
    addLayer(g, 1);
    for (int i = 0; i < 30; ++i) addElement(g, 0, 900 - i);
    addElement(g, 0, 895); /* duplicate later in the walk */
    for (int i = 0; i < 30; ++i) expect(g, 900 - i, 0, i);
    expect(g, 895, 0, 5);
    expect(g, 870, -1, -1);
    dispose(g);
    /* Ascending walk crossing an in-place ID change at the hinted slot. */
    expect(f, 1000, 2, 0);
    expect(f, 1001, 2, 1);
    f->r.runtimeLayers[2].elements[1].id = 5000;
    expect(f, 1002, 2, 2);
    expect(f, 1001, -1, -1);
    dispose(f);
}
static void testBuiltins(void) {
    Fixture *f = fixture();
    addLayer(f, 1); addLayer(f, 2);
    addElement(f, 1, 10000);
    expect(f, 10000, 1, 0);
    /* Force an earlier duplicate via the real creator, also exercising ID wrap/collision semantics. */
    RValue args[8] = {RValue_makeReal(1), RValue_makeReal(0), RValue_makeReal(0), RValue_makeReal(0),
                     RValue_makeReal(0), RValue_makeReal(0), RValue_makeReal(8), RValue_makeReal(8)};
    const char *creators[] = {"layer_background_create", "layer_sprite_create", "layer_tile_create"};
    const char *destroyers[] = {"layer_background_destroy", "layer_sprite_destroy", "layer_tile_destroy"};
    int argc[] = {2, 4, 8};
    for (int i = 0; i < 3; ++i) {
        f->r.nextLayerId = 10000;
        RValue id = call(f, creators[i], argc[i], args);
        assert(RValue_toInt32(id) == 10000);
        expect(f, 10000, 0, 0);
        RValue one[] = {id};
        call(f, destroyers[i], 1, one);
        expect(f, 10000, 1, 0);
    }
    RValue move[] = {RValue_makeReal(10000), RValue_makeReal(1)};
    call(f, "layer_element_move", 2, move);
    expect(f, 10000, 0, 0);
    RValue destroy[] = {RValue_makeReal(1)};
    call(f, "layer_destroy", 1, destroy);
    expect(f, 10000, -1, -1);
    addElement(f, 0, 77);
    expect(f, 77, 0, 0);
    RValue depth[] = {RValue_makeReal(10)};
    for (int i = 0; i < 40; ++i) call(f, "layer_create", 1, depth);
    expect(f, 77, 0, 0);
    dispose(f);
}
static void testInstances(void) {
    Fixture *f = fixture();
    addLayer(f, 1); addLayer(f, 2);
    addElement(f, 1, 10000);
    expect(f, 10000, 1, 0);
    Runner_addInstanceLayerElement(&f->r, 1, 44);
    expect(f, 10000, 0, 0);
    Runner_removeInstanceLayerElement(&f->r, 44);
    expect(f, 10000, 1, 0);
    Instance inst = {0}; inst.instanceId = 55; inst.layer = 1;
    Runner_addInstanceLayerElement(&f->r, 1, 55);
    expect(f, 10001, 0, 0);
    Runner_moveInstanceToDepthLayer(&f->r, &inst, 22);
    expect(f, 10001, -1, -1);
    expect(f, 10003, 2, 0);
    /* Merge a second automatic-depth layer into the first. */
    Instance other = {0}; other.instanceId = 66; other.layer = -1;
    Runner_moveInstanceToDepthLayer(&f->r, &other, 33);
    expect(f, 10005, 3, 0);
    Runner_moveInstanceToDepthLayer(&f->r, &inst, 33);
    expect(f, 10005, 2, 1);
    expect(f, 10003, 2, 0);
    dispose(f);
}
static unsigned destroyCallbacks;
static bool warmDuringDestroy(VMContext *vm) {
    RuntimeLayer *owner = NULL;
    assert(Runner_findLayerElementById(vm->runner, 77, &owner));
    assert(owner == &vm->runner->runtimeLayers[1]);
    ++destroyCallbacks;
    return true;
}
static void testDestroyCallback(void) {
    Fixture *f = fixture();
    f->r.pendingRoom = -1;
    GameObject object = {0}; object.present = true; object.parentId = -1;
    EventAction action = {0}; action.codeId = 0;
    ObjectEvent event = {0}; event.actionCount = 1; event.actions = &action;
    object.eventLists[EVENT_DESTROY].eventCount = 1;
    object.eventLists[EVENT_DESTROY].events = &event;
    f->data.objt.count = 1; f->data.objt.objects = &object;
    CodeEntry code = {0}; code.name = "layer destroy callback";
    f->data.code.count = 1; f->data.code.entries = &code;
    VMNativeCode native = warmDuringDestroy; f->vm.nativeCode = &native;
    EventSlotMap_build(&f->r.eventSlotMap, &f->data);
    ResolvedEventTable_build(&f->r.eventTable, &f->data, &f->r.eventSlotMap);
    addLayer(f, 1); addLayer(f, 2); addLayer(f, 3);
    addElement(f, 1, 77); addElement(f, 2, 77);
    Instance *inst = Instance_create(123, 0, 0, 0);
    hmput(f->r.instancesById, inst->instanceId, inst);
    Runner_addInstanceLayerElement(&f->r, 1, 123);
    RValue id = RValue_makeReal(1);
    call(f, "layer_destroy", 1, &id);
    assert(destroyCallbacks == 1);
    /* The callback's cached slot is still in bounds and still has ID 77,
     * but the first surviving duplicate has shifted to index zero. */
    expect(f, 77, 0, 0);
    Instance_free(inst);
    ResolvedEventTable_free(&f->r.eventTable);
    EventSlotMap_destroy(&f->r.eventSlotMap);
    dispose(f);
}
static void testRoomLifecycle(void) {
    Fixture *f = fixture();
    f->data.room.count = 1; f->data.room.rooms = &f->room;
    f->room.name = "lookup-room"; f->room.payloadLoaded = true;
    f->room.width = f->room.height = 64; f->room.creationCodeId = -1;
    f->r.savedRoomStates = (SavedRoomState *)calloc(1, sizeof(SavedRoomState));
    addLayer(f, 1); addElement(f, 0, 50);
    addLayer(f, 2); addElement(f, 1, 50); expect(f, 50, 0, 0);
    persistRoomState(&f->r, 0);
    expect(f, 50, -1, -1);
    /* Populate another live array before reinstalling the persistent room. */
    addLayer(f, 3); addElement(f, 0, 60);
    addLayer(f, 4); addElement(f, 1, 50); expect(f, 50, 1, 0);
    f->room.persistent = true;
    initRoom(&f->r, 0);
    expect(f, 50, 0, 0); expect(f, 60, -1, -1);
    f->room.persistent = false;
    expect(f, 50, 0, 0);
    initRoom(&f->r, 0);
    expect(f, 50, -1, -1);
    addLayer(f, 3); addElement(f, 0, 70); expect(f, 70, 0, 0);
    f->r.audioSystem = (AudioSystem *)NoopAudioSystem_create();
    Runner_reset(&f->r);
    expect(f, 70, -1, -1);
    f->r.audioSystem->vtable->destroy(f->r.audioSystem);
    cleanupState(&f->r);
    Instance_free(f->vm.globalScopeInstance);
    dispose(f);
}
static void testSnapshot(void) {
    Fixture *f = fixture();
    f->data.room.count = 1; f->data.room.rooms = &f->room;
    f->room.name = "lookup-room"; f->room.payloadLoaded = true;
    f->room.width = f->room.height = 64; f->room.creationCodeId = -1;
    f->r.pendingRoom = -1;
    /* Clamp captured elapsed time to zero for a deterministic byte comparison. */
    f->r.gameStartTime = (uint64_t)-1;
    f->r.renderer = SWRenderer_create();
    f->vm.globalScopeInstance = Instance_create(0, STRUCT_OBJECT_INDEX, 0, 0);
    addLayer(f, 1); addLayer(f, 2);
    RValue args[] = {RValue_makeReal(2), RValue_makeReal(-1)};
    RValue id = call(f, "layer_background_create", 2, args);
    int elementId = RValue_toInt32(id);
    expect(f, elementId, 1, 0);
    f->r.nextLayerId = elementId;
    args[0] = RValue_makeReal(1);
    call(f, "layer_background_create", 2, args);
    expect(f, elementId, 0, 0);
    uint8_t *before = NULL, *after = NULL; size_t beforeSize = 0, afterSize = 0;
    const char *reason = NULL;
    assert(Runner_snapshotCapture(&f->r, 7, &before, &beforeSize, &reason));
    Runner_invalidateLayerElementLookup(&f->r);
    assert(Runner_snapshotCapture(&f->r, 7, &after, &afterSize, &reason));
    assert(beforeSize == afterSize && !memcmp(before, after, beforeSize));
    free(after);
    /* Remove the first duplicate: the warm later slot still matches after
     * restore, so bounds/ID checks alone cannot preserve first-match order. */
    call(f, "layer_background_destroy", 1, &id);
    expect(f, elementId, 1, 0);
    RunnerSnapshot *snapshot = Runner_snapshotPrepare(&f->r, 7, before, beforeSize, &reason);
    if (!snapshot) fprintf(stderr, "snapshot prepare: %s\n", reason);
    assert(snapshot);
    Runner_snapshotApply(&f->r, snapshot);
    expect(f, elementId, 0, 0);
    Runner_snapshotFree(snapshot);
    free(before);
    cleanupState(&f->r);
    Instance_free(f->vm.globalScopeInstance);
    f->r.renderer->vtable->destroy(f->r.renderer);
    dispose(f);
}
int main(int argc, char **argv) {
    behaviorOnly = argc > 1 && !strcmp(argv[1], "--behavior-only");
    testSearch(); testSequentialWalk(); testBuiltins(); testInstances(); testDestroyCallback(); testRoomLifecycle(); testSnapshot();
    printf("%u behavior checks, %u work-bound failures\n", checks, workFailures);
    return workFailures ? 1 : 0;
}
