#include "sw_renderer_private.h"
#include "sw_drawing.h"
#include "of_game.h"
#include <assert.h>
#include <stdio.h>

static void battleUiInstall(Runner *runner) { ofGame.battleUiInstall(runner); }
static void battleUiUpdate(Runner *runner, bool speed) {
    SWRenderer_setContrastEnabled(runner->renderer, speed && ofGame.battleUiActive(runner));
}

enum { SIZE = 16, TEX = 8 };
static unsigned checked;

static void setup(SWRenderer *swr, SWTexture *texture, DataWin *data, SWTexture **pages) {
    memset(swr, 0, sizeof(*swr));
    Renderer *model = SWRenderer_create();
    swr->base.vtable = model->vtable;
    free(model);
    swr->base.dataWin = data;
    swr->fb = swr->mainFb = calloc(SIZE * SIZE, sizeof(uintpixel_t));
    assert(swr->fb != NULL);
    swr->width = swr->height = swr->mainWidth = swr->mainHeight = SIZE;
    swr->fbPitch = swr->mainPitch = SIZE;
    swr->portW = swr->portH = swr->maxX = swr->maxY = SIZE;
    swr->scaleX = swr->scaleY = 0.5f;
    swr->blendMode = bm_normal;
    swr->writeMask = WRITE_MASK_ALL;
    swr->textures = pages;
    swr->totalTextureCount = 1;
    swr->contrastEnabled = true;
    texture->width = texture->height = TEX;
    texture->immutable = true;
    texture->buffer = calloc(TEX * TEX, sizeof(uintpixel_t));
    assert(texture->buffer != NULL);
    swrFavorSpeed = swrSmoothMinify = true;
}

static void finish(SWRenderer *swr, SWTexture *texture) {
    swrOverlayFlush(swr);
    free(swr->fb);
    free(swr->hintedGlyphs);
    free(swr->contrastItems); free(swr->contrastFonts);
    swrFreeButtonCache(texture);
    free(texture->halfBuffer); free(texture->rowBounds); free(texture->buffer);
}

static void clear(SWRenderer *swr, uintpixel_t background) {
    swrOverlayFlush(swr);
    swrFillPixels(swr->fb, SIZE * SIZE, background);
}

/* Known quarter/half/three-quarter coverage, every possible position of thin ink.
 * UI mixes center and box sampling equally; black bubble ink averages that
 * coverage with the bold curve. The legacy glyph kind keeps the bold curve.
 * expected channels are a plain linear blend, independent of the renderer's masks. */
static void coverage(unsigned pattern, int kind, bool warm, int phaseX, int phaseY, float opacity, uintpixel_t ink) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    unsigned count = 0;
    for (int bit = 0; bit < 4; ++bit) count += (pattern >> bit) & 1;
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x)
            texture.buffer[y * TEX + x] = pattern & (1u << ((y & 1) * 2 + (x & 1))) ? ink :
                                         kind == SWR_CONTRAST_BUTTON ? 0x8000 : 0;
    if (warm) swrDrawSprite(&swr.base, 0, 0, TEX, TEX, &texture, 0, 0, TEX, TEX, 0xffffff, 1);
    bool bubble = kind == SWR_CONTRAST_BUBBLE_GLYPH;
    uintpixel_t background = bubble ? 0xffff : 0x8000;
    clear(&swr, background);
    uintpixel_t source[TEX * TEX], half[TEX * TEX];
    memcpy(source, texture.buffer, sizeof(source));
    uintpixel_t *halfBefore = texture.halfBuffer;
    size_t halfBytes = ((TEX + texture.halfPhaseX + 1) / 2) * ((TEX + texture.halfPhaseY + 1) / 2) * sizeof(uintpixel_t);
    if (halfBefore) memcpy(half, halfBefore, halfBytes);
    swrDrawContrastSprite(&swr.base, 4, 4, 4, 4, &texture, phaseX, phaseY, 4, 4, bubble ? 0 : 0xffffff, opacity, kind);
    swrOverlayFlush(&swr);
    unsigned strength = (unsigned) lroundf(powf((float) count / 4.0f, 0.2f) * 256.0f);
    unsigned alpha = (unsigned) (opacity * 256);
    unsigned center = (((phaseY + 1) & 1) << 1) | ((phaseX + 1) & 1);
    unsigned centered = (pattern & (1u << center)) != 0;
    unsigned amount = kind == SWR_CONTRAST_GLYPH ? (alpha * strength) >> 8 : ((4 * centered + count) * alpha) / 8;
    if (bubble) amount = (amount + alpha * strength / 256) / 2;
    unsigned r = bubble ? 31 * (256 - amount) / 256 : ((ink >> 10) & 31) * amount / 256;
    unsigned g = bubble ? r : ((ink >> 5) & 31) * amount / 256;
    unsigned b = bubble ? r : (ink & 31) * amount / 256;
    uintpixel_t expected = (uintpixel_t) (0x8000 | (r << 10) | (g << 5) | b);
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < SIZE; ++x) {
            uintpixel_t want = x >= 2 && x < 4 && y >= 2 && y < 4 ? expected : background;
            if (swr.fb[y * SIZE + x] != want) {
                fprintf(stderr, "coverage pattern=%u kind=%d warm=%d phase=%d/%d opacity=%g at=%d/%d: %04x != %04x\n",
                        pattern, kind, warm, phaseX, phaseY, (double) opacity, x, y, swr.fb[y * SIZE + x], want);
                abort();
            }
        }
    assert(memcmp(source, texture.buffer, sizeof(source)) == 0);
    if (halfBefore) assert(texture.halfBuffer == halfBefore && memcmp(half, halfBefore, halfBytes) == 0);
    ++checked; finish(&swr, &texture);
}

