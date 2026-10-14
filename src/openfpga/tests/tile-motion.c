/* Compare warm, translated pictures with the original cold-picture composition.
 * Count only allocations made by the renderer, not fixture/reference storage. */
#include "sw_renderer_private.h"
#include <assert.h>
#include <stdio.h>
static unsigned allocations;
static void* countedCalloc(size_t count, size_t size) {
    ++allocations;
    return calloc(count, size);
}
#define calloc countedCalloc
#include "../../sw/sw_renderer.c"
#undef calloc

enum { SIDE = 64, TILES = 64, TEX = 9 };
static unsigned checked;
static bool evictDuringGrid;
static unsigned evictionRequests;
static void pressureSprite(Renderer* renderer, int32_t index, float x, float y,
                           float ox, float oy, float xs, float ys, float angle,
                           uint32_t color, float alpha) {
    if (evictDuringGrid) { ++evictionRequests; swrTileRunsFree(); }
    SWRenderer_drawSprite(renderer, index, x, y, ox, oy, xs, ys, angle, color, alpha);
}


/* Rasterize into an empty picture, as the pre-reuse cache did on every miss,
 * then composite opaque pixels one by one. No cache keys or block marks. */
static void coldPicture(SWRenderer* swr, RoomTile** tiles, const float* offsets,
                        uintpixel_t* expected) {
    int shift = swr->scaleX == 0.5f ? 1 : 0;
    int left = INT32_MAX, top = INT32_MAX, right = INT32_MIN, bottom = INT32_MIN;
    for (int i = 0; i < TILES; ++i) {
        int x = swrFloor((float) tiles[i]->x + offsets[i * 2]);
        int y = swrFloor((float) tiles[i]->y + offsets[i * 2 + 1]);
        if (x < left) left = x;
        if (y < top) top = y;
        if (x + (int) tiles[i]->width > right) right = x + (int) tiles[i]->width;
        if (y + (int) tiles[i]->height > bottom) bottom = y + (int) tiles[i]->height;
    }
    int step = 16 << shift;
    left = (int) (floor((double) left / step) * step);
    top = (int) (floor((double) top / step) * step);
    int width = (right - left + shift) >> shift, height = (bottom - top + shift) >> shift;
    uintpixel_t* picture = calloc((size_t) width * height, sizeof(*picture));
    assert(picture);
    SWRenderer saved = *swr;
    swr->fb = picture; swr->fbPitch = (uint16_t) width;
    swr->viewX = left; swr->viewY = top;
    swr->portX = swr->portY = 0;
    swr->portW = swr->maxX = width; swr->portH = swr->maxY = height;
    for (int i = 0; i < TILES; ++i)
        Renderer_drawTile(&swr->base, tiles[i], offsets[i * 2], offsets[i * 2 + 1]);
    swrOverlayFlush(swr);
    *swr = saved;
    int vx = shift ? (swr->viewX + 1) >> 1 : swr->viewX;
    int vy = shift ? (swr->viewY + 1) >> 1 : swr->viewY;
    for (int y = swr->portY; y < swr->maxY; ++y)
        for (int x = swr->portX; x < swr->maxX; ++x) {
            int sx = x - swr->portX + vx - (left >> shift);
            int sy = y - swr->portY + vy - (top >> shift);
            if (sx >= 0 && sy >= 0 && sx < width && sy < height && (picture[sy * width + sx] & 0x8000))
                expected[y * SIDE + x] = picture[sy * width + sx];
        }
    free(picture);
}

static void checkPixels(SWRenderer* swr, const uintpixel_t* expected) {
    swrOverlayFlush(swr);
    for (int i = 0; i < SIDE * SIDE; ++i) {
        if (swr->fb[i] != expected[i]) {
            fprintf(stderr, "FAIL frame %u scale %.1f pixel %d,%d: %04x != %04x\n",
                    swr->frameCounter, (double) swr->scaleX, i % SIDE, i / SIDE,
                    swr->fb[i], expected[i]);
            exit(1);
        }
    }
    ++checked;
}

static void clearFrame(SWRenderer* swr, uintpixel_t* expected) {
    swrOverlayFlush(swr);
    swrFillPixels(swr->fb, SIDE * SIDE, 0x8c63);
    swrFillPixels(expected, SIDE * SIDE, 0x8c63);
}

