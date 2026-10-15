/*
 * Undertale: what the port does differently for it. See of_game.h.
 */

#include "of_game.h"

#include <string.h>

#include "log.h"
#include "of_state_requests.h"
#include "stb_ds.h"
#include "runner_keyboard.h"
#include "vm.h"

/* Prompts beyond the ones of_strings.c renames for every game. */
static const OfStringPatch g_strings[] = {
    /* Papyrus's date. */
    { "\\XSTEP ONE..^1. PRESS&THE [ C ] KEY ON&YOUR KEYBOARD FOR&\"\\RDATING HUD\\X.\"/",
      "\\XSTEP ONE..^1. PRESS&THE [ X ] KEY ON&YOUR POCKET FOR&\"\\RDATING HUD\\X.\"/" },
    { "\\XSTEP ONE..^1. PRESS&THE [ C ] KEY ON&YOUR KEYBOARD FOR&\"\\RFRIENDSHIP HUD\\X.\"/",
      "\\XSTEP ONE..^1. PRESS&THE [ X ] KEY ON&YOUR POCKET FOR&\"\\RFRIENDSHIP HUD\\X.\"/" },
};

/* Undertale v1.08's instruction screen lists five keys from a table, the
 * last being "[Hold ESC] - Quit". A Pocket core is left from the Pocket's
 * own menu, which the game cannot describe, so the list is cut to four by
 * changing the count the screen's code loads. The patch is applied only if
 * that code is exactly the build it was written against. */
#define UNDERTALE_NAMING_SCRIPT "gml_Script_scr_namingscreen"
#define UNDERTALE_NAMING_LENGTH 12332u
#define UNDERTALE_NAMING_COUNT_AT 0x2A58u
#define UNDERTALE_PUSH_FIVE 0x840F0005u
#define UNDERTALE_PUSH_FOUR 0x840F0004u

static void patchInstructions(DataWin *dw) {
    if (dw->bytecodeBuffer == NULL) return;
    for (uint32_t i = 0; i < dw->code.count; i++) {
        CodeEntry *entry = &dw->code.entries[i];
        if (!entry->present || entry->name == NULL || strcmp(entry->name, UNDERTALE_NAMING_SCRIPT) != 0) continue;
        if (entry->length != UNDERTALE_NAMING_LENGTH) return;

        uint8_t *at = dw->bytecodeBuffer + (entry->bytecodeAbsoluteOffset - dw->bytecodeBufferBase) + UNDERTALE_NAMING_COUNT_AT;
        uint32_t word;
        memcpy(&word, at, sizeof(word));
        if (word != UNDERTALE_PUSH_FIVE) return;
        word = UNDERTALE_PUSH_FOUR;
        memcpy(at, &word, sizeof(word));
        logInfo("Strings: instruction screen shortened to four lines\n");
        return;
    }
}

/* Save states (of_state_requests.c) are taken anywhere the runner can store
 * exactly (Runner_snapshotCheck refuses the rest): the overworld, the game's
 * own menus, dialogue, battles, cutscenes, and the intro, title and naming
 * screens. A battle keeps the room it left as persistent saved state, which
 * snapshots carry since revision 6. Cutscenes used to be refused; restores
 * inside Papyrus's first scene (obj_papyrus1, the earliest that sets
 * obj_mainchara.cutscene) were checked exactly, as Deltarune's were. */
static const char *savestateReason(Runner *r) {
    if (r->dataWin->gen8.wadVersion != 16 || strcmp(r->dataWin->gen8.name, "UNDERTALE"))
        return "Unsupported game version";
    return NULL;
}

/* Interactive sleep test (--bench-sleep, the "Undertale Sleep Test" entry),
 * the counterpart of Deltarune's. It jumps to Snowdin's room_tundra4, where
 * Papyrus's first scene (obj_papyrus1, present while plot < 39) is the
 * earliest that sets obj_mainchara.cutscene. Checkpoint A enters the room
 * with plot 39, past the scene: free to walk. Start reloads it with plot 1;
 * walking right then starts the real scene, checkpoint B. The phase lives in
 * a GML global, so waking an older A state also rewinds the test. */
