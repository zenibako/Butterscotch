/*
 * The port's menu (Select), drawn in the game's own font over the paused
 * game in the style of the game's own boxes: white text in a black box with
 * a white border, the chosen row in yellow with the red soul beside it.
 *
 *   Up/Down   choose a row (held, it repeats)
 *   Left/Right  change a setting
 *   A         change a setting, or do what the row says
 *   B, Select, Start  close
 *
 * The layout is worked out at 320x240 and doubled for a 640x480 frame, the
 * way utFontScale does the text, and rows scroll if the font is too tall for
 * all of them.
 */

#include "ut_menu.h"

#include "of.h"
#include "ut_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UT_WHITE  0x7FFF
#define UT_YELLOW 0x7FE0
#define UT_GREY   0x4210
#define UT_RED    0x7C00
#define UT_BLACK  0x0000

/* Sizes at 320x240. */
#define UT_MENU_MARGIN 8
#define UT_MENU_BORDER 3
#define UT_MENU_PAD    6
#define UT_SOUL_W      8
#define UT_SOUL_H      7
/* Held Up or Down repeats after this many frames, then every few. */
#define UT_REPEAT_DELAY 18
#define UT_REPEAT_EVERY 5
#define UT_FRAME_MICROS 16000

/* The soul, one row per byte, leftmost pixel in the top bit. */
static const uint8_t g_soul[UT_SOUL_H] = { 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18 };

typedef enum { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_SELECT, BTN_START, BTN_COUNT } MenuButton;

static const uint32_t g_buttons[BTN_COUNT] = {
    OF_BTN_UP, OF_BTN_DOWN, OF_BTN_LEFT, OF_BTN_RIGHT, OF_BTN_A, OF_BTN_B, OF_BTN_SELECT, OF_BTN_START,
};

#ifdef OF_PC
static const char *g_script = NULL;

void utMenuScript(const char *moves) {
    g_script = moves;
}
#endif

static void fillRect(uint16_t *fb, int width, int height, int x, int y, int w, int h, uint16_t color) {
    for (int py = y < 0 ? 0 : y; py < y + h && py < height; py++)
        for (int px = x < 0 ? 0 : x; px < x + w && px < width; px++) fb[py * width + px] = color;
}

static void drawSoul(uint16_t *fb, int width, int height, int x, int y, int scale) {
    for (int row = 0; row < UT_SOUL_H; row++)
        for (int col = 0; col < UT_SOUL_W; col++)
            if (g_soul[row] & (0x80 >> col)) fillRect(fb, width, height, x + col * scale, y + row * scale, scale, scale, UT_RED);
}

typedef struct {
    int cursor, scroll;
    int heldFrames; /* how long Up or Down has been held */
} MenuState;

static void draw(const UtMenu *menu, const MenuState *state, uint16_t *fb, const uint16_t *game, int width, int height) {
    /* The game behind at a quarter of its brightness. */
    for (size_t i = 0, n = (size_t) width * (size_t) height; i < n; i++) fb[i] = (uint16_t) ((game[i] >> 2) & 0x1CE7);

    int s = utFontScale(width);
    int line = utFontLineHeight(s);
    int margin = UT_MENU_MARGIN * s, border = UT_MENU_BORDER * s, pad = UT_MENU_PAD * s;
    int chrome = 2 * border + 2 * pad + line /* title */ + line / 2 + line / 2 + 2 * line /* status, help */;
    int visible = (height - 2 * margin - chrome) / line;
    if (visible > menu->count) visible = menu->count;
    if (visible < 1) visible = 1;

    int boxW = width - 2 * margin;
    int boxH = chrome + visible * line;
    int boxX = margin, boxY = (height - boxH) / 2;
    fillRect(fb, width, height, boxX, boxY, boxW, boxH, UT_WHITE);
    fillRect(fb, width, height, boxX + border, boxY + border, boxW - 2 * border, boxH - 2 * border, UT_BLACK);

    int left = boxX + border + pad, right = boxX + boxW - border - pad;
    int y = boxY + border + pad;
    utFontDraw(fb, width, height, left, y, menu->title, UT_WHITE, s);
    y += line + line / 2;

    int textX = left + (UT_SOUL_W + 4) * s;
    for (int i = 0; i < visible; i++) {
        int index = state->scroll + i;
        const UtMenuRow *row = &menu->rows[index];
        bool chosen = index == state->cursor;
        if (chosen) drawSoul(fb, width, height, left, y + (line - UT_SOUL_H * s) / 2, s);
        utFontDraw(fb, width, height, textX, y, row->label, chosen ? UT_YELLOW : UT_WHITE, s);
        if (row->value != NULL) {
            const char *value = row->value();
            utFontDraw(fb, width, height, right - utFontWidth(value, s), y, value, chosen ? UT_YELLOW : UT_WHITE, s);
        }
        y += line;
    }
    /* More rows above or below: a mark at the right edge. */
    if (state->scroll > 0) fillRect(fb, width, height, right - 2 * s, y - visible * line - line / 4, 4 * s, s, UT_GREY);
    if (state->scroll + visible < menu->count) fillRect(fb, width, height, right - 2 * s, y + line / 8, 4 * s, s, UT_GREY);

    y += line / 2;
    if (menu->status != NULL) {
        char status[64];
        menu->status(status, sizeof(status));
        utFontDraw(fb, width, height, left, y, status, UT_GREY, s);
    }
    y += line;
    utFontDraw(fb, width, height, left, y, "A: change   B: back", UT_GREY, s);
}

