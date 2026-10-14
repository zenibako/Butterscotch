/*
 * Deltarune: what the port does differently for it. See of_game.h.
 */

#include "of_game.h"

#include <stdlib.h>

#include "instance.h"
#include "log.h"
#include "renderer.h"
#include "runner.h"
#include "rvalue.h"
#include "vm.h"

#define DELTARUNE_GRASS_MAX_CELLS 4096

/* a < b as the VM's Cmp does it. */
static bool lessThan(GMLReal a, GMLReal b) {
    GMLReal diff = a - b;
    return GMLReal_fabs(diff) > GML_MATH_EPSILON && diff < 0;
}

static bool isNumber(RValue value) {
    return value.type == RVALUE_REAL || value.type == RVALUE_INT32;
}

/* gml_Object_obj_purplegrass_Draw_0: the floor of the field, and
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
    static int32_t subimgs[DELTARUNE_GRASS_MAX_CELLS];
    bool whole = cols > 0 && rows > 0 && cols <= DELTARUNE_GRASS_MAX_CELLS && rows <= DELTARUNE_GRASS_MAX_CELLS &&
                 cols * rows <= DELTARUNE_GRASS_MAX_CELLS && runner->renderer->vtable->drawSpriteGrid != NULL;
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

/* gml_Script___view_get(prop, index): GameMaker's own shim for the
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

/* gml_Script_scr_84_get_sprite(name):
 *
 *   return ds_map_find_value(global.chemg_sprite_map, name)
 */
static int32_t g_spriteMapVar = -1; /* the VM's own id for the name; forgetVm forgets it for each new VM */
static bool get84Sprite(VMContext *ctx, RValue *args, int32_t argCount, RValue *result) {
    static BuiltinFunc find = NULL;
    if (find == NULL) find = VM_findBuiltin(ctx, "ds_map_find_value");
    if (g_spriteMapVar < 0) g_spriteMapVar = VM_getOrAllocateVarID(ctx, "chemg_sprite_map");
    int32_t mapVar = g_spriteMapVar;
    if (find == NULL || argCount < 1 || args == NULL || ctx->globalScopeInstance == NULL) return false;
    RValue call[2] = { Instance_getSelfVar(ctx->globalScopeInstance, mapVar), args[0] };
    *result = find(ctx, call, 2);
    return true;
}

static const OfNativeScript g_nativeScripts[] = {
    { "gml_Script___view_get", 1260, 0xeff822f793176fb9ull, viewGet },
    { "gml_Script_scr_84_get_sprite", 84, 0x4121b98b835bc977ull, get84Sprite },
};

static const OfNativeCode g_natives[] = {
    { "gml_Object_obj_purplegrass_Draw_0", 348, 0x8cf611b685a27620ull, purpleGrassDraw },
};

static void forgetVm(void) {
    g_spriteMapVar = -1;
}

/* The opening is played as it comes (left and confirm, which gets through the
 * first text and into the character creation screens). The town and the Dark
 * World are a long way into the game, so the run jumps to them: the party and
 * the dark-zone flag are set the frame before each jump. In the field the
 * walk to the right runs into the room's enemy, and confirm plays the battle
 * out. Room numbers and frame counts are for the chapter 1 data file. */