/* Fallback draws and subsequent ordinary draws must match ordinary sampling. */
static void isolation(int guard, int kind) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    for (int i = 0; i < TEX * TEX; ++i) texture.buffer[i] = i % 3 ? 0 : 0xffff;
    float width = TEX, height = TEX;
    uint32_t tintColor = kind == SWR_CONTRAST_BUBBLE_GLYPH && guard != 10 ? 0 : 0x80c0ff;
    switch (guard) {
        case 0: swr.contrastEnabled = false; break;
        case 1: swrFavorSpeed = false; break;
        case 2: swrSmoothMinify = false; break;
        case 3: texture.immutable = false; break;
        case 4: swr.drawingToSurface = true; break;
        case 5: swr.blendMode = bm_add; break;
        case 6: width *= 1.5f; break;
        case 7: width = -width; break;
        case 8: swr.scaleX = swr.scaleY = 1; break;
        default: break;
    }
    uintpixel_t expected[SIZE * SIZE];
    clear(&swr, 0x9084);
    swrDrawSprite(&swr.base, 5, 5, width, height, &texture, 0, 0, TEX, TEX, tintColor, 0.5f);
    swrOverlayFlush(&swr); memcpy(expected, swr.fb, sizeof(expected));
    clear(&swr, 0x9084);
    if (guard == 9) {
        swrDrawContrastSprite(&swr.base, 5, 5, width, height, &texture, 0, 0, TEX, TEX, tintColor, 1, kind);
        clear(&swr, 0x9084);
        swrDrawSprite(&swr.base, 5, 5, width, height, &texture, 0, 0, TEX, TEX, tintColor, 0.5f);
    } else {
        swrDrawContrastSprite(&swr.base, 5, 5, width, height, &texture, 0, 0, TEX, TEX, tintColor, 0.5f, kind);
    }
    swrOverlayFlush(&swr);
    assert(memcmp(expected, swr.fb, sizeof(expected)) == 0);
    ++checked; finish(&swr, &texture);
}

static void metadata(void) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    TexturePageItem items[7] = {{0}};
    int32_t targets[] = {0, 1}, other = 2, hp = 3, kris = 4, ralsei = 5, susie = 6;
#ifdef GAME_DELTARUNE
    const char *buttonName = "spr_btfight", *bubbleName = "fnt_dotumche", *whiteName = "fnt_mainbig";
#else
    const char *buttonName = "spr_fightbt", *bubbleName = "fnt_curs", *whiteName = "fnt_ja_curs";
#endif
    Sprite sprites[] = {{.name = buttonName, .textureCount = 2, .tpagIndices = targets},
                        {.name = "enemy", .textureCount = 1, .tpagIndices = &other},
                        {.name = "spr_hpname", .textureCount = 1, .tpagIndices = &hp},
#ifdef GAME_DELTARUNE
                        {.name = "spr_bnamekris", .textureCount = 1, .tpagIndices = &kris},
                        {.name = "spr_bnameralsei", .textureCount = 1, .tpagIndices = &ralsei},
                        {.name = "spr_bnamesusie", .textureCount = 1, .tpagIndices = &susie},
#endif
    };
    Font fonts[] = {{.name = bubbleName}, {.name = "unrelated"}, {.name = whiteName}};
    data.tpag.count = 7; data.tpag.items = items;
    data.sprt.count = sizeof(sprites) / sizeof(sprites[0]); data.sprt.sprites = sprites;
    data.font.count = 3; data.font.fonts = fonts;
    Runner runner = {0}; Room room = {.name = "room_floweybattle"};
    runner.renderer = &swr.base; runner.dataWin = &data; runner.currentRoom = &room;
#ifdef GAME_DELTARUNE
    GameObject controllerType = {.name = "obj_battlecontroller"};
    Instance controller = {.active = true};
    Instance **bucket = NULL;
    data.objt.count = 1; data.objt.objects = &controllerType;
    runner.instancesByObject = &bucket;
#endif
    battleUiInstall(&runner); battleUiUpdate(&runner, true);
