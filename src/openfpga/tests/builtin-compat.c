/* Asset-free checks through the real builtin registry. */
#include "vm.h"
#include "vm_builtins.h"
#include "runner.h"
#include "stb_ds.h"
#include "of_game.h"
#include "runner_keyboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { float gain, gainTarget, pitch; uint32_t fade; bool active, paused; } TestVoice;
typedef struct {
    AudioSystem base;
    TestVoice voices[4];
    int count;
    float velocity[3];
    bool paused;
} TestAudio;

static int32_t playSound(AudioSystem *audio, int32_t sound, int32_t priority, bool loop) {
    TestAudio *test = (TestAudio *)audio;
    (void)sound; (void)priority; (void)loop;
    assert(test->count < 4);
    test->voices[test->count] = (TestVoice){.gain = 0.5f, .pitch = 2, .active = true};
    return 100000 + test->count++;
}

static TestVoice *voice(AudioSystem *audio, int32_t id) {
    TestAudio *test = (TestAudio *)audio;
    assert(id >= 100000 && id < 100000 + test->count);
    return &test->voices[id - 100000];
}

static void setGain(AudioSystem *audio, int32_t id, float gain, uint32_t fade) {
    if (id == 0) {
        TestAudio *test = (TestAudio *)audio;
        for (int i = 0; i < test->count; i++) {
            if (!fade) test->voices[i].gain = gain;
            test->voices[i].gainTarget = gain; test->voices[i].fade = fade;
        }
        return;
    }
    if (!fade) voice(audio, id)->gain = gain;
    voice(audio, id)->gainTarget = gain; voice(audio, id)->fade = fade;
}
static float getGain(AudioSystem *audio, int32_t id) { return voice(audio, id)->gain; }
static void setPitch(AudioSystem *audio, int32_t id, float pitch) {
    if (id == 0) {
        TestAudio *test = (TestAudio *)audio;
        for (int i = 0; i < test->count; i++) test->voices[i].pitch = pitch;
        return;
    }
    voice(audio, id)->pitch = pitch;
}
static float getPitch(AudioSystem *audio, int32_t id) { return voice(audio, id)->pitch; }
static void stopSound(AudioSystem *audio, int32_t id) { voice(audio, id)->active = false; }
static void pauseSound(AudioSystem *audio, int32_t id) { voice(audio, id)->paused = true; }
static void resumeSound(AudioSystem *audio, int32_t id) { voice(audio, id)->paused = false; }
static void pauseAll(AudioSystem *audio) { ((TestAudio *)audio)->paused = true; }
static void resumeAll(AudioSystem *audio) { ((TestAudio *)audio)->paused = false; }
static bool isAllPaused(AudioSystem *audio) { return ((TestAudio *)audio)->paused; }
static void updateAudio(AudioSystem *audio, float dt) {
    TestAudio *test = (TestAudio *)audio;
    uint32_t ms = (uint32_t)(dt * 1000);
    for (int i = 0; i < test->count; i++) {
        TestVoice *v = &test->voices[i];
        if (audio->vtable->pauseStopsFades && (test->paused || v->paused)) continue;
        if (!v->fade) continue;
        if (ms >= v->fade) { v->gain = v->gainTarget; v->fade = 0; }
        else { v->gain += (v->gainTarget - v->gain) * (float)ms / (float)v->fade; v->fade -= ms; }
    }
}
static void setSpatial(AudioSystem *audio, int32_t id, float x, float y, float z,
                       float vx, float vy, float vz, float ref, float max, float factor) {
    TestAudio *test = (TestAudio *)audio;
    (void)voice(audio, id); (void)x; (void)y; (void)z; (void)ref; (void)max; (void)factor;
    test->velocity[0] = vx; test->velocity[1] = vy; test->velocity[2] = vz;
}

static AudioSystemVtable testVtable = {
    .playSound = playSound, .stopSound = stopSound,
    .setSoundSpatial = setSpatial,
    .setSoundGain = setGain, .getSoundGain = getGain,
    .setSoundPitch = setPitch, .getSoundPitch = getPitch,
    .update = updateAudio, .pauseSound = pauseSound, .resumeSound = resumeSound,
    .pauseAll = pauseAll, .resumeAll = resumeAll,
    .isAllPaused = isAllPaused,
};

static RValue call(VMContext *ctx, const char *name, RValue *args, int count) {
    BuiltinFunc builtin = VM_findBuiltin(ctx, name);
    assert(builtin != NULL);
    return builtin(ctx, args, count);
}

