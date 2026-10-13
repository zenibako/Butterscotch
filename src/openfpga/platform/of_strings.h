#ifndef OF_STRINGS_H
#define OF_STRINGS_H

#include "data_win.h"

/* Renames keyboard keys in the game's on-screen prompts to Pocket buttons. */
void ofPatchStrings(DataWin *dw);
void ofPatchInstructions(DataWin *dw);

#endif /* OF_STRINGS_H */
