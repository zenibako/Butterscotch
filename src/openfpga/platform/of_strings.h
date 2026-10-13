#ifndef OF_STRINGS_H
#define OF_STRINGS_H

#include "data_win.h"

/* Renames keyboard keys in the game's on-screen prompts to Pocket buttons,
 * then makes any change to the game's code its game_<name>.c asks for. */
void ofPatchStrings(DataWin *dw);

#endif /* OF_STRINGS_H */