#ifdef GAME_DELTARUNE
    assert(!swr.contrastEnabled);
    assert(swr.contrastItems != NULL && swr.contrastFonts != NULL);
    assert(swr.contrastItems[0] == SWR_CONTRAST_BUTTON && swr.contrastItems[1] == SWR_CONTRAST_BUTTON);
    assert(!swr.contrastItems[2] && swr.contrastItems[3] == SWR_CONTRAST_UI_GLYPH);
    assert(swr.contrastItems[kris] == SWR_CONTRAST_UI_GLYPH);
    assert(swr.contrastItems[ralsei] == SWR_CONTRAST_UI_GLYPH);
    assert(swr.contrastItems[susie] == SWR_CONTRAST_UI_GLYPH);
    assert(swr.contrastFonts[0] == SWR_FONT_BUBBLE_CONTRAST && !swr.contrastFonts[1]);
    assert(!swr.contrastFonts[2]);
    room.name = "room_field2";
    arrput(bucket, &controller);
    battleUiUpdate(&runner, true); assert(swr.contrastEnabled);
    battleUiUpdate(&runner, false); assert(!swr.contrastEnabled);
    controller.active = false;
    battleUiUpdate(&runner, true); assert(!swr.contrastEnabled);
    controller.active = true; controller.destroyed = true;
    battleUiUpdate(&runner, true); assert(!swr.contrastEnabled);
    controller.destroyed = false;
    DataWin otherData = {0};
    runner.dataWin = &otherData;
    battleUiUpdate(&runner, true); assert(!swr.contrastEnabled);
    runner.dataWin = &data;
    runner.instancesByObject = NULL;
    battleUiUpdate(&runner, true); assert(!swr.contrastEnabled);
    runner.instancesByObject = &bucket;
    controllerType.name = "other";
    battleUiInstall(&runner); battleUiUpdate(&runner, true);
    assert(!swr.contrastEnabled);
    arrfree(bucket);
#else
    assert(swr.contrastEnabled);
    assert(swr.contrastItems[0] && swr.contrastItems[1] && !swr.contrastItems[2]);
    assert(swr.contrastFonts[0] && !swr.contrastFonts[1]);
    assert(swr.contrastFonts[2] == (SWR_FONT_BUBBLE_CONTRAST | SWR_FONT_WHITE_CONTRAST));
    assert(swr.contrastItems[0] == SWR_CONTRAST_BUTTON && swr.contrastItems[3] == SWR_CONTRAST_UI_GLYPH);
    battleUiUpdate(&runner, false); assert(!swr.contrastEnabled);
    room.name = "room_intromenu";
    battleUiUpdate(&runner, true); assert(!swr.contrastEnabled);
    runner.currentRoom = NULL;
    battleUiUpdate(&runner, true); assert(!swr.contrastEnabled);
#endif
    ++checked; finish(&swr, &texture);
}

/* Transparent HUD labels must use glyph coverage, not opaque-button coverage. */
static void hudSpriteDispatch(void) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    TexturePageItem item = {.sourceWidth = 4, .sourceHeight = 4, .targetWidth = 4, .targetHeight = 4};
    int32_t target = 0;
    Sprite sprite = {.name = "spr_hpname", .textureCount = 1, .tpagIndices = &target};
    data.tpag.count = data.sprt.count = 1; data.tpag.items = &item; data.sprt.sprites = &sprite;
    const char *names[] = {"spr_hpname"};
    SWRenderer_setContrastAssets(&swr.base, NULL, 0, NULL, 0);
    SWRenderer_setContrastGlyphAssets(&swr.base, names, 1, NULL, 0);
    for (int y = 0; y < 4; y += 2)
        for (int x = 0; x < 4; x += 2) texture.buffer[y * TEX + x] = 0xffff;
    texture.buffer[TEX + 3] = 0xffff;
    clear(&swr, 0x8000);
    swr.base.vtable->drawSprite(&swr.base, 0, 4, 4, 0, 0, 1, 1, 0, 0xffffff, 1);
    swrOverlayFlush(&swr); assert(swr.fb[2 * SIZE + 2] == 0x8c63);
    assert(swr.fb[2 * SIZE + 3] == 0xdef7);
    SWRenderer_setContrastEnabled(&swr.base, false);
    clear(&swr, 0x8000);
    swr.base.vtable->drawSprite(&swr.base, 0, 4, 4, 0, 0, 1, 1, 0, 0xffffff, 1);
    swrOverlayFlush(&swr); assert(swr.fb[2 * SIZE + 2] == 0x9ce7 && swr.fb[2 * SIZE + 3] == 0xbdef);
    ++checked; finish(&swr, &texture);
}

/* A synthetic tall R must retain its counter and acquire a diagonal leg at
 * native 5x8 size. Literal rows are the desired readable output, not a filter oracle. */
