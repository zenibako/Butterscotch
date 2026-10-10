/* Native stand-ins for game scripts.
 *
 * The interpreter costs about 2 microseconds an operation on the Pocket, and
 * a few of a game's scripts run thousands of operations a frame to do very
 * little. Such a script is written out here in C, operation for operation:
 * the same arithmetic in the same order and precision, the same calls, the
 * same variables left behind. It is installed by VM_setNativeCode, which
 * checks the script's bytecode against the hash below, so any other version
 * of the game simply runs its own bytecode.
 *
 * To add one: disassemble the entry (UT_DISASM=<name>), write it out, get
 * the length and hash from UT_CODEHASH=<name> on the desktop build, and
 * compare frames with and without it (UT_NO_NATIVE=1). */

#include "ut_native.h"

#include <stdlib.h>

#include "instance.h"
#include "log.h"
#include "renderer.h"
#include "rvalue.h"
#include "vm.h"

#define UT_GRASS_MAX_CELLS 4096

/* a < b as the VM's Cmp does it. */
static bool lessThan(GMLReal a, GMLReal b) {
    GMLReal diff = a - b;
    return GMLReal_fabs(diff) > GML_MATH_EPSILON && diff < 0;
}

static bool isNumber(RValue value) {
    return value.type == RVALUE_REAL || value.type == RVALUE_INT32;
}

/* Deltarune, gml_Object_obj_purplegrass_Draw_0: the floor of the field, and
 * what is behind its battles. About 11,000 operations a frame for:
 *
 *   index += 0.2
 *   for (i = 0; i < length; i += 1)
 *       for (j = 0; j < height; j += 1)
 *           draw_sprite(sprite_index, index + x / 320 + i * 0.125 + j * 0.125 + y / 320,
 *                       x + 40 * i, y + 40 * j)
 */
static bool purpleGrassDraw(VMContext *ctx) {
    static int32_t indexVar = -1, lengthVar, heightVar, iVar, jVar;
    Instance *self = ctx->currentInstance;
    Runner *runner = ctx->runner;
    if (self == NULL || runner->renderer == NULL) return false;
    if (indexVar < 0) {
        indexVar = VM_getOrAllocateVarID(ctx, "index");
        lengthVar = VM_getOrAllocateVarID(ctx, "length");
        heightVar = VM_getOrAllocateVarID(ctx, "height");
        iVar = VM_getOrAllocateVarID(ctx, "i");
        jVar = VM_getOrAllocateVarID(ctx, "j");
    }
    RValue indexValue = Instance_getSelfVar(self, indexVar);
    RValue lengthValue = Instance_getSelfVar(self, lengthVar);
    RValue heightValue = Instance_getSelfVar(self, heightVar);
    if (!isNumber(indexValue) || !isNumber(lengthValue) || !isNumber(heightValue)) return false;

    GMLReal index = RValue_toReal(indexValue) + (GMLReal) 0.2;
    Instance_setSelfVar(self, indexVar, RValue_makeReal(index));
    GMLReal length = RValue_toReal(lengthValue), height = RValue_toReal(heightValue);
    GMLReal x = (GMLReal) self->x, y = (GMLReal) self->y;
    int32_t sprite = self->spriteIndex;

    /* The loops' counters come out the same however many cells are drawn. */
    GMLReal i = 0, j = 0;
    int32_t cols = 0, rows = 0;
    for (; lessThan(i, length); i = i + 1) cols++;
    bool innerRan = cols > 0;
    if (innerRan) for (; lessThan(j, height); j = j + 1) rows++;

    /* The image of each cell, in the order the script draws them; then the
     * whole grid in one call if the renderer takes it, else cell by cell. */
    static int32_t subimgs[UT_GRASS_MAX_CELLS];
    bool whole = cols > 0 && rows > 0 && cols <= UT_GRASS_MAX_CELLS && rows <= UT_GRASS_MAX_CELLS &&
                 cols * rows <= UT_GRASS_MAX_CELLS && runner->renderer->vtable->drawSpriteGrid != NULL;
    GMLReal ci = 0;
    for (int32_t col = 0; col < cols; col++, ci = ci + 1) {
        GMLReal cj = 0;
        for (int32_t row = 0; row < rows; row++, cj = cj + 1) {
            GMLReal image = index + x / (GMLReal) 320;
            image = image + ci * (GMLReal) 0.125;
            image = image + cj * (GMLReal) 0.125;
            image = image + y / (GMLReal) 320;
            int32_t subimg = (int32_t) image;
            if (0 > subimg) subimg = (int32_t) self->imageIndex; /* as draw_sprite does */
            if (whole) subimgs[col * rows + row] = subimg;
            else Renderer_drawSprite(runner->renderer, sprite, subimg, (float) (x + (GMLReal) 40 * ci), (float) (y + (GMLReal) 40 * cj));
        }
    }
    if (whole && !runner->renderer->vtable->drawSpriteGrid(runner->renderer, sprite, subimgs, cols, rows, (float) x, (float) y, 40.0f, 40.0f)) {
        ci = 0;
        for (int32_t col = 0; col < cols; col++, ci = ci + 1) {
            GMLReal cj = 0;
            for (int32_t row = 0; row < rows; row++, cj = cj + 1)
                Renderer_drawSprite(runner->renderer, sprite, subimgs[col * rows + row], (float) (x + (GMLReal) 40 * ci), (float) (y + (GMLReal) 40 * cj));
        }
    }
    Instance_setSelfVar(self, iVar, RValue_makeReal(i));
    if (innerRan) Instance_setSelfVar(self, jVar, RValue_makeReal(j));
    return true;
}

