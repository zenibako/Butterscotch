/*
 * Rewrites the handful of game strings that name keyboard keys so they name
 * the Pocket's buttons instead (see the mapping in of_platform.c).
 *
 * The user's data.win is never modified: the strings are patched in memory
 * after loading. A replacement no longer than the text it replaces is written
 * over the original in the string table, which covers every way the runner
 * reaches a string. A longer one cannot be, so the table's entry is pointed
 * at the new text instead; that reaches code that looks the string up when it
 * runs (all the instruction screen needs), not anything that kept the old
 * pointer from loading.
 *
 * One line of the instruction screen is removed as well, which takes a
 * change to the game's code and not its strings; see utPatchInstructions.
 */

#include "ut_strings.h"

#include "log.h"

#include <string.h>

typedef struct {
    const char *from;
    const char *to;
} UtStringPatch;

static const UtStringPatch g_patches[] = {
    /* Title screen and the instruction screen. The instruction screen and
     * the Pocket's own controls screen (dist/.../input.json) describe the
     * same buttons: keep their wording the same. Debug mode's buttons are
     * named on the Pocket's screen only. */
    { "[PRESS Z OR ENTER]", "[PRESS A OR START]" },
    { "[Z or ENTER]", "[A or START]" },
    { "[X or SHIFT]", "[B]" },
    { "[C or CTRL]", "[X or Y]" },
    /* F4 toggled fullscreen; here L switches the 640x480 screens between
     * native resolution (accurate) and smoothed 320x240 (fast). The Esc-to-
     * quit line has no counterpart and is dropped by utPatchInstructions. */
    { "[F4]", "[L]" },
    { "Fullscreen", "Toggle speed/accuracy" },
    /* Settings / joystick screens. */
    { "[Z / ENTER]", "[A / START]" },
    { "[X / SHIFT]", "[B]" },
    { "[C / CTRL]", "[X / Y]" },
    /* Key names substituted into dialogue. */
    { "[Z]", "[A]" },
    { "[X]", "[B]" },
    { "[C]", "[X]" },
    /* Papyrus's date. */
    { "\\XSTEP ONE..^1. PRESS&THE [ C ] KEY ON&YOUR KEYBOARD FOR&\"\\RDATING HUD\\X.\"/",
      "\\XSTEP ONE..^1. PRESS&THE [ X ] KEY ON&YOUR POCKET FOR&\"\\RDATING HUD\\X.\"/" },
    { "\\XSTEP ONE..^1. PRESS&THE [ C ] KEY ON&YOUR KEYBOARD FOR&\"\\RFRIENDSHIP HUD\\X.\"/",
      "\\XSTEP ONE..^1. PRESS&THE [ X ] KEY ON&YOUR POCKET FOR&\"\\RFRIENDSHIP HUD\\X.\"/" },
};
#define UT_PATCH_COUNT (sizeof(g_patches) / sizeof(g_patches[0]))

void utPatchStrings(DataWin *dw) {
    if (dw == NULL || dw->strg.strings == NULL) return;

    unsigned patched = 0;
    for (uint32_t i = 0; i < dw->strg.count; i++) {
        char *text = (char *) dw->strg.strings[i];
        if (text == NULL || (text[0] != '[' && text[0] != 'F' && text[0] != '\\')) continue;

        for (size_t p = 0; p < UT_PATCH_COUNT; p++) {
            if (strcmp(text, g_patches[p].from) != 0) continue;
            if (strlen(g_patches[p].to) <= strlen(g_patches[p].from))
                strcpy(text, g_patches[p].to);
            else
                dw->strg.strings[i] = (char *) g_patches[p].to;
            patched++;
            break;
        }
    }
    logInfo("Strings: %u key prompts renamed for the Pocket's buttons\n", patched);
    utPatchInstructions(dw);
}

/* Undertale v1.08's instruction screen lists five keys from a table, the
 * last being "[Hold ESC] - Quit". A Pocket core is left from the Pocket's
 * own menu, which the game cannot describe, so the list is cut to four by
 * changing the count the screen's code loads. The patch is applied only if
 * that code is exactly the build it was written against. */
#define UT_NAMING_SCRIPT "gml_Script_scr_namingscreen"
#define UT_NAMING_LENGTH 12332u
#define UT_NAMING_COUNT_AT 0x2A58u
#define UT_PUSH_FIVE 0x840F0005u
#define UT_PUSH_FOUR 0x840F0004u

void utPatchInstructions(DataWin *dw) {
    if (dw->bytecodeBuffer == NULL) return;
    for (uint32_t i = 0; i < dw->code.count; i++) {
        CodeEntry *entry = &dw->code.entries[i];
        if (!entry->present || entry->name == NULL || strcmp(entry->name, UT_NAMING_SCRIPT) != 0) continue;
        if (entry->length != UT_NAMING_LENGTH) return;

        uint8_t *at = dw->bytecodeBuffer + (entry->bytecodeAbsoluteOffset - dw->bytecodeBufferBase) + UT_NAMING_COUNT_AT;
        uint32_t word;
        memcpy(&word, at, sizeof(word));
        if (word != UT_PUSH_FIVE) return;
        word = UT_PUSH_FOUR;
        memcpy(at, &word, sizeof(word));
        logInfo("Strings: instruction screen shortened to four lines\n");
        return;
    }
}