int main(void) {
    SWRenderer* swr = (SWRenderer*) SWRenderer_create();
    DataWin data = {0}; Background background = {0}; TexturePageItem item = {0};
    SWTexture texture = {0}; SWTexture* pages[] = {&texture};
    uintpixel_t texels[TEX * TEX], pixels[SIDE * SIDE], expected[SIDE * SIDE];
    RoomTile tiles[TILES] = {{0}}; RoomTile* run[TILES]; float offsets[TILES * 2] = {0};
    data.bgnd.count = data.tpag.count = 1;
    data.bgnd.backgrounds = &background; data.tpag.items = &item;
    item.sourceWidth = item.sourceHeight = item.targetWidth = item.targetHeight = TEX;
    texture.width = texture.height = TEX; texture.buffer = texels; texture.immutable = true;
    for (int i = 0; i < TEX * TEX; ++i)
        texels[i] = i % 5 == 0 ? 0 : (uintpixel_t)(0x8000 | ((i * 491) & 0x7fff));
    swr->base.dataWin = &data; swr->textures = pages; swr->totalTextureCount = 1;
    swr->fb = swr->mainFb = pixels;
    swr->width = swr->height = swr->mainWidth = swr->mainHeight = SIDE;
    swr->fbPitch = swr->mainPitch = SIDE;
    swr->portW = swr->portH = swr->maxX = swr->maxY = SIDE;
    swr->writeMask = WRITE_MASK_ALL;
    swrFavorSpeed = swrSmoothMinify = true;
    for (int i = 0; i < TILES; ++i) {
        run[i] = &tiles[i]; tiles[i].width = tiles[i].height = TEX;
        tiles[i].scaleX = tiles[i].scaleY = tiles[i].alpha = 1.0f;
        tiles[i].color = 0xffffff;
    }
    for (int pattern = 0; pattern < 3; ++pattern) {
      swrTileRunsFree();
      free(texture.halfBuffer); free(texture.rowBounds);
      memset(&texture, 0, sizeof(texture));
      texture.width = texture.height = TEX; texture.buffer = texels; texture.immutable = true;
      int spacing = pattern == 1 ? 8 : 10;
      for (int i = 0; i < TEX * TEX; ++i)
          texels[i] = pattern == 2 || (pattern == 0 && i % 5 == 0) ? 0 :
                      (uintpixel_t)(0x8000 | ((i * 491) & 0x7fff));
      for (int i = 0; i < TILES; ++i) tiles[i].width = tiles[i].height = (uint32_t)(pattern == 1 ? 8 : TEX);
      for (int half = 0; half < 2; ++half) {
        swrTileRunsFree(); allocations = 0;
        swr->scaleX = swr->scaleY = half ? 0.5f : 1.0f;
        /* Traverse every block phase, all clipping edges and negative positions.
         * Odd source extents and transparent texels exercise partial coverage. */
        for (int f = 0; f < 40; ++f) {
            swr->frameCounter = (uint32_t)(100 + half * 100 + f);
            swr->viewX = (f % 3) - 1; swr->viewY = (f % 5) - 2;
            swr->portX = f % 4; swr->portY = f % 5;
            swr->maxX = SIDE - f % 3; swr->maxY = SIDE - f % 2;
            swr->portW = swr->maxX - swr->portX; swr->portH = swr->maxY - swr->portY;
            for (int i = 0; i < TILES; ++i) {
                tiles[i].x = (i % 8) * spacing + f * (half ? 2 : 1) - 36;
                tiles[i].y = (i / 8) * spacing - f * (half ? 2 : 1) + 36;
            }
            clearFrame(swr, expected);
            coldPicture(swr, run, offsets, expected);
            if (f % 7 == 0) {
                swrFillPixels(swr->fb, SIDE * SIDE, 0xffff);
                swr->clearHeld = true; swr->clearHeldColor = 0x8c63;
                swr->uniformValid = true; swr->uniformColor = 0x8c63;
            }
            assert(SWRenderer_drawTileRun(&swr->base, run, offsets, TILES));
            checkPixels(swr, expected);
        }
        if (allocations != 1) {
            fprintf(stderr, "FAIL translated content built %u pictures, expected one\n", allocations);
            return 1;
        }
        /* Reuse one picture twice before flushing; each placement must survive. */
        ++swr->frameCounter;
        clearFrame(swr, expected);
        for (int pass = 0; pass < 2; ++pass)
          for (int copy = 0; copy < 6; ++copy) {
            for (int i = 0; i < TILES; ++i) {
                tiles[i].x = (i % 8) * spacing + copy * 24;
                tiles[i].y = (i / 8) * spacing;
            }
            if (pass == 0) coldPicture(swr, run, offsets, expected);
            else assert(SWRenderer_drawTileRun(&swr->base, run, offsets, TILES));
        }
        checkPixels(swr, expected);
        /* Warm a flattened pair, then move only the second picture. */
        for (int f = 0; f < 5; ++f) {
            ++swr->frameCounter; clearFrame(swr, expected);
            for (int pass = 0; pass < 2; ++pass)
              for (int copy = 0; copy < 2; ++copy) {
                tiles[0].color = copy ? 0x00ff00 : 0xffffff;
                for (int i = 0; i < TILES; ++i) {
                    tiles[i].x = (i % 8) * spacing + (copy && f >= 3 ? 6 : 0);
                    tiles[i].y = (i / 8) * spacing;
                }
                if (pass == 0) coldPicture(swr, run, offsets, expected);
                else assert(SWRenderer_drawTileRun(&swr->base, run, offsets, TILES));
            }
            checkPixels(swr, expected);
        }
        tiles[0].color = 0xffffff;
        /* A layer offset is also placement, including stable fractional phase at 1:1. */
        swrTileRunsFree(); allocations = 0;
        for (int f = 0; f < 12; ++f) {
            ++swr->frameCounter;
            for (int i = 0; i < TILES; ++i) {
                offsets[i * 2] = half ? (float)(f * 2) : (float)f + 0.25f;
                offsets[i * 2 + 1] = half ? (float)(-f * 2) : (float)-f + 0.75f;
            }
            clearFrame(swr, expected); coldPicture(swr, run, offsets, expected);
            assert(SWRenderer_drawTileRun(&swr->base, run, offsets, TILES));
            checkPixels(swr, expected);
        }
        assert(allocations == 1);
        /* Content/order/source changes must invalidate rather than translate an old image. */
        for (int mutation = 0; mutation < 3; ++mutation) {
            ++swr->frameCounter;
            if (mutation == 0) tiles[0].sourceX = 1;
            if (mutation == 1) { RoomTile* swap = run[0]; run[0] = run[1]; run[1] = swap; }
            if (mutation == 2) tiles[1].x += 2;
            clearFrame(swr, expected); coldPicture(swr, run, offsets, expected);
            assert(SWRenderer_drawTileRun(&swr->base, run, offsets, TILES));
            checkPixels(swr, expected);
            assert(allocations == (unsigned) mutation + 2);
        }
        tiles[0].sourceX = 0;
        for (int i = 0; i < TILES; ++i) run[i] = &tiles[i];
        memset(offsets, 0, sizeof(offsets));
        if (half) {
            offsets[0] = 0.5f;
            assert(!swrDrawTileRunCached(&swr->base, run, offsets, TILES));
            offsets[0] = 0;
            tiles[0].x |= 1;
            assert(!swrDrawTileRunCached(&swr->base, run, offsets, TILES));
        }
      }
    }
    /* Eviction requested during below-grid replay must not free queued images. */
    swrTileRunsFree();
    Sprite sprite = {0}; int32_t spriteItem = 0;
    sprite.width = sprite.height = TEX; sprite.textureCount = 1; sprite.tpagIndices = &spriteItem;
    data.sprt.count = 1; data.sprt.sprites = &sprite;
    RendererVtable pressureVtable = *swr->base.vtable;
    pressureVtable.drawSprite = pressureSprite; swr->base.vtable = &pressureVtable;
    swr->base.drawAlpha = 1; swr->base.drawColor = 0xffffff;
    swr->viewX = swr->viewY = swr->portX = swr->portY = 0;
    swr->portW = swr->portH = swr->maxX = swr->maxY = SIDE;
    for (int i = 0; i < TEX * TEX; ++i) texels[i] = i % 5 ? 0xffff : 0;
    free(texture.halfBuffer); free(texture.rowBounds);
    memset(&texture, 0, sizeof(texture));
    texture.width = texture.height = TEX; texture.buffer = texels; texture.immutable = true;
    for (int i = 0; i < TILES; ++i) { tiles[i].x = (i % 8) * 10; tiles[i].y = (i / 8) * 10; }
    for (int above = 0; above < 2; ++above) {
        ++swr->frameCounter; clearFrame(swr, expected);
        if (!above) {
            Renderer_drawSprite(&swr->base, 0, 0, 12, 12); swrOverlayFlush(swr);
            memcpy(expected, swr->fb, sizeof(expected));
        }
        coldPicture(swr, run, offsets, expected);
        if (above) {
            uintpixel_t* saved = swr->fb; swr->fb = expected;
            Renderer_drawSprite(&swr->base, 0, 0, 12, 12); swrOverlayFlush(swr); swr->fb = saved;
        }
        swrFillPixels(swr->fb, SIDE * SIDE, 0x8c63);
        assert(SWRenderer_drawTileRun(&swr->base, run, offsets, TILES));
        swrGrid.count = swrGrid.cells = 1; swrGrid.above = above != 0; swrGrid.owner = swr;
        swrGrid.grids[0].sprite = 0; swrGrid.grids[0].cols = swrGrid.grids[0].rows = 1;
        swrGrid.grids[0].x = swrGrid.grids[0].y = 12; swrGrid.grids[0].alpha = 1;
        swrGrid.grids[0].first = 0; swrGrid.subimgs[0] = 0;
        evictDuringGrid = true; checkPixels(swr, expected); evictDuringGrid = false;
    }
    assert(evictionRequests == 2);
    assert(swrTileRunsFree());
    free(texture.halfBuffer); free(texture.rowBounds); free(swr);
    printf("PASS: %u translated tile frames preserve cold-picture pixels\n", checked);
    return 0;
}