/* Deltarune, gml_Script___view_get(prop, index): GameMaker's own shim for the
 * old view_xview[] family, a chain of seventeen compares that ends in one or
 * two built-in calls. Battle scripts call it some twenty times a frame.
 *
 *   switch (prop) { case 0: return camera_get_view_x(view_get_camera(index)); ... }
 *
 * Anything but a number from 0 to 16 for prop is left to the bytecode. */
static bool viewGet(VMContext *ctx, RValue *args, int32_t argCount, RValue *result) {
    static const char *const names[17] = {
        "camera_get_view_x", "camera_get_view_y", "camera_get_view_width", "camera_get_view_height", "camera_get_view_angle",
        "camera_get_view_border_x", "camera_get_view_border_y", "camera_get_view_speed_x", "camera_get_view_speed_y",
        "camera_get_view_target", "view_get_visible", "view_get_xport", "view_get_yport", "view_get_wport", "view_get_hport",
        "view_get_camera", "view_get_surface_id",
    };
    static BuiltinFunc functions[17];
    static int resolved = 0; /* 1: all found, -1: not all */
    if (resolved == 0) {
        resolved = 1;
        for (int k = 0; k < 17; k++) {
            functions[k] = VM_findBuiltin(ctx, names[k]);
            if (functions[k] == NULL) resolved = -1;
        }
    }
    if (resolved < 0 || argCount < 2 || args == NULL || !isNumber(args[0])) return false;

    /* Cmp EQ against 0, 1, 2, ... in turn, as the bytecode does it. */
    GMLReal prop = RValue_toReal(args[0]);
    int which = -1;
    for (int k = 0; k < 17 && which < 0; k++) {
        if (GMLReal_fabs(prop - (GMLReal) k) <= GML_MATH_EPSILON) which = k;
    }
    if (which < 0) return false;

    RValue index = args[1];
    if (which <= 9) {
        /* The camera's property: the view's camera first. */
        RValue camera = functions[15](ctx, &index, 1);
        *result = functions[which](ctx, &camera, 1);
        RValue_free(&camera);
    } else {
        *result = functions[which](ctx, &index, 1);
    }
    return true;
}

/* Deltarune, gml_Script_scr_84_get_sprite(name):
 *
 *   return ds_map_find_value(global.chemg_sprite_map, name)
 */
static bool get84Sprite(VMContext *ctx, RValue *args, int32_t argCount, RValue *result) {
    static BuiltinFunc find = NULL;
    static int32_t mapVar = -1;
    if (find == NULL) {
        find = VM_findBuiltin(ctx, "ds_map_find_value");
        mapVar = VM_getOrAllocateVarID(ctx, "chemg_sprite_map");
    }
    if (find == NULL || argCount < 1 || args == NULL || ctx->globalScopeInstance == NULL) return false;
    RValue call[2] = { Instance_getSelfVar(ctx->globalScopeInstance, mapVar), args[0] };
    *result = find(ctx, call, 2);
    return true;
}

static const struct {
    const char *name;
    uint32_t length;
    uint64_t hash;
    VMNativeScript native;
} g_nativeScripts[] = {
    { "gml_Script___view_get", 1260, 0xeff822f793176fb9ull, viewGet },
    { "gml_Script_scr_84_get_sprite", 84, 0x4121b98b835bc977ull, get84Sprite },
};

static const struct {
    const char *name;
    uint32_t length;
    uint64_t hash;
    VMNativeCode native;
} g_natives[] = {
    { "gml_Object_obj_purplegrass_Draw_0", 348, 0x8cf611b685a27620ull, purpleGrassDraw },
};

void utNativeInstall(Runner *runner) {
    VMContext *vm = runner->vmContext;
    if (vm == NULL) return;
#ifdef OF_PC
    /* UT_CODEHASH=<code entry> prints what a table row needs; UT_NO_NATIVE=1 leaves the bytecode in charge. */
    const char *ask = getenv("UT_CODEHASH");
    if (ask != NULL) {
        uint32_t length = 0;
        uint64_t hash = VM_codeHash(vm, ask, &length);
        printf("[undertale] %s: length %u, hash 0x%016llxull\n", ask, (unsigned) length, (unsigned long long) hash);
    }
    if (getenv("UT_NO_NATIVE") != NULL) return;
#endif
    for (size_t n = 0; n < sizeof(g_natives) / sizeof(g_natives[0]); n++) {
        uint32_t length = 0;
        if (VM_codeHash(vm, g_natives[n].name, &length) == 0) continue; /* another game */
        bool installed = VM_setNativeCode(vm, g_natives[n].name, g_natives[n].length, g_natives[n].hash, g_natives[n].native);
        logInfo("Native: %s %s\n", g_natives[n].name, installed ? "installed" : "left to the interpreter (bytecode differs)");
    }
    for (size_t n = 0; n < sizeof(g_nativeScripts) / sizeof(g_nativeScripts[0]); n++) {
        uint32_t length = 0;
        if (VM_codeHash(vm, g_nativeScripts[n].name, &length) == 0) continue; /* another game */
        bool installed = VM_setNativeScript(vm, g_nativeScripts[n].name, g_nativeScripts[n].length, g_nativeScripts[n].hash, g_nativeScripts[n].native);
        logInfo("Native: %s %s\n", g_nativeScripts[n].name, installed ? "installed" : "left to the interpreter (bytecode differs)");
    }
}
