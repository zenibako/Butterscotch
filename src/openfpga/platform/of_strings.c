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
 * The rows here are the prompts every game might show; a game adds its own
 * in its game_<name>.c, with any change to its code that a prompt needs
 * (Undertale's instruction screen loses a line that way).
 */

#include "of_strings.h"

#include "log.h"
#include "of_game.h"

#include <string.h>

static const OfStringPatch g_patches[] = {
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
     * quit line has no counterpart; Undertale's game_undertale.c drops it. */
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
};
#define OF_PATCH_COUNT (sizeof(g_patches) / sizeof(g_patches[0]))

/* Replaces string i if a row matches it. */
static bool patchOne(DataWin *dw, uint32_t i, char *text, const OfStringPatch *patches, size_t count) {
    for (size_t p = 0; p < count; p++) {
        if (strcmp(text, patches[p].from) != 0) continue;
        if (strlen(patches[p].to) <= strlen(patches[p].from))
            strcpy(text, patches[p].to);
        else
            dw->strg.strings[i] = (char *) patches[p].to;
        return true;
    }
    return false;
}

void ofPatchStrings(DataWin *dw) {
    if (dw == NULL || dw->strg.strings == NULL) return;

    unsigned patched = 0;
    for (uint32_t i = 0; i < dw->strg.count; i++) {
        char *text = (char *) dw->strg.strings[i];
        if (text == NULL || (text[0] != '[' && text[0] != 'F' && text[0] != '\\')) continue;

        if (patchOne(dw, i, text, g_patches, OF_PATCH_COUNT) || patchOne(dw, i, text, ofGame.strings, ofGame.stringCount))
            patched++;
    }
    logInfo("Strings: %u key prompts renamed for the Pocket's buttons\n", patched);
    if (ofGame.patchCode != NULL) ofGame.patchCode(dw);
}