#define DELTARUNE_YARD_FRAME 2100
#define DELTARUNE_FIELD_FRAME 2600
static const char g_script[] =
    "150:L*4,160:Z,174:L*4,184:Z,198:L*4,208:Z,222:L*4,232:Z,246:L*4,256:Z,270:L*4,280:Z,294:L*4,304:Z,318:L*4,"
    "328:Z,342:L*4,352:Z,366:L*4,376:Z,390:L*4,400:Z,414:L*4,424:Z,438:L*4,448:Z,462:L*4,472:Z,486:L*4,496:Z,"
    "510:L*4,520:Z,534:L*4,544:Z,558:L*4,568:Z,582:L*4,592:Z,606:L*4,616:Z,630:L*4,640:Z,654:L*4,664:Z,678:L*4,"
    "688:Z,702:L*4,712:Z,726:L*4,736:Z,750:L*4,760:Z,774:L*4,784:Z,798:L*4,808:Z,822:L*4,832:Z,846:L*4,856:Z,"
    "870:L*4,880:Z,894:L*4,904:Z,918:L*4,928:Z,942:L*4,952:Z,966:L*4,976:Z,990:L*4,1000:Z,1014:L*4,1024:Z,1038:L*4,"
    "1048:Z,1062:L*4,1072:Z,1086:L*4,1096:Z,1110:L*4,1120:Z,1134:L*4,1144:Z,1158:L*4,1168:Z,1182:L*4,1192:Z,"
    "1206:L*4,1216:Z,1230:L*4,1240:Z,1254:L*4,1264:Z,1278:L*4,1288:Z,1302:L*4,1312:Z,1326:L*4,1336:Z,1350:L*4,"
    "1360:Z,1374:L*4,1384:Z,1398:L*4,1408:Z,1422:L*4,1432:Z,1446:L*4,1456:Z,1470:L*4,1480:Z,1494:L*4,1504:Z,"
    "1518:L*4,1528:Z,1542:L*4,1552:Z,1566:L*4,1576:Z,1590:L*4,1600:Z,1614:L*4,1624:Z,1638:L*4,1648:Z,1662:L*4,"
    "1672:Z,1686:L*4,1696:Z,1710:L*4,1720:Z,1734:L*4,1744:Z,1758:L*4,1768:Z,1782:L*4,1792:Z,1806:L*4,1816:Z,"
    "1830:L*4,1840:Z,1854:L*4,1864:Z,1878:L*4,1888:Z,1902:L*4,1912:Z,1926:L*4,1936:Z,1950:L*4,1960:Z,1974:L*4,"
    "1984:Z,1998:L*4,2008:Z,2022:L*4,2032:Z,2046:L*4,2056:Z,2150:R*120,2290:D*60,2370:L*100,2490:U*60,2720:R*500,"
    "3000:Z,3015:Z,3030:Z,3045:Z,3060:Z,3075:Z,3090:Z,3105:Z,3120:Z,3135:Z,3150:Z,3165:Z,3180:Z,3195:Z,3210:Z,"
    "3225:Z,3240:Z,3255:Z,3270:Z,3285:Z,3300:Z,3315:Z,3330:Z,3345:Z,3360:Z,3375:Z,3390:Z,3405:Z,3420:Z,3435:Z,"
    "3450:Z,3465:Z,3480:Z,3495:Z,3510:Z,3525:Z,3540:Z,3555:Z,3570:Z,3585:Z";

static const OfJump g_jumps[] = {
    { DELTARUNE_YARD_FRAME - 1, -1, "plot=60,interact=0" }, /* past the scene that would otherwise start in the yard */
    { DELTARUNE_YARD_FRAME, 7, NULL },
    { DELTARUNE_FIELD_FRAME - 1, -1, "darkzone=1,char[1]=2,char[2]=3,plot=60,interact=0" },
    { DELTARUNE_FIELD_FRAME, 52, NULL },
};

static const OfBenchSection g_sections[] = {
    { "opening text 320x240", 1700, "text" },
    { "creation screens 320", 2100, "creat" },
    { "town yard 320x240", 2600, "yard" },
    { "field (640x480)", 3050, "field" },
    /* The battle in three: choosing and attacking, the enemy's turn (the
     * heart in its box among the bullets), and the turn after. */
    { "battle: menu, attack", 3225, "bmenu" },
    { "battle: dodging", 3380, "dodge" },
    { "battle: next turn", 3600, "bnext" },
};

/* Save states are not supported yet (see of_state_requests.c). */
static const char *savestateReason(Runner *r) {
    (void)r;
    return "Deltarune saves aren't ready";
}

const OfGame ofGame = {
    .natives = g_natives,
    .nativeCount = sizeof(g_natives) / sizeof(g_natives[0]),
    .nativeScripts = g_nativeScripts,
    .nativeScriptCount = sizeof(g_nativeScripts) / sizeof(g_nativeScripts[0]),
    .nativesForgetVm = forgetVm,
    .benchTitle = "Deltarune ch. 1",
    .benchScript = g_script,
    .benchJumps = g_jumps,
    .benchJumpCount = (int) (sizeof(g_jumps) / sizeof(g_jumps[0])),
    .benchSections = g_sections,
    .benchSectionCount = (int) (sizeof(g_sections) / sizeof(g_sections[0])),
    /* The audio pack is about 43 MB against Undertale's 134; the read tests use the same offsets, scaled. */
    .ioDivisor = 3u,
    .ioAsyncBase = 35u,
    .ioAsyncStep = 1u,
    .savestateReason = savestateReason,
};