static void hintedName(void) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    free(texture.buffer);
    texture.width = texture.height = 16;
    texture.buffer = calloc(256, sizeof(uintpixel_t));
    assert(texture.buffer);
    /* Deliberately ambiguous original: a stem and almost vertical right leg. */
    for (int y = 0; y < 15; ++y)
        for (int x = 0; x < 9; ++x)
            if (x < 2 || x > 6 || y < 2 || (y >= 6 && y <= 8)) texture.buffer[y * 16 + x] = 0xffff;
    uint32_t hash = 2166136261u;
    for (int y = 0; y < 15; ++y)
        for (int x = 0; x < 9; ++x) {
            unsigned pixel = texture.buffer[y * 16 + x];
            hash = (hash ^ (pixel & 255)) * 16777619u;
            hash = (hash ^ (pixel >> 8)) * 16777619u;
        }
    SWHintedGlyphAsset hint = {"synthetic_name", 9, 15, hash, 0, 5,
                              {30, 17, 17, 30, 20, 18, 17, 17}};
    TexturePageItem item = {.sourceWidth = 9, .sourceHeight = 15, .targetWidth = 9, .targetHeight = 15};
    int32_t target = 0;
    Sprite sprite = {.name = "synthetic_name", .textureCount = 1, .tpagIndices = &target};
    data.tpag.count = data.sprt.count = 1; data.tpag.items = &item; data.sprt.sprites = &sprite;
    const char *names[] = {"synthetic_name"};
    SWRenderer_setContrastAssets(&swr.base, NULL, 0, NULL, 0);
    SWRenderer_setContrastGlyphAssets(&swr.base, names, 1, NULL, 0);
    SWRenderer_setHintedGlyphAssets(&swr.base, &hint, 1);
    uintpixel_t original[256]; memcpy(original, texture.buffer, sizeof(original));
    swrDrawSprite(&swr.base, 0, 0, 16, 16, &texture, 0, 0, 16, 16, 0xffffff, 1);
    swrOverlayFlush(&swr);
    assert(texture.halfBuffer);
    uintpixel_t *halfBefore = texture.halfBuffer, half[64];
    memcpy(half, halfBefore, sizeof(half));
    static const unsigned expected[8] = {30, 17, 17, 30, 20, 18, 17, 17};
    for (int warm = 0; warm < 2; ++warm) {
        for (int clip = 0; clip < 20; ++clip) {
            swr.portX = clip & 1 ? 3 : 0; swr.portY = clip & 2 ? 3 : 0;
            swr.portW = (clip & 4 ? 6 : SIZE) - swr.portX;
            swr.portH = (clip & 8 ? 9 : SIZE) - swr.portY;
            if (clip >= 16) {
                swr.portX = clip == 17 ? 6 : 2; swr.portY = clip == 19 ? 9 : 2;
                swr.portW = clip <= 17 ? 1 : 5; swr.portH = clip >= 18 ? 1 : 8;
            }
            swr.viewX = swr.portX * 2; swr.viewY = swr.portY * 2;
            swr.maxX = swr.portX + swr.portW; swr.maxY = swr.portY + swr.portH;
            for (int opacity = 1; opacity <= 2; ++opacity) {
                clear(&swr, 0x8000);
                swr.base.vtable->drawSprite(&swr.base, 0, 4, 4, 0, 0, 1, 1, 0, 0xffffff, opacity * 0.5f);
                swrOverlayFlush(&swr);
                for (int y = 0; y < SIZE; ++y)
                    for (int x = 0; x < SIZE; ++x) {
                        bool visible = x >= swr.portX && x < swr.portX + swr.portW &&
                                       y >= swr.portY && y < swr.portY + swr.portH;
                        bool ink = x >= 2 && x < 7 && y >= 2 && y < 10 && (expected[y-2] & (1u << (6-x)));
                        uintpixel_t want = visible && ink ? (opacity == 2 ? 0xffff : 0xbdef) : 0x8000;
                        if (swr.fb[y * SIZE + x] != want) {
                            fprintf(stderr, "hinted warm=%d clip=%d opacity=%d at=%d/%d: %04x != %04x\n",
                                    warm, clip, opacity, x, y, swr.fb[y * SIZE + x], want);
                            abort();
                        }
                    }
                ++checked;
            }
        }
    }
    assert(memcmp(original, texture.buffer, sizeof(original)) == 0);
    assert(halfBefore == texture.halfBuffer && memcmp(half, texture.halfBuffer, sizeof(half)) == 0);
    SWGlyphHintCache *cached = texture.glyphHints;
    assert(cached && !cached->next && cached->valid);
    /* Every rejected configuration must match the existing selected-UI path. */
    for (int guard = 0; guard < 12; ++guard) {
        swr.portX = swr.portY = swr.viewX = swr.viewY = 0; swr.portW = swr.portH = swr.maxX = swr.maxY = SIZE;
        swr.contrastEnabled = guard != 0; swrFavorSpeed = guard != 1;
        swrSmoothMinify = guard != 2; swr.drawingToSurface = guard == 3;
        swr.scaleX = guard == 4 ? 1 : 0.5f; swr.scaleY = 0.5f;
        swr.blendMode = guard == 5 ? bm_add : bm_normal;
        texture.immutable = guard != 6;
        float scale = guard == 7 ? 2 : guard == 8 ? -1 : 1;
        float angle = guard == 9 ? 20 : 0;
        item.targetWidth = guard == 10 ? 10 : 9;
        sprite.name = guard == 11 ? "localized_name" : "synthetic_name";
        SWRenderer_setHintedGlyphAssets(&swr.base, &hint, 1);
        clear(&swr, 0x8000);
        swr.base.vtable->drawSprite(&swr.base, 0, 4, 4, 0, 0, scale, 1, angle, 0xffffff, 1);
        swrOverlayFlush(&swr);
        uintpixel_t reference[SIZE * SIZE]; memcpy(reference, swr.fb, sizeof(reference));
        SWRenderer_setHintedGlyphAssets(&swr.base, NULL, 0);
        clear(&swr, 0x8000);
        swr.base.vtable->drawSprite(&swr.base, 0, 4, 4, 0, 0, scale, 1, angle, 0xffffff, 1);
        swrOverlayFlush(&swr);
        assert(memcmp(reference, swr.fb, sizeof(reference)) == 0);
        ++checked;
    }
    SWRenderer_setHintedGlyphAssets(&swr.base, NULL, 0);
    swr.portX = swr.portY = swr.viewX = swr.viewY = 0;
    swr.portW = swr.portH = swr.maxX = swr.maxY = SIZE;
    swr.scaleX = swr.scaleY = 0.5f; swr.blendMode = bm_normal;
    swr.contrastEnabled = texture.immutable = swrFavorSpeed = swrSmoothMinify = true;
    swr.drawingToSurface = false;
    for (int phase = 0; phase < 4; ++phase) {
        swrFreeGlyphHints(&texture);
        memset(texture.buffer, 0, 256 * sizeof(uintpixel_t));
        int sx = phase & 1, sy = phase >> 1;
        for (int y = 0; y < 15; ++y)
            memcpy(texture.buffer + (sy+y)*16+sx, original+y*16, 9*sizeof(uintpixel_t));
        for (int red = 0; red < 2; ++red) {
            clear(&swr, 0x8000);
            assert(swrDrawHintedGlyph(&swr.base, 4, 4, 9, 15, &texture, sx, sy, 9, 15,
                                     red ? 0x0000ff : 0xffffff, 0.5f, &hint));
            swrOverlayFlush(&swr);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 5; ++x)
                    assert(swr.fb[(y+2)*SIZE+x+2] == ((expected[y] & (1u << (4-x))) ?
                           (red ? 0xbc00 : 0xbdef) : 0x8000));
            ++checked;
        }
        assert(texture.glyphHints && !texture.glyphHints->next);
    }
    for (int guard = 0; guard < 7; ++guard) {
        swrFreeGlyphHints(&texture);
        memcpy(texture.buffer, original, sizeof(original));
        SWHintedGlyphAsset rejected = hint;
        if (guard == 0) rejected.sourceHash ^= 1;
        if (guard == 1) rejected.patchWidth = 9;
        if (guard == 2) rejected.patchX = 5;
        if (guard == 3) texture.buffer[0] ^= 0xffff;
        if (guard == 4) {
            texture.buffer[0] = 0x83e0; // Matching fingerprint still cannot opt in colored ink.
            rejected.sourceHash = 2166136261u;
            for (int y = 0; y < 15; ++y)
                for (int x = 0; x < 9; ++x) {
                    unsigned pixel = texture.buffer[y*16+x];
                    rejected.sourceHash = (rejected.sourceHash ^ (pixel & 255)) * 16777619u;
                    rejected.sourceHash = (rejected.sourceHash ^ (pixel >> 8)) * 16777619u;
                }
        }
        clear(&swr, 0x8000);
        assert(!swrDrawHintedGlyph(&swr.base, 4, 4, 9, 15, &texture, guard == 5 ? -1 : 0,
                                  guard == 6 ? 2 : 0, 9, 15, 0xffffff, 1, &rejected));
        for (int i = 0; i < SIZE*SIZE; ++i) assert(swr.fb[i] == 0x8000);
        ++checked;
    }
    swrFreeGlyphHints(&texture);
    finish(&swr, &texture);
}

