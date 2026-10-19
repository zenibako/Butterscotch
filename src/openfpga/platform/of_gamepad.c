#include "of_gamepad.h"
#include <stdlib.h>
#include <string.h>

/* One built-in controller. X/Y and Start retain the port's menu/confirm
 * aliases. Select and shoulders belong to the port, never to gameplay. */
void ofGamepadPoll(RunnerGamepadState *gp, uint32_t buttons, bool rebase) {
    GamepadSlot *slot = &gp->slots[0];
    memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDown));
    memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
    memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
    memset(slot->axisValue, 0, sizeof(slot->axisValue));
    slot->connected = true;
    slot->jid = 0;
    strcpy(slot->description, "Analogue Pocket");
    strcpy(slot->guid, "openfpga-pocket");
    gp->connectedCount = 1; /* beginFrame clears this every frame. */
    slot->buttonDown[0] = (buttons & (OF_BTN_A | OF_BTN_START)) != 0;
    slot->buttonDown[1] = (buttons & OF_BTN_B) != 0;
    slot->buttonDown[3] = (buttons & (OF_BTN_X | OF_BTN_Y)) != 0;
    slot->buttonDown[9] = (buttons & OF_BTN_START) != 0;
    slot->buttonDown[12] = (buttons & OF_BTN_UP) != 0;
    slot->buttonDown[13] = (buttons & OF_BTN_DOWN) != 0;
    slot->buttonDown[14] = (buttons & OF_BTN_LEFT) != 0;
    slot->buttonDown[15] = (buttons & OF_BTN_RIGHT) != 0;
    /* Undertale's desktop joystick path reads axes as well as the POV. */
    slot->axisValue[0] = (float)slot->buttonDown[15] - (float)slot->buttonDown[14];
    slot->axisValue[1] = (float)slot->buttonDown[13] - (float)slot->buttonDown[12];
    for (int b = 0; b < GP_BUTTON_COUNT; b++) {
        bool down = slot->buttonDown[b];
        slot->buttonValue[b] = down ? 1.0f : 0.0f;
        slot->buttonPressed[b] = !rebase && down && !slot->buttonDownPrev[b];
        slot->buttonReleased[b] = !rebase && !down && slot->buttonDownPrev[b];
    }
    if (rebase) {
        memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDown));
        slot->connectedPrev = true;
    }
}

/* Same frame/hold notation as BS_SCRIPT, but physical Pocket buttons.
 * Aggregate held intervals so overlapping aliases cannot release each other. */
uint32_t ofGamepadScriptButtons(const char *script, int frame) {
    uint32_t buttons = 0;
    if (script == NULL) return 0;
    for (const char *p = script; *p; ) {
        int at = atoi(p);
        const char *colon = strchr(p, ':');
        if (colon == NULL || !colon[1]) break;
        uint32_t button = 0;
        switch (colon[1]) {
            case 'U': button = OF_BTN_UP; break;
            case 'D': button = OF_BTN_DOWN; break;
            case 'L': button = OF_BTN_LEFT; break;
            case 'R': button = OF_BTN_RIGHT; break;
            case 'A': button = OF_BTN_A; break;
            case 'B': button = OF_BTN_B; break;
            case 'X': button = OF_BTN_X; break;
            case 'Y': button = OF_BTN_Y; break;
            case 'E': button = OF_BTN_START; break;
        }
        int hold = colon[2] == '*' ? atoi(colon + 3) : 2;
        if (frame >= at && frame - at < hold) buttons |= button;
        const char *comma = strchr(colon, ',');
        if (comma == NULL) break;
        p = comma + 1;
    }
    return buttons;
}

uint32_t ofGamepadFilterButtons(uint32_t *blocked, uint32_t buttons, bool suppress) {
    *blocked &= buttons;
    if (suppress) *blocked |= buttons;
    return buttons & ~*blocked;
}
