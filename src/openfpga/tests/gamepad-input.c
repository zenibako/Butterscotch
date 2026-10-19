#include "of_gamepad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    RunnerGamepadState *gp = RunnerGamepad_create();
    ofGamepadPoll(gp, 0, false);
    assert(RunnerGamepad_isConnected(gp, 0));
    assert(!gp->slots[0].connectedPrev);
    assert(RunnerGamepad_getDeviceCount(gp) == 1);
    RunnerGamepad_beginFrame(gp);
    ofGamepadPoll(gp, OF_BTN_A | OF_BTN_UP, false);
    assert(gp->slots[0].connectedPrev);
    assert(RunnerGamepad_getDeviceCount(gp) == 1);
    assert(RunnerGamepad_buttonCheckPressed(gp, 0, GP_FACE1));
    assert(RunnerGamepad_buttonValue(gp, 0, GP_FACE1) == 1);
    assert(RunnerGamepad_buttonCheck(gp, 0, GP_PADU));
    assert(RunnerGamepad_axisValue(gp, 0, GP_AXIS_LV) == -1);
    RunnerGamepad_beginFrame(gp);
    ofGamepadPoll(gp, OF_BTN_A, false);
    assert(!RunnerGamepad_buttonCheckPressed(gp, 0, GP_FACE1));
    assert(RunnerGamepad_buttonCheckReleased(gp, 0, GP_PADU));
    ofGamepadPoll(gp, 0, false); /* Debug chords release held gameplay. */
    assert(RunnerGamepad_buttonCheckReleased(gp, 0, GP_FACE1));
    ofGamepadPoll(gp, OF_BTN_B, false);
    assert(RunnerGamepad_buttonCheckPressed(gp, 0, GP_FACE2));
    ofGamepadPoll(gp, OF_BTN_X, false);
    assert(RunnerGamepad_buttonCheck(gp, 0, GP_FACE4));
    ofGamepadPoll(gp, OF_BTN_X | OF_BTN_Y, false);
    ofGamepadPoll(gp, OF_BTN_Y, false);
    assert(RunnerGamepad_buttonCheck(gp, 0, GP_FACE4));
    assert(!RunnerGamepad_buttonCheckReleased(gp, 0, GP_FACE4));
    ofGamepadPoll(gp, OF_BTN_START, false);
    assert(RunnerGamepad_buttonCheck(gp, 0, GP_START));
    assert(RunnerGamepad_buttonCheck(gp, 0, GP_FACE1));
    ofGamepadPoll(gp, OF_BTN_SELECT | OF_BTN_L1 | OF_BTN_R1, false);
    for (int b=0;b<GP_BUTTON_COUNT;b++) assert(!gp->slots[0].buttonDown[b]);
    gp->slots[0].deadzone = .25f;
    gp->slots[0].triggerThreshold = .7f;
    ofGamepadPoll(gp, OF_BTN_B | OF_BTN_LEFT, true); /* Restore/menu rebase. */
    assert(RunnerGamepad_buttonCheck(gp, 0, GP_FACE2));
    for (int b=0;b<GP_BUTTON_COUNT;b++) {
        assert(!gp->slots[0].buttonPressed[b] && !gp->slots[0].buttonReleased[b]);
        assert(gp->slots[0].buttonDown[b] == gp->slots[0].buttonDownPrev[b]);
    }
    assert(gp->slots[0].deadzone == .25f && gp->slots[0].triggerThreshold == .7f);
    ofGamepadPoll(gp, OF_BTN_LEFT | OF_BTN_RIGHT | OF_BTN_UP | OF_BTN_DOWN, false);
    assert(RunnerGamepad_axisValue(gp, 0, GP_AXIS_LH) == 0);
    assert(RunnerGamepad_axisValue(gp, 0, GP_AXIS_LV) == 0);
    assert(ofGamepadScriptButtons("3:A*5,4:B,9:E",4) == (OF_BTN_A|OF_BTN_B));
    assert(ofGamepadScriptButtons("3:A*5,4:B,9:E",8) == 0);
    assert(ofGamepadScriptButtons("3:A*5,4:B,9:E",9) == OF_BTN_START);
    uint32_t blocked = 0;
    assert(ofGamepadFilterButtons(&blocked, OF_BTN_A | OF_BTN_DOWN, true) == 0);
    assert(ofGamepadFilterButtons(&blocked, OF_BTN_A, false) == 0);
    assert(ofGamepadFilterButtons(&blocked, 0, false) == 0);
    assert(ofGamepadFilterButtons(&blocked, OF_BTN_A, false) == OF_BTN_A);
    RunnerGamepad_free(gp);
    puts("Pocket gamepad: connection, actions, edges, axes and rebase passed");
    return 0;
}