static void textDispatch(void) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    FontGlyph glyph = {.character = 'A', .sourceWidth = 4, .sourceHeight = 4, .shift = 4};
    Font fonts[2] = {{0}};
    TexturePageItem item = {.sourceWidth = TEX, .sourceHeight = TEX};
    data.font.count = 2; data.font.fonts = fonts;
    data.tpag.count = 1; data.tpag.items = &item;
    for (int i = 0; i < 2; ++i) {
        fonts[i].present = true; fonts[i].glyphs = &glyph; fonts[i].glyphCount = 1;
        fonts[i].scaleX = fonts[i].scaleY = 1; fonts[i].maxGlyphHeight = 4;
        Font_buildGlyphLUT(&fonts[i]);
    }
    fonts[0].name = "fnt_curs"; fonts[1].name = "other";
    const char *names[] = {"fnt_curs"};
    SWRenderer_setContrastAssets(&swr.base, NULL, 0, names, 1);
    for (int y = 0; y < TEX; y += 2)
        for (int x = 0; x < TEX; x += 2) texture.buffer[y * TEX + x] = 0xffff;
    texture.buffer[TEX + 3] = 0xffff;
    uintpixel_t ordinary[SIZE * SIZE];
    swr.base.drawFont = 1;
    clear(&swr, 0xffff); swrDrawText(&swr, "A", 4, 4, 1, 1, 0, 0, 1, -1);
    swrOverlayFlush(&swr); memcpy(ordinary, swr.fb, sizeof(ordinary));
    swr.base.drawFont = 0;
    clear(&swr, 0xffff); swrDrawText(&swr, "A", 4, 4, 1, 1, 0, 0, 1, -1);
    swrOverlayFlush(&swr);
    assert(swr.fb[2 * SIZE + 2] == 0xc631 && swr.fb[2 * SIZE + 3] == 0x94a5 && ordinary[2 * SIZE + 2] == 0xdef7);
    swr.base.drawFont = 1;
    clear(&swr, 0xffff); swrDrawText(&swr, "A", 4, 4, 1, 1, 0, 0, 1, -1);
    swrOverlayFlush(&swr); assert(memcmp(ordinary, swr.fb, sizeof(ordinary)) == 0);
    // A cached glyph draw must return to ordinary black sampling outside the opted-in context.
    SWRenderer_setContrastEnabled(&swr.base, false);
    swr.base.drawFont = 0;
    clear(&swr, 0xffff); swrDrawText(&swr, "A", 4, 4, 1, 1, 0, 0, 1, -1);
    swrOverlayFlush(&swr); assert(memcmp(ordinary, swr.fb, sizeof(ordinary)) == 0);
    SWRenderer_setContrastEnabled(&swr.base, true);
    // The same font's white status text is not an enemy dialogue bubble.
    swr.base.drawFont = 0;
    clear(&swr, 0x8000); swrDrawText(&swr, "A", 4, 4, 1, 1, 0, 0xffffff, 1, -1);
    swrOverlayFlush(&swr); assert(swr.fb[2 * SIZE + 2] == 0x9ce7);
    SWRenderer_setContrastGlyphAssets(&swr.base, NULL, 0, names, 1);
    clear(&swr, 0x8000); swrDrawText(&swr, "A", 4, 4, 1, 1, 0, 0xffffff, 1, -1);
    swrOverlayFlush(&swr); assert(swr.fb[2 * SIZE + 2] == 0x8c63 && swr.fb[2 * SIZE + 3] == 0xdef7);
    clear(&swr, 0x8000); swrDrawText(&swr, "A", 4, 4, 1, 1, 0, 0xffff00, 1, -1);
    swrOverlayFlush(&swr); memcpy(ordinary, swr.fb, sizeof(ordinary));
    clear(&swr, 0x8000);
    swrDrawSprite(&swr.base, 4, 4, 4, 4, &texture, 0, 0, 4, 4, 0xffff00, 1);
    swrOverlayFlush(&swr); assert(memcmp(ordinary, swr.fb, sizeof(ordinary)) == 0);
    SWRenderer_setContrastEnabled(&swr.base, false);
    clear(&swr, 0x8000); swrDrawText(&swr, "A", 4, 4, 1, 1, 0, 0xffffff, 1, -1);
    swrOverlayFlush(&swr); assert(swr.fb[2 * SIZE + 2] == 0x9ce7);
    ++checked; finish(&swr, &texture);
}