static void testDateStrings(VMContext *ctx) {
    RValue date = RValue_makeReal(46309); /* 2026-10-14, midnight UTC. */
    RValue shortDate = call(ctx, "date_date_string", &date, 1);
    RValue longDate = call(ctx, "date_datetime_string", &date, 1);
    assert(shortDate.type == RVALUE_STRING && longDate.type == RVALUE_STRING);
    assert(!strcmp(shortDate.string, "10/14/2026"));
    assert(!strcmp(longDate.string, "October 14th 2026, 00:00.00"));
    /* Keep both results alive across further calls, then release ownership. */
    RValue later = RValue_makeReal(46310);
    RValue other = call(ctx, "date_date_string", &later, 1);
    assert(!strcmp(shortDate.string, "10/14/2026"));
    assert(!strcmp(other.string, "10/15/2026"));
    RValue_free(&other);
    RValue_free(&longDate);
    RValue_free(&shortDate);
    assert(call(ctx, "date_date_string", NULL, 0).type == RVALUE_UNDEFINED);
    assert(call(ctx, "date_datetime_string", NULL, 0).type == RVALUE_UNDEFINED);
}

static void testEmitter(VMContext *ctx, const char *property) {
    TestAudio audio = {0}; audio.base.vtable = &testVtable;
    ctx->runner->audioSystem = &audio.base;
    RValue emitter = call(ctx, "audio_emitter_create", NULL, 0);
    RValue setting[] = {emitter, RValue_makeReal(0.25)};
    if (!strcmp(property, "gain")) call(ctx, "audio_emitter_gain", setting, 2);
    setting[1] = RValue_makeReal(0.5);
    if (!strcmp(property, "pitch")) call(ctx, "audio_emitter_pitch", setting, 2);
    RValue position[] = {emitter, RValue_makeReal(10), RValue_makeReal(20), RValue_makeReal(90)};
    RValue velocity[] = {emitter, RValue_makeReal(1), RValue_makeReal(2), RValue_makeReal(3)};
    call(ctx, "audio_emitter_position", position, 4);
    call(ctx, "audio_emitter_velocity", velocity, 4);
    RValue args[] = {emitter, RValue_makeReal(0), RValue_makeBool(false), RValue_makeReal(0),
                     RValue_makeReal(0.5), RValue_makeUndefined(), RValue_makeReal(2)};
    RValue first = call(ctx, "audio_play_sound_on", args, 7);
    call(ctx, "audio_play_sound_on", args, 4);
    if (!strcmp(property, "gain")) {
        assert(audio.voices[0].gain == 0.0625f && audio.voices[1].gain == 0.125f);
        setting[1] = RValue_makeReal(0);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0 && audio.voices[1].gain == 0);
        setting[1] = RValue_makeReal(0.5);
        call(ctx, "audio_emitter_gain", setting, 2);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.125f && audio.voices[1].gain == 0.25f);
        RValue individual[] = {first, RValue_makeReal(0.5), RValue_makeReal(500)};
        call(ctx, "audio_sound_gain", individual, 3);
        assert(audio.voices[0].gain == 0.125f && audio.voices[0].gainTarget == 0.25f && audio.voices[0].fade == 500);
        call(ctx, "audio_emitter_position", position, 4);
        assert(audio.voices[0].gain == 0.125f && audio.voices[0].fade == 500);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.125f && audio.voices[0].fade == 500);
        setting[1] = RValue_makeReal(1);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.25f && audio.voices[0].gainTarget == 0.5f);
        assert(audio.voices[0].fade > 0 && audio.voices[0].fade <= 500);
        setting[1] = RValue_makeReal(0.5);
        call(ctx, "audio_emitter_gain", setting, 2);
        /* Halfway through the fade: mute, advance the backend's update clock,
         * then restore volume. The fade must resume from its current value. */
        Runner_updateAudio(ctx->runner, 0.25f);
        setting[1] = RValue_makeReal(0);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0 && audio.voices[0].fade == 250);
        Runner_updateAudio(ctx->runner, 0.125f);
        setting[1] = RValue_makeReal(0.5);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.21875f && audio.voices[0].gainTarget == 0.25f);
        assert(audio.voices[0].fade == 125);
        Runner_updateAudio(ctx->runner, 0.125f);
        setting[1] = RValue_makeReal(1);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.5f && audio.voices[0].fade == 0);
        setting[1] = RValue_makeReal(0.5);
        call(ctx, "audio_emitter_gain", setting, 2);
        RValue asset[] = {RValue_makeReal(0), RValue_makeReal(0.5), RValue_makeReal(0)};
        call(ctx, "audio_sound_gain", asset, 3);
        assert(audio.voices[0].gain == 0.25f && audio.voices[1].gain == 0.25f);
        setting[1] = RValue_makeReal(1);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.5f && audio.voices[1].gain == 0.5f);
        /* Pocket freezes sample-driven fades while paused, including while
         * emitter gain is zero. Backend update time must respect that. */
        testVtable.pauseStopsFades = true;
        individual[1] = RValue_makeReal(0.25);
        call(ctx, "audio_sound_gain", individual, 3);
        setting[1] = RValue_makeReal(0);
        call(ctx, "audio_emitter_gain", setting, 2);
        call(ctx, "audio_pause_sound", &first, 1);
        Runner_updateAudio(ctx->runner, 1);
        setting[1] = RValue_makeReal(1);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.5f && audio.voices[0].gainTarget == 0.25f);
        assert(audio.voices[0].fade == 500);
        call(ctx, "audio_resume_sound", &first, 1);
        Runner_updateAudio(ctx->runner, 0.25f);
        assert(audio.voices[0].gain == 0.375f && audio.voices[0].fade == 250);
        call(ctx, "audio_pause_all", NULL, 0);
        Runner_updateAudio(ctx->runner, 1);
        setting[1] = RValue_makeReal(0.5);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.1875f && audio.voices[0].fade == 250);
        call(ctx, "audio_resume_all", NULL, 0);
        Runner_updateAudio(ctx->runner, 0.25f);
        assert(audio.voices[0].gain == 0.125f && audio.voices[0].fade == 0);
        /* Desktop backends continue their gain ramp on paused sources. */
        testVtable.pauseStopsFades = false;
        individual[1] = RValue_makeReal(0.5);
        call(ctx, "audio_sound_gain", individual, 3);
        call(ctx, "audio_pause_sound", &first, 1);
        Runner_updateAudio(ctx->runner, 1);
        setting[1] = RValue_makeReal(1);
        call(ctx, "audio_emitter_gain", setting, 2);
        assert(audio.voices[0].gain == 0.5f && audio.voices[0].fade == 0);
    } else if (!strcmp(property, "pitch")) {
        assert(audio.voices[0].pitch == 2 && audio.voices[1].pitch == 1);
        setting[1] = RValue_makeReal(1.5);
        call(ctx, "audio_emitter_pitch", setting, 2);
        call(ctx, "audio_emitter_pitch", setting, 2);
        assert(audio.voices[0].pitch == 6 && audio.voices[1].pitch == 3);
        RValue individual[] = {first, RValue_makeReal(4)};
        call(ctx, "audio_sound_pitch", individual, 2);
        assert(audio.voices[0].pitch == 6);
        setting[1] = RValue_makeReal(2);
        call(ctx, "audio_emitter_pitch", setting, 2);
        assert(audio.voices[0].pitch == 8 && audio.voices[1].pitch == 4);
        RValue asset[] = {RValue_makeReal(0), RValue_makeReal(3)};
        call(ctx, "audio_sound_pitch", asset, 2);
        assert(audio.voices[0].pitch == 6 && audio.voices[1].pitch == 6);
    } else {
        assert(audio.velocity[0] == 1 && audio.velocity[1] == 2 && audio.velocity[2] == 3);
    }
    call(ctx, "audio_emitter_free", &emitter, 1);
    assert(!audio.voices[0].active && !audio.voices[1].active);
    ctx->runner->audioSystem = NULL;
}