#define UNDERTALE_SLEEP_ROOM 48
static bool playerInCutscene(Runner *r) {
    for (ptrdiff_t i = 0; i < arrlen(r->instances); i++) {
        Instance *inst = r->instances[i];
        if (!inst->active || inst->destroyed || inst->objectIndex < 0 ||
            (uint32_t)inst->objectIndex >= r->dataWin->objt.count) continue;
        if (!strcmp(r->dataWin->objt.objects[inst->objectIndex].name, "obj_mainchara"))
            return ofStateNumber(r, inst, "cutscene") != 0;
    }
    return false;
}
static const char *sleepTest(Runner *r) {
    if (savestateReason(r) || r->dataWin->room.count <= UNDERTALE_SLEEP_ROOM ||
        strcmp(r->dataWin->room.rooms[UNDERTALE_SLEEP_ROOM].name, "room_tundra4"))
        return "Sleep test needs Undertale v1.08";
    if (r->frameCount < 30) return "Preparing sleep test";
    VMContext *vm = r->vmContext;
    Instance *globals = vm->globalScopeInstance;
    int32_t phaseId = VM_getOrAllocateVarID(vm, "__pocket_sleep_test_phase");
    RValue phase = Instance_getSelfVar(globals, phaseId);
    bool start = phase.type == RVALUE_UNDEFINED;
    bool advance = !start && RValue_toInt32(phase) == 1 && r->currentRoomIndex == UNDERTALE_SLEEP_ROOM &&
        r->pendingRoom < 0 && RunnerKeyboard_checkPressed(r->keyboard, VK_ENTER);
    if (start || advance) {
        Instance_setSelfVar(globals, VM_getOrAllocateVarID(vm, "plot"), RValue_makeReal(start ? 39 : 1));
        Instance_setSelfVar(globals, VM_getOrAllocateVarID(vm, "interact"), RValue_makeReal(0));
        phase = RValue_makeReal(start ? 1 : 2);
        Instance_setSelfVar(globals, phaseId, phase);
        r->pendingRoom = UNDERTALE_SLEEP_ROOM;
        RunnerKeyboard_clear(r->keyboard, VK_ENTER);
        logInfo("Sleep test: checkpoint %s\n", start ? "A (free to walk)" : "B (Papyrus's scene)");
    }
    if (RValue_toInt32(phase) == 1) return "A: sleep/wake, then START";
    return playerInCutscene(r) ? "B: sleep/wake in cutscene" : "B: walk right to Papyrus";
}

/* Key presses that walk from boot to the first battle; see runInputScript()
 * in of_platform.c for the syntax. Frame numbers assume a fresh save state. */
static const char g_script[] =
    "30:Z,100:Z,160:Z,220:Z,230:D,238:D,246:D,254:D,262:D,270:D,278:D,286:D,300:R,308:R,330:Z,370:R,380:Z,700:R*400,1100:L*10,1120:U*400,1560:Z,1605:Z,1650:Z,1695:Z,1740:Z,1785:Z,1830:Z,1875:Z,1920:Z,1965:Z,2010:Z,2055:Z,2100:Z,2145:Z,2190:Z,2235:Z,2280:Z,2325:Z,2370:Z,2415:Z,2460:Z,2505:Z,2550:Z,2595:Z,2640:Z,2685:Z";

/* Sections end on these frames; they follow the script above. */
static const OfBenchSection g_sections[] = {
    { "intro, menu, naming", 650, "intro" },
    { "first room 320x240", 1220, "room" },
    { "Flowey talk 320x240", 2060, "talk" },
    { "Flowey battle (640x480)", 2600, "battl" },
};

const OfGame ofGame = {
    .strings = g_strings,
    .stringCount = sizeof(g_strings) / sizeof(g_strings[0]),
    .patchCode = patchInstructions,
    .benchTitle = "Undertale",
    .benchScript = g_script,
    .benchSections = g_sections,
    .benchSectionCount = (int) (sizeof(g_sections) / sizeof(g_sections[0])),
    .ioDivisor = 1u,
    .ioAsyncBase = 110u,
    .ioAsyncStep = 2u,
    .savestateReason = savestateReason,
    .sleepTest = sleepTest,
};