/* Odd glyph rectangles must not borrow ink from their atlas neighbours. */
static void oddEdges(bool warm, int clip, int kind) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    if (kind == SWR_CONTRAST_BUTTON) swrFillPixels(texture.buffer, TEX * TEX, 0x8000);
    texture.buffer[0] = texture.buffer[2 * TEX + 1] = texture.buffer[4 * TEX + 4] = 0xffff;
    for (int y = 0; y < TEX; ++y) texture.buffer[y * TEX + 5] = 0xffff;
    for (int x = 0; x < TEX; ++x) texture.buffer[5 * TEX + x] = 0xffff;
    if (warm) swrDrawSprite(&swr.base, 0, 0, TEX, TEX, &texture, 0, 0, TEX, TEX, 0xffffff, 1);
    bool bubble = kind == SWR_CONTRAST_BUBBLE_GLYPH;
    uintpixel_t background = bubble ? 0xffff : 0x8000;
    clear(&swr, background);
    if (clip & 1) swr.portX = 3;
    if (clip & 2) swr.portY = 3;
    if (clip & 4) swr.maxX = 4;
    if (clip & 8) swr.maxY = 4;
    swr.portW = swr.maxX - swr.portX; swr.portH = swr.maxY - swr.portY;
    swr.viewX = swr.portX * 2; swr.viewY = swr.portY * 2;
    swrDrawContrastSprite(&swr.base, 4, 4, 5, 5, &texture, 0, 0, 5, 5, bubble ? 0 : 0xffffff, 1, kind);
    swrOverlayFlush(&swr);
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < SIZE; ++x) {
            uintpixel_t want = background;
            if (x >= swr.portX && x < swr.maxX && y >= swr.portY && y < swr.maxY) {
                if (x == 2 && (y == 2 || y == 3)) want = kind == SWR_CONTRAST_GLYPH ? 0xdef7 : 0x8c63;
                if (x == 4 && y == 4) want = kind == SWR_CONTRAST_GLYPH ? 0xdef7 : 0xce73;
                if (bubble && x == 2 && (y == 2 || y == 3)) want = 0xc631;
                if (bubble && x == 4 && y == 4) want = 0xa529;
            }
            if (swr.fb[y * SIZE + x] != want) {
                fprintf(stderr, "oddEdges warm=%d clip=%d kind=%d at=%d/%d: %04x != %04x\n",
                        warm, clip, kind, x, y, swr.fb[y * SIZE + x], want);
                abort();
            }
        }
    ++checked; finish(&swr, &texture);
}