static void testDynamicSpriteName(VMContext *ctx) {
    RuntimeLayer layer = {0}; layer.id = 5;
    arrput(ctx->runner->runtimeLayers, layer);
    RValue args[] = {RValue_makeReal(5), RValue_makeReal(10),
                    RValue_makeReal(20), RValue_makeReal(3)};
    RValue id = call(ctx, "layer_sprite_create", args, 4);
    RuntimeLayer *created = &ctx->runner->runtimeLayers[0];
    assert(arrlen(created->elements) == 1);
    assert(created->elements[0].id == (uint32_t)RValue_toInt32(id));
    RuntimeSpriteElement *sprite = created->elements[0].spriteElement;
    assert(sprite && sprite->name == NULL);
    assert(sprite->x == 10 && sprite->y == 20 && sprite->spriteIndex == 3);
    Runner_freeRuntimeLayer(created);
    arrfree(ctx->runner->runtimeLayers);
}

#ifdef GAME_DELTARUNE
static void testSleepBenchmark(VMContext *ctx) {
    Runner *r = ctx->runner;
    Room rooms[8] = {{0}};
    rooms[7].name = "room_town_krisyard";
    ctx->dataWin->room.rooms = rooms; ctx->dataWin->room.count = 8;
    ctx->dataWin->gen8.name = "DELTARUNE"; ctx->dataWin->gen8.wadVersion = 17;
    ctx->globalScopeInstance = Instance_create(1, STRUCT_OBJECT_INDEX, 0, 0);
    r->vmContext = ctx; r->keyboard = RunnerKeyboard_create();
    r->frameCount = 30; r->pendingRoom = -1;
    assert(ofGame.sleepTest != NULL);
    ofGame.sleepTest(r);
    assert(r->pendingRoom == 7);
    int plot = VM_getOrAllocateVarID(ctx, "plot");
    assert(RValue_toReal(Instance_getSelfVar(ctx->globalScopeInstance, plot)) == 60);
    r->currentRoomIndex = 7; r->currentRoom = &rooms[7]; r->pendingRoom = -1;
    r->frameCount = 300;
    ofGame.sleepTest(r); assert(r->pendingRoom == -1); /* A waits indefinitely. */
    RunnerKeyboard_onKeyDown(r->keyboard, VK_ENTER);
    ofGame.sleepTest(r);
    assert(r->pendingRoom == 7);
    assert(RValue_toReal(Instance_getSelfVar(ctx->globalScopeInstance, plot)) == 0);
    assert(!RunnerKeyboard_check(r->keyboard, VK_ENTER));
    r->pendingRoom = -1; r->frameCount++;
    ofGame.sleepTest(r); assert(r->pendingRoom == -1); /* B doesn't keep reloading. */
    GameObject mainCharacter = {0}; mainCharacter.name = "obj_mainchara";
    ctx->dataWin->objt.objects = &mainCharacter; ctx->dataWin->objt.count = 1;
    Instance *character = Instance_create(2, 0, 0, 0);
    Instance_setSelfVar(character, VM_getOrAllocateVarID(ctx, "cutscene"), RValue_makeReal(1));
    arrput(r->instances, character);
    assert(ofGame.savestateReason(r) == NULL); /* The marker alone isn't an unsupported resource. */
    ctx->dataWin->gen8.wadVersion = 16;
    assert(ofGame.savestateReason(r) != NULL);
    ctx->dataWin->gen8.wadVersion = 17;
    Instance_free(character); arrfree(r->instances);
    ctx->dataWin->objt.objects = NULL; ctx->dataWin->objt.count = 0;
    Instance_free(ctx->globalScopeInstance); ctx->globalScopeInstance = NULL;
    RunnerKeyboard_free(r->keyboard); r->keyboard = NULL;
    for (ptrdiff_t i = 0; i < shlen(ctx->varNameMap); i++) free(ctx->varNameMap[i].key);
    shfree(ctx->varNameMap); r->currentRoom = NULL;
    ctx->dataWin->room.rooms = NULL; ctx->dataWin->room.count = 0;
}
#endif

int main(int argc, char **argv) {
    DataWin data = {0}; Runner runner = {0}; VMContext ctx = {0};
    data.gen8.wadVersion = 16;
    runner.dataWin = &data;
    ctx.dataWin = &data; ctx.runner = &runner;
    VMBuiltins_registerAll(&ctx);
#ifdef GAME_DELTARUNE
    if (argc == 1 || !strcmp(argv[1], "sleep")) testSleepBenchmark(&ctx);
#endif
    if (argc == 1 || !strcmp(argv[1], "sprite")) testDynamicSpriteName(&ctx);
    if (argc == 1 || !strcmp(argv[1], "date")) testDateStrings(&ctx);
    const char *properties[] = {"gain", "pitch", "velocity"};
    for (int i = 0; i < 3; i++)
        if (argc == 1 || !strcmp(argv[1], properties[i])) testEmitter(&ctx, properties[i]);
    arrfree(runner.audioEmitters);
    shfree(ctx.builtinMap);
    puts("PASS: dynamic sprite name, date strings and emitter compatibility");
    return 0;
}
