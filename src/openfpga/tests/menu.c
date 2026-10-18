/* Asset-free exercise of the real menu, font rendering and scripted input. */
#include "of_menu.h"
#include "of_font.h"
#include "of_perf.h"
#include "runner.h"
#include "sw_renderer_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t *shown;
static int width, height, choices, direction;
static bool choose(int step) { choices++; direction = step; return false; }
static const char *value(void) { return "On"; }
static void present(const uint16_t *fb) { memcpy(shown, fb, (size_t)width * (size_t)height * sizeof(*fb)); }
static void run(const OfMenu *menu, const uint16_t *background, const char *script) {
    ofMenuScript(script);
    assert(ofMenuRun(menu, background, width, height));
}
static size_t greyPixels(void) {
    size_t count = 0;
    /* Descriptions sit above the hints; grey text must stay inside the box. */
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        if (shown[y * width + x] != 0x4210) continue;
        assert(x >= 8 * (width / 320) && x < width - 8 * (width / 320));
        assert(y >= 8 * (width / 320) && y < height - 8 * (width / 320));
        count++;
    }
    return count;
}
static void dump(const char *path) {
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; i++) {
        unsigned p = shown[i];
        fputc((int)(((p >> 10) & 31) * 255 / 31), f);
        fputc((int)(((p >> 5) & 31) * 255 / 31), f);
        fputc((int)((p & 31) * 255 / 31), f);
    }
    assert(fclose(f) == 0);
}
int main(int argc, char **argv) {
    int fontHeight = argc > 1 ? atoi(argv[1]) : 16;
    bool fallback = fontHeight == 0;
    if (fallback) fontHeight = 12;
    Runner runner = {0}; SWRenderer swr = {0}; DataWin data = {0};
    Font font = {0}; FontGlyph glyphs[95] = {{0}}; TexturePageItem item = {0};
    SWTexture texture = {0}; SWTexture *pages[] = {&texture};
    runner.dataWin = &data; runner.renderer = &swr.base;
    swr.base.dataWin = &data; swr.totalTextureCount = 1; swr.textures = pages;
    data.font.count = data.tpag.count = 1; data.font.fonts = &font; data.tpag.items = &item;
    font.name = "fnt_main"; font.present = true; font.tpagIndex = 0;
    font.maxGlyphHeight = (uint16_t)fontHeight; font.glyphCount = 95; font.glyphs = glyphs;
    texture.width = 95 * 8; texture.height = fontHeight;
    texture.buffer = calloc((size_t)texture.width * (size_t)fontHeight, sizeof(*texture.buffer)); assert(texture.buffer);
    for (int i = 0; i < 95; i++) {
        char text[] = {(char)(' ' + i), 0};
        glyphs[i].character = (uint16_t)(' ' + i); glyphs[i].sourceX = (uint16_t)(i * 8);
        glyphs[i].sourceWidth = glyphs[i].shift = 8; glyphs[i].sourceHeight = (uint16_t)fontHeight;
        /* The log font has descenders beyond its nominal 12px cell. */
        uint16_t ink[8 * 32] = {0};
        ofPerfDrawLogText(ink, 8, 32, 0, 0, text, 0xffff);
        for (int y = 0; y < fontHeight; y++) for (int x = 0; x < 8; x++)
            texture.buffer[y * texture.width + i * 8 + x] = ink[(y * 16 / fontHeight) * 8 + x];
    }
    Font_buildGlyphLUT(&font); ofFontSetRunner(fallback ? NULL : &runner);
    OfMenuRow rows[10];
    for (int i = 0; i < 10; i++) rows[i] = (OfMenuRow){"Setting", value, choose, NULL, NULL};
    rows[0].label = "Save log to SD card"; rows[0].value = NULL;
    rows[0].description = "Writes the debug log. Quit through the Analogue menu to keep it.";
    rows[1].description = "";
    rows[9].description = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    OfMenuPage debug = {"DEBUG", rows, 10};
    OfMenuRow root[] = {{"Debug", NULL, NULL, &debug, "Open diagnostic tools."}};
    OfMenu menu = {{"BUTTERSCOTCH", root, 1}, present, NULL};
    for (int scale = 1; scale <= 2; scale++) {
        width = 320 * scale; height = 240 * scale;
        size_t bytes = (size_t)width * (size_t)height * sizeof(*shown);
        shown = malloc(bytes); uint16_t *background = calloc(1, bytes); assert(shown && background);
        run(&menu, background, "AD"); size_t empty = greyPixels();
        uint16_t *emptyFrame = malloc(bytes); assert(emptyFrame);
        memcpy(emptyFrame, shown, bytes);
        rows[1].description = NULL;
        run(&menu, background, "AD"); assert(memcmp(shown, emptyFrame, bytes) == 0);
        rows[1].description = "First line.\nSecond line.";
        run(&menu, background, "AD"); assert(greyPixels() > empty);
        rows[1].description = "   ";
        run(&menu, background, "AD"); assert(memcmp(shown, emptyFrame, bytes) == 0);
        rows[1].description = "";
        free(emptyFrame);
        run(&menu, background, "A"); assert(greyPixels() > empty);
        if (argc > 2 && scale == 1) dump(argv[2]);
        run(&menu, background, "AU"); assert(greyPixels() > empty); /* wrapped last row scrolls into view */
        run(&menu, background, "AUD"); assert(greyPixels() > empty); /* wrap to first */
        choices = 0; run(&menu, background, "ADLRA"); assert(choices == 3 && direction == 0);
        run(&menu, background, "AB"); assert(greyPixels() > 0); /* back restores parent */
        const char *exits[] = {"S", "E", "B", "AS", "AE", "ABB"};
        for (size_t i = 0; i < sizeof(exits) / sizeof(exits[0]); i++) {
            run(&menu, background, exits[i]); assert(memcmp(shown, background, bytes) == 0);
        }
        free(shown); free(background);
    }
    puts("menu: description rendering, scrolling, actions and exits passed at both resolutions");
    return 0;
}
