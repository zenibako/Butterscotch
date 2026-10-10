#ifndef UT_MENU_H
#define UT_MENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The port's menu: Select opens it over the paused game. It lists the
 * debug and performance settings, each a row; see of_platform.c for the
 * rows themselves. This module draws the menu and moves through it. */

typedef struct {
    const char *label;
    /* The setting's value as shown ("On", "Speed"), or NULL for a row that
     * does something instead. */
    const char *(*value)(void);
    /* A on the row (direction 0), or Left/Right on a setting (-1, +1).
     * Returns true when the menu should close. */
    bool (*choose)(int direction);
} UtMenuRow;

typedef struct {
    const char *title;
    const UtMenuRow *rows;
    int count;
    /* A line under the rows, written fresh each frame (the frame times). */
    void (*status)(char *out, size_t size);
    /* Shows a finished frame of `width` x `height` pixels. */
    void (*present)(const uint16_t *fb);
    /* Called about every 16 ms while the menu is up, to keep the sound fed. */
    void (*idle)(void);
} UtMenu;

/* Shows the menu over `frame` (the picture last shown, `width` x `height`)
 * until it is closed, then shows `frame` again and returns. Input is read
 * here; the game does not step meanwhile. Returns false without showing
 * anything if there is no memory for it. */
bool utMenuRun(const UtMenu *menu, const uint16_t *frame, int width, int height);

#ifdef OF_PC
/* Desktop test aid: the next utMenuRun takes these buttons from `moves`
 * instead of the pad, one per frame (U D L R, A B), draws the menu after the
 * last one, and returns. */
void utMenuScript(const char *moves);
#endif

#endif