/* Independent weighted-color oracle, plus clipped-versus-unclipped crops.
 * Distinct source colors reveal both grid shifts and sampling past an odd edge. */
static void hybridCrop(int kind, int clip, bool warm) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    for (int y = 0; y < TEX; ++y)
        for (int x = 0; x < TEX; ++x)
            texture.buffer[y * TEX + x] = (uintpixel_t) (0x8000 | ((x + 1) << 10) | ((y + 1) << 5));
    if (warm) {
        swrDrawSprite(&swr.base, 0, 0, TEX, TEX, &texture, 0, 0, TEX, TEX, 0xffffff, 1);
        swrOverlayFlush(&swr);
    }
    clear(&swr, 0x8000);
    swrDrawContrastSprite(&swr.base, 4, 4, 5, 5, &texture, 0, 0, 5, 5, 0xffffff, 1, kind);
    swrOverlayFlush(&swr);
    // Hand-computed five-tap weights on this grid, over black; the odd last
    // row/column has missing box taps, but keeps its clamped center sample.
    const uintpixel_t expected[3][3] = {
        {0x8420, 0x8c20, 0x8c20}, {0x8460, 0x8c60, 0x8c40}, {0x8460, 0x8860, 0x8c60}
    };
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            assert(swr.fb[(y + 2) * SIZE + x + 2] == expected[y][x]);
    uintpixel_t full[SIZE * SIZE]; memcpy(full, swr.fb, sizeof(full));
    clear(&swr, 0x8000);
    if (clip & 1) swr.portX = 3;
    if (clip & 2) swr.portY = 3;
    if (clip & 4) swr.maxX = 4;
    if (clip & 8) swr.maxY = 4;
    swr.portW = swr.maxX - swr.portX; swr.portH = swr.maxY - swr.portY;
    swr.viewX = swr.portX * 2; swr.viewY = swr.portY * 2;
    swrDrawContrastSprite(&swr.base, 4, 4, 5, 5, &texture, 0, 0, 5, 5, 0xffffff, 1, kind);
    swrOverlayFlush(&swr);
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < SIZE; ++x) {
            bool inside = x >= swr.portX && x < swr.maxX && y >= swr.portY && y < swr.maxY;
            assert(swr.fb[y * SIZE + x] == (inside ? full[y * SIZE + x] : 0x8000));
        }
    ++checked; finish(&swr, &texture);
}

/* A Memory restores renderer state, not immutable texture derivatives. Warm
 * redraws and a cold rebuild must still produce the same complete framebuffer. */