/* Which buttons went down this frame. */
static unsigned readButtons(bool *scriptDone) {
    unsigned pressed = 0;
    *scriptDone = false;
#ifdef OF_PC
    if (g_script != NULL) {
        static const char keys[BTN_COUNT] = { 'U', 'D', 'L', 'R', 'A', 'B', 'S', 'E' };
        if (*g_script == '\0') {
            *scriptDone = true;
            return 0;
        }
        for (int b = 0; b < BTN_COUNT; b++)
            if (*g_script == keys[b]) pressed |= 1u << b;
        g_script++;
        return pressed;
    }
#endif
    of_input_poll();
    for (int b = 0; b < BTN_COUNT; b++)
        if (of_btn_pressed(g_buttons[b])) pressed |= 1u << b;
    return pressed;
}

static bool held(MenuButton button) {
#ifdef OF_PC
    if (g_script != NULL) return false;
#endif
    return of_btn(g_buttons[button]);
}

bool utMenuRun(const UtMenu *menu, const uint16_t *frame, int width, int height) {
    size_t pixels = (size_t) width * (size_t) height;
    /* A copy: `frame` can be one of the display's buffers, which the menu
     * draws into. */
    uint16_t *game = (uint16_t *) malloc(pixels * sizeof(uint16_t));
    uint16_t *fb = (uint16_t *) malloc(pixels * sizeof(uint16_t));
    if (game == NULL || fb == NULL || menu->count <= 0) {
        free(game);
        free(fb);
        return false;
    }
    memcpy(game, frame, pixels * sizeof(uint16_t));

    MenuState state = { 0, 0, 0 };
    bool open = true;
    while (open) {
        bool scriptDone;
        unsigned pressed = readButtons(&scriptDone);
        if (scriptDone) break;

        int step = 0;
        if (pressed & (1u << BTN_UP)) step = -1;
        if (pressed & (1u << BTN_DOWN)) step = 1;
        if (held(BTN_UP) || held(BTN_DOWN)) {
            if (++state.heldFrames >= UT_REPEAT_DELAY && (state.heldFrames - UT_REPEAT_DELAY) % UT_REPEAT_EVERY == 0)
                step = held(BTN_UP) ? -1 : 1;
        } else {
            state.heldFrames = 0;
        }
        if (step != 0) state.cursor = (state.cursor + step + menu->count) % menu->count;

        const UtMenuRow *row = &menu->rows[state.cursor];
        int direction = (pressed & (1u << BTN_LEFT)) ? -1 : (pressed & (1u << BTN_RIGHT)) ? 1 : 0;
        if (pressed & (1u << BTN_A)) {
            if (row->choose(0)) open = false;
        } else if (direction != 0 && row->value != NULL) {
            if (row->choose(direction)) open = false;
        }
        if (pressed & ((1u << BTN_B) | (1u << BTN_SELECT) | (1u << BTN_START))) open = false;

        /* Keep the chosen row in view. */
        int s = utFontScale(width), line = utFontLineHeight(s);
        int chrome = 2 * (UT_MENU_BORDER + UT_MENU_PAD) * s + 3 * line + 2 * (line / 2);
        int visible = (height - 2 * UT_MENU_MARGIN * s - chrome) / line;
        if (visible < 1) visible = 1;
        if (state.cursor < state.scroll) state.scroll = state.cursor;
        if (state.cursor >= state.scroll + visible) state.scroll = state.cursor - visible + 1;

        if (!open) break;
        draw(menu, &state, fb, game, width, height);
        menu->present(fb);
        if (menu->idle != NULL) menu->idle();
#ifdef OF_PC
        if (g_script != NULL) continue;
#endif
        usleep(UT_FRAME_MICROS);
    }
#ifdef OF_PC
    if (g_script != NULL) {
        /* The script ran out with the menu open: show it as it stands. */
        g_script = NULL;
        if (open) {
            draw(menu, &state, fb, game, width, height);
            menu->present(fb);
            free(game);
            free(fb);
            return true;
        }
    }
#endif
    /* The game's picture back: a paused game shows no new one by itself. */
    menu->present(game);
    free(game);
    free(fb);
    return true;
}
