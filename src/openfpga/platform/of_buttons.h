#ifndef OF_BUTTONS_H
#define OF_BUTTONS_H

#include "data_win.h"
#include "renderer.h"

/* Private-use codepoints: one game character each, never a keyboard letter.
 * Keep the first four in physical order: right, bottom, top, left. */
#define OF_ICON_A      "\xee\x80\x80"
#define OF_ICON_B      "\xee\x80\x81"
#define OF_ICON_X      "\xee\x80\x82"
#define OF_ICON_Y      "\xee\x80\x83"
#define OF_ICON_START  "\xee\x80\x84"
#define OF_ICON_SELECT "\xee\x80\x85"
#define OF_ICON_L      "\xee\x80\x86"
#define OF_ICON_R      "\xee\x80\x87"
#define OF_ICON_DPAD   "\xee\x80\x88"
#define OF_ICON_UP     "\xee\x80\x89"
#define OF_ICON_DOWN   "\xee\x80\x8a"
#define OF_ICON_LEFT   "\xee\x80\x8b"
#define OF_ICON_RIGHT  "\xee\x80\x8c"
#define OF_ICON_FIRST 0xe000u
#define OF_ICON_COUNT 13u
#define OF_ICON_SIZE 13

void ofButtonsInstallFonts(DataWin *dw);
const char *ofButtonForKey(int32_t key);
bool ofButtonPixel(uint16_t codepoint, int x, int y);
bool ofButtonCodepoint(uint16_t codepoint);
void ofButtonDraw(Renderer *renderer, uint16_t codepoint, float x, float y,
                  int size, uint32_t color, float alpha);
#endif
