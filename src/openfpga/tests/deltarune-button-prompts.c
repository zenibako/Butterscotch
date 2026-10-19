/* Exercise the profile's callback with real VM arrays and instances.
 * No game assets or initialized graphics/audio are needed. */
#include "../platform/game_deltarune.c"
#include <assert.h>
#include <stdio.h>
#include "of_gamepad.h"

static void set(VMContext *vm, Instance *inst, const char *name, RValue value) {
    Instance_setSelfVar(inst, VM_getOrAllocateVarID(vm, name), value);
}

int main(void) {
    DataWin dw = {0};
    dw.gen8.wadVersion = 17;
    Runner runner = {0};
    VMContext vm = {0};
    Instance *globals = Instance_create(1, -1, 0, 0);
    vm.globalScopeInstance = globals;
    vm.runner = &runner; vm.dataWin = &dw; runner.dataWin = &dw;
    GMLArray *input = GMLArray_create(&dw, 10);
    const int defaults[] = {40, 39, 38, 37, 90, 88, 67, 13, 16, 17};
    const char *expected[] = {
        OF_ICON_DOWN, OF_ICON_RIGHT, OF_ICON_UP, OF_ICON_LEFT, OF_ICON_A,
        OF_ICON_B, OF_ICON_X, OF_ICON_START, OF_ICON_B, OF_ICON_X,
    };
    for (int i = 0; i < 10; i++) *GMLArray_slot(input, i) = RValue_makeInt32(defaults[i]);
    set(&vm, globals, "input_k", RValue_makeArrayWeak(input));
    set(&vm, globals, "is_console", RValue_makeReal(0));
    for (int i = 0; i < 10; i++) {
        RValue arg = RValue_makeInt32(i), result;
        assert(inputName(&vm, &arg, 1, &result));
        assert(result.type == RVALUE_STRING);
        assert(strlen(result.string) == 4 && result.string[3] == ' ');
        assert(!strncmp(result.string, expected[i], 3));
    }
    RValue arg = RValue_makeInt32(4), result;
    set(&vm, globals, "is_console", RValue_makeReal(1));
    assert(!inputName(&vm, &arg, 1, &result));
    set(&vm, globals, "is_console", RValue_makeReal(0));
    *GMLArray_slot(input, 4) = RValue_makeInt32('Q');
    assert(!inputName(&vm, &arg, 1, &result));
    *GMLArray_slot(input, 4) = RValue_makeInt32('Z');
    GameObject object = {0};
    object.name = "obj_gamecontroller";
    dw.objt.count = 1; dw.objt.objects = &object;
    Instance *controller = Instance_create(2, 0, 0, 0);
    arrput(runner.instances, controller);
    set(&vm, controller, "gamepad_active", RValue_makeReal(1));
    assert(!inputName(&vm, &arg, 1, &result));
    runner.gamepads = RunnerGamepad_create();
    ofGamepadPoll(runner.gamepads, 0, false);
    set(&vm, controller, "gamepad_id", RValue_makeInt32(0));
    GMLArray *padInput = GMLArray_create(&dw, 10);
    const int padDefaults[] = {GP_PADD, GP_PADR, GP_PADU, GP_PADL, GP_FACE1, GP_FACE2, GP_FACE4, 999, 999, 999};
    for (int i=0;i<10;i++) *GMLArray_slot(padInput,i)=RValue_makeInt32(padDefaults[i]);
    set(&vm, globals, "input_g", RValue_makeArrayWeak(padInput));
    for (int i=0;i<7;i++) {
        arg=RValue_makeInt32(i);
        assert(inputName(&vm, &arg, 1, &result));
        assert(!strncmp(result.string, expected[i],3));
    }
    arg=RValue_makeInt32(4); /* Rabbick Blow uses action 4, including after remapping. */
    *GMLArray_slot(padInput,4)=RValue_makeInt32(GP_FACE2);
    assert(inputName(&vm, &arg, 1, &result));
    assert(!strncmp(result.string,OF_ICON_B,3));
    *GMLArray_slot(padInput,4)=RValue_makeInt32(13); /* Legacy DR face1. */
    assert(inputName(&vm, &arg, 1, &result));
    assert(!strncmp(result.string,OF_ICON_A,3));
    *GMLArray_slot(padInput,4)=RValue_makeInt32(GP_FACE3); /* Unmapped face button. */
    assert(!inputName(&vm, &arg, 1, &result));
    *GMLArray_slot(padInput,4)=RValue_makeInt32(GP_FACE1);
    set(&vm, controller,"gamepad_id",RValue_makeInt32(1));
    assert(!inputName(&vm, &arg, 1, &result));
    RunnerGamepad_free(runner.gamepads); runner.gamepads=NULL;
    GMLArray_decRef(padInput);
    set(&vm, controller, "gamepad_active", RValue_makeReal(0));
    assert(inputName(&vm, &arg, 1, &result));
    arg = RValue_makeInt32(-1);
    assert(!inputName(&vm, &arg, 1, &result));
    arg = RValue_makeInt32(10);
    assert(!inputName(&vm, &arg, 1, &result));
    assert(!inputName(&vm, NULL, 0, &result));
    arg = RValue_makeString("x");
    assert(!inputName(&vm, &arg, 1, &result));
    Instance_free(controller); Instance_free(globals); GMLArray_decRef(input);
    arrfree(runner.instances);
    for (ptrdiff_t i = 0; i < shlen(vm.varNameMap); i++) free(vm.varNameMap[i].key);
    shfree(vm.varNameMap);
    puts("Deltarune native prompts: defaults and fallback passed");
    return 0;
}
