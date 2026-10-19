#ifndef OF_GAMEPAD_H
#define OF_GAMEPAD_H
#include "runner_gamepad.h"
#include "of.h"
void ofGamepadPoll(RunnerGamepadState *gp, uint32_t buttons, bool rebase);
uint32_t ofGamepadFilterButtons(uint32_t *blocked, uint32_t buttons, bool suppress);
uint32_t ofGamepadScriptButtons(const char *script, int frame);
#endif