static void buttonCacheRestore(void) {
    SWRenderer swr;
    SWTexture texture = {0}; DataWin data = {0}; SWTexture *pages[] = {&texture};
    setup(&swr, &texture, &data, pages);
    for (int i = 0; i < TEX * TEX; ++i)
        texture.buffer[i] = (uintpixel_t) ((i % 3 ? 0x8000 : 0) | ((i * 7919u) & 0x7fff));
    // Independent per-channel five-tap result for crop (1,1,5,5) over
    // RGB555 0xa937. The center weighs four and each in-bounds box tap one;
    // transparent/missing taps keep their eighth of the destination channel.
    const uintpixel_t crop[3][3] = {
        {0xa537, 0xda4e, 0xa554}, {0x9a99, 0xb173, 0xd4af}, {0xb115, 0xc132, 0xa937}
    };
    uintpixel_t expected[SIZE * SIZE];
    for (int y = 0; y < SIZE; ++y)
        for (int x = 0; x < SIZE; ++x)
            expected[y * SIZE + x] = x >= 2 && x < 5 && y >= 2 && y < 5 ? crop[y - 2][x - 2] : 0xa937;
    clear(&swr, 0xa937);
    swrDrawContrastSprite(&swr.base, 4, 4, 5, 5, &texture, 1, 1, 5, 5, 0xffffff, 1, SWR_CONTRAST_BUTTON);
    swrOverlayFlush(&swr);
    if (!texture.buttonCache || memcmp(expected, swr.fb, sizeof(expected))) abort();
    const SWButtonCache *warm = texture.buttonCache;
    SWRendererSnapshotState state;
    if (!SWRenderer_snapshotGetState(&swr.base, &state)) abort();
    swr.blendMode = bm_add; swr.writeMask = 0;
    SWRenderer_snapshotApplyState(&swr.base, &state);
    clear(&swr, 0xa937);
    swrDrawContrastSprite(&swr.base, 4, 4, 5, 5, &texture, 1, 1, 5, 5, 0xffffff, 1, SWR_CONTRAST_BUTTON);
    swrOverlayFlush(&swr);
    if (texture.buttonCache != warm || memcmp(expected, swr.fb, sizeof(expected))) abort();
    swrFreeButtonCache(&texture);
    clear(&swr, 0xa937);
    swrDrawContrastSprite(&swr.base, 4, 4, 5, 5, &texture, 1, 1, 5, 5, 0xffffff, 1, SWR_CONTRAST_BUTTON);
    swrOverlayFlush(&swr);
    if (!texture.buttonCache || memcmp(expected, swr.fb, sizeof(expected))) abort();
    // Exercise the real page-cache eviction path with heap-owned textures.
    // Recreate from the same synthetic source instead of requiring an asset pack.
    size_t previousTextureCount = swr.textureCount;
    swr.textureCount = 1;
    for (int reload = 0; reload < 2; ++reload) {
        SWTexture *page = swrCreateTextureEx(texture.buffer, TEX, TEX, false);
        page->immutable = true;
        page->lastUsedFrame = swr.frameCounter;
        pages[0] = page;
        if (page->buttonCache) abort();
        clear(&swr, 0xa937);
        swrDrawContrastSprite(&swr.base, 4, 4, 5, 5, page, 1, 1, 5, 5, 0xffffff, 1, SWR_CONTRAST_BUTTON);
        swrOverlayFlush(&swr);
        if (!page->buttonCache || memcmp(expected, swr.fb, sizeof(expected))) abort();
        swrEvictTextureFromCache(&swr, 0);
        if (pages[0] != NULL) abort();
    }
    pages[0] = &texture;
    swr.textureCount = previousTextureCount;
    ++checked; finish(&swr, &texture);
}

int main(void) {
    for (unsigned pattern = 0; pattern < 16; ++pattern)
        for (int kind = SWR_CONTRAST_GLYPH; kind <= SWR_CONTRAST_BUBBLE_GLYPH; ++kind)
            for (int warm = 0; warm < 2; ++warm)
                for (int phase = 0; phase < 4; ++phase)
                    for (int alpha = 1; alpha <= 2; ++alpha)
                        coverage(pattern, kind, warm, phase & 1, phase >> 1, (float) alpha / 2,
                                 kind == SWR_CONTRAST_BUTTON ? 0xfde4 : 0xffff);
    for (int kind = SWR_CONTRAST_GLYPH; kind <= SWR_CONTRAST_BUBBLE_GLYPH; ++kind)
        for (int guard = 0; guard < 10; ++guard) isolation(guard, kind);
    isolation(10, SWR_CONTRAST_BUBBLE_GLYPH);
    metadata();
    textDispatch();
    hudSpriteDispatch();
    hintedName();
    buttonCacheRestore();
    for (int kind = SWR_CONTRAST_GLYPH; kind <= SWR_CONTRAST_BUBBLE_GLYPH; ++kind)
        for (int clip = 0; clip < 16; ++clip)
            for (int warm = 0; warm < 2; ++warm) oddEdges(warm, clip, kind);
    for (int kind = SWR_CONTRAST_BUTTON; kind <= SWR_CONTRAST_UI_GLYPH; ++kind)
        for (int clip = 0; clip < 16; ++clip)
            for (int warm = 0; warm < 2; ++warm) hybridCrop(kind, clip, warm);
    printf("PASS: %u battle contrast coverage and isolation cases\n", checked);
    return 0;
}
