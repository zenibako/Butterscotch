#include <stdio.h>
#include <limits.h>
#include <float.h>
#include <assert.h>
#include "text_utils.h"
#include "image/image_decoder.h"

#include "sw_renderer_private.h"
#include "gettime.h"

#define MAX_TRIS 4096
#define VERTICES_PER_TRIANGLE 3
#define VERTICES_PER_QUAD 4

void platformSetNextFramebuffer(uintpixel_t* framebuffer, int width, int height, int bpp);

static void swrFlushPendingClear(SWRenderer* swr);

#ifdef SW_PLATFORM_FRAMEBUFFER
// Optional: lets the platform hand out the buffer each frame is drawn into
// (typically the display's back buffer) so a finished frame does not have to
// be copied to the screen. Must return width * height pixels with no row
// padding, or NULL to make the renderer use its own buffer. It is called at
// the start of every frame and may return a different buffer each time; the
// contents are undefined, the renderer clears them.
uintpixel_t* platformAcquireFramebuffer(int width, int height);

static bool swrAcquirePlatformFramebuffer(SWRenderer* swr, int width, int height)
{
    uintpixel_t* fb = platformAcquireFramebuffer(width, height);
    if (!fb) return false;
    
    if (!swr->fbIsPlatform) free(swr->mainFb);
    swr->fbIsPlatform = true;
    swr->fb = swr->mainFb = fb;
    swr->fbPitch = width;
    swr->width = swr->mainWidth = width;
    swr->height = swr->mainHeight = height;
    return true;
}
#endif

static void SWRenderer_gpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha);

static void SWRenderer_init(Renderer* renderer, DataWin* dataWin)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    renderer->dataWin = dataWin;
    
    //allocate texture buffer
    swr->textureCount = dataWin->txtr.count;
    swr->surfaceCount = SURFACE_MAX_COUNT;
    swr->totalTextureCount = swr->textureCount + swr->surfaceCount;
    swr->textures = (SWTexture**) safeCalloc(swr->totalTextureCount, sizeof(SWTexture*));
    swr->surfaces = (SWSurface**) safeCalloc(swr->surfaceCount, sizeof(SWSurface*));
    
    //HACK: this isn't good, really.  This should seriously be refactored.
    //expand datawin's tpag items list to include our surface count.
    swr->originalTPagCount = dataWin->tpag.count;
    dataWin->tpag.items = (TexturePageItem*) safeRealloc(dataWin->tpag.items, sizeof(TexturePageItem) * (dataWin->tpag.count + swr->surfaceCount));
    dataWin->tpag.count += swr->surfaceCount;
    
    swr->originalSpriteCount = dataWin->sprt.count;
    
    for (size_t i = swr->originalTPagCount; i < dataWin->tpag.count; i++)
    {
        memset(&dataWin->tpag.items[i], 0, sizeof(TexturePageItem));
        dataWin->tpag.items[i].texturePageId = -1;
    }
    
    // Allocate vertex data for primitive support
    swr->maxVertexCount = MAX_TRIS * VERTICES_PER_QUAD;
    swr->vertexData = (SWVertex*) safeCalloc(swr->maxVertexCount, sizeof(SWVertex));
    swr->vertexCount = 0;
    swr->primitiveType = 0;
    swr->primitiveBegun = false;
    
    logInfo("SWRenderer initialized.\n");
}

static void SWRenderer_destroy(Renderer* renderer)
{
    SWRenderer* swr = (SWRenderer*) renderer;

    swr->primitiveBegun = false;
    swr->primitiveOverflow = false;
    swr->primitiveType = -1;
    swr->vertexCount = 0;
    
    for (size_t i = 0; i < swr->surfaceCount; i++)
    {
        swrFreeSurface(swr->surfaces[i]);
    }
    free(swr->surfaces);
    swr->surfaceCount = 0;
    {
    for (size_t i = 0; i < swr->totalTextureCount; i++)
    {
        swrFreeTexture(swr->textures[i]);
    }
    }
    swrFreeItemTextures(swr);
    free(swr->textures);
    swr->textureCount = 0;
    swr->totalTextureCount = 0;
    free(swr->vertexData);
    
    if (!swr->fbIsPlatform) free(swr->mainFb);
    swr->fb = swr->mainFb = NULL;
    
    free(swr);
    logInfo("SWRenderer destroyed.\n");
}

static void SWRenderer_beginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    swr->frameCounter++;
    swr->gameW = gameW;
    swr->gameH = gameH;
    swr->drawingToSurface = false;
    swr->blendMode = bm_normal;

#ifdef SW_PLATFORM_FRAMEBUFFER
    {
        bool resized = swr->width != windowW || swr->height != windowH;
        if (swrAcquirePlatformFramebuffer(swr, windowW, windowH)) {
            // clearFrameBuffer ran before this frame's size was known.
            if (resized) swrFillPixels(swr->fb, (size_t) windowW * windowH, swrConvertPixel(0));
            return;
        }
        if (swr->fbIsPlatform) {
            // The platform stopped providing a buffer: go back to our own.
            swr->fbIsPlatform = false;
            swr->fb = swr->mainFb = NULL;
            swr->width = swr->height = 0;
        }
    }
#endif

    if (swr->width != windowW || swr->height != windowH)
    {
        //allocate frame buffer
        free(swr->fb);
        swr->fb = (uintpixel_t*) safeMalloc(windowW * windowH * sizeof(uintpixel_t));
        swr->fbPitch = windowW;
        swr->width = windowW;
        swr->height = windowH;
        
        swr->mainFb = swr->fb;
        swr->mainWidth = swr->width;
        swr->mainHeight = swr->height;
    }
    
#ifdef SW_DEBUG_FRAME_DRAW_BOUNDS
    logDebug("swr: begin drawing frame\n");
#endif
}

// This used to be just one, "endFrame". Not sure what the difference is.
static void SWRenderer_endFrameInit(Renderer* renderer)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
    
    //this is kinda useless to do twice isn't it?
}

static void SWRenderer_endFrameEnd(Renderer* renderer)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    assert(!swr->drawingToSurface);
    
#ifdef SW_DEBUG_FRAME_DRAW_BOUNDS
    logDebug("swr: end drawing frame\n");
#endif
    
    // Nothing cleared the frame itself: do it now rather than present stale pixels.
    if (swrSkipFrame) swr->pendingClear = false;
    swrFlushPendingClear(swr);
    
    platformSetNextFramebuffer(swr->fb, swr->width, swr->height, PIXEL_SIZE);

    swr->primitiveOverflow = false;
}

static void SWRenderer_beginView(Renderer* renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH,
                                 int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void)renderer; (void)viewX; (void)viewY; (void)viewW; (void)viewH;
    (void)portX; (void)portY; (void)portW; (void)portH; (void)viewAngle;
    UNIMP2();
    
    SWRenderer* swr = (SWRenderer*) renderer;
    
    float xratio, yratio;
    
    float portviewX = (float) portW / viewW;
    float portviewY = (float) portH / viewH;
    
    int offsetX = 0, offsetY = 0;
    
    if (swr->drawingToSurface) {
        UNIMP();
        xratio = 1.0f;
        yratio = 1.0f;
        portX = (int)(portX * xratio);
        portY = (int)(portY * yratio);
    }
    else {
        float scaleX = (float) swr->width  / swr->gameW;
        float scaleY = (float) swr->height / swr->gameH;
        float scale  = (scaleX < scaleY) ? scaleX : scaleY;

        int32_t scaledW = (int32_t)(swr->gameW * scale);
        int32_t scaledH = (int32_t)(swr->gameH * scale);
        
        offsetX = (swr->width  - scaledW) / 2;
        offsetY = (swr->height - scaledH) / 2;

        xratio = scale;
        yratio = scale;

        portX = (int)(portX * xratio) + offsetX;
        portY = (int)(portY * yratio) + offsetY;
    }
    
    swr->scaleX = xratio * portviewX;
    swr->scaleY = yratio * portviewY;
    swr->offsetX = offsetX;
    swr->offsetY = offsetY;
    swr->defaultScaleX = xratio;
    swr->defaultScaleY = yratio;
    
    portW = (int)(portW * xratio);
    portH = (int)(portH * yratio);
    
    swr->viewActive = true;
    swr->viewX = viewX;
    swr->viewY = viewY;
    swr->viewW = viewW;
    swr->viewH = viewH;
    swr->portX = portX;
    swr->portY = portY;
    swr->portW = portW;
    swr->portH = portH;
    swr->maxX = portX + portW;
    swr->maxY = portY + portH;
}

static void SWRenderer_endView(Renderer* renderer)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void)renderer;
    UNIMP2();
    
    SWRenderer* swr = (SWRenderer*) renderer;
    swr->viewActive = false;
    
    swr->viewX = 0;
    swr->viewY = 0;
    swr->portX = 0;
    swr->portY = 0;
    swr->portW = swr->viewW = swr->width;
    swr->portH = swr->viewH = swr->height;
    swr->maxX = swr->portX + swr->portW;
    swr->maxY = swr->portY + swr->portH;
    swr->scaleX = swr->defaultScaleX;
    swr->scaleY = swr->defaultScaleY;
}

static void SWRenderer_beginGUI(Renderer* renderer, int32_t guiW, int32_t guiH,
                                int32_t portX, int32_t portY, int32_t portW, int32_t portH, int32_t targetSurfaceId)
{
    swrOverlayFlush((SWRenderer*) renderer);
    swrSwitchToSurface(renderer, targetSurfaceId, false);
    
    (void)guiW; (void)guiH;
    (void)portX; (void)portY; (void)portW; (void)portH;
    (void)targetSurfaceId;
    UNIMP2();
}

static void SWRenderer_setGuiProjection(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portW, int32_t portH, bool renderingToUserSurface)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
    (void) guiW; (void) guiH;
    (void) portW; (void) portH;
    (void) renderingToUserSurface;
    UNIMP();
}

static void SWRenderer_endGUI(Renderer* renderer)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void)renderer;
    UNIMP2();
}

static void SWRenderer_drawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y,
                                  float originX, float originY, float xscale, float yscale,
                                  float angleDeg, uint32_t color, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    SWRenderer* swr = (SWRenderer*) renderer;
    DataWin* dwin = renderer->dataWin;

    if (tpagIndex < 0 || (uint32_t) tpagIndex >= dwin->tpag.count) {
        logError("%s: tpagIndex of %d is invalid\n", __func__, tpagIndex);
        return;
    }

    TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || swr->totalTextureCount <= (uint32_t) pageId) {
        logError("%s: tpagIndex of %d is invalid, as pageId of %d is invalid\n", __func__, tpagIndex, pageId);
        return;
    }
    SWTexture* texture = swrTextureForItem(swr, tpagIndex);
    if (!texture) {
        logError("%s: could not ensure texture is loaded, tpagIndex: %d, pageId: %d\n", __func__, tpagIndex, pageId);
        return;
    }
    
    int sx = tpag->sourceX - texture->originX;
    int sy = tpag->sourceY - texture->originY;
    int sw = tpag->sourceWidth;
    int sh = tpag->sourceHeight;
    
    float dx = (float)(tpag->targetX - originX);
    float dy = (float)(tpag->targetY - originY);
    int dw = (int)(xscale * tpag->targetWidth);
    int dh = (int)(yscale * tpag->targetHeight);
    dx *= xscale;
    dy *= yscale;
    dx += x;
    dy += y;
    
    if (UNLIKELY(swrMustRotate(angleDeg)))
    {
        float pivotX = (x - dx) * swrSgn(xscale);
        float pivotY = (y - dy) * swrSgn(yscale);
        
        if (tpag->targetWidth != tpag->sourceWidth)
            pivotX *= (float)tpag->targetWidth / tpag->sourceWidth;
        if (tpag->targetHeight != tpag->sourceHeight)
            pivotY *= (float)tpag->targetHeight/ tpag->sourceHeight;
        
        swrOverlayFlush(swr);
        swrDrawSpriteRotated(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha, angleDeg, pivotX, pivotY);
    }
    else
    {
        // Only a plain sprite draw may be held back for merging; see swrOverlayFlush.
        swr->overlayMergeAllowed = true;
        swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
        swr->overlayMergeAllowed = false;
    }
}

static void SWRenderer_drawSpritePart(Renderer* renderer, int32_t tpagIndex,
                                      float srcOffXf, float srcOffYf, float srcWf, float srcHf,
                                      float x, float y, float xscale, float yscale, float angleDeg,
                                      float pivotX, float pivotY, uint32_t color, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    // The interface allows fractional source rectangles; this renderer samples whole texels.
    int32_t srcOffX = (int32_t) srcOffXf, srcOffY = (int32_t) srcOffYf, srcW = (int32_t) srcWf, srcH = (int32_t) srcHf;
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    DataWin* dwin = renderer->dataWin;
    
    if (tpagIndex < 0 || (uint32_t) tpagIndex >= dwin->tpag.count) return;
    
    TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || swr->totalTextureCount <= (uint32_t) pageId) return;
    
    // An item stored at another size than it is drawn has its page position
    // rescaled below, which only means something on the whole page.
    bool rescaled = tpag->sourceWidth != tpag->targetWidth || tpag->sourceHeight != tpag->targetHeight;
    SWTexture* texture = NULL;
    if (!rescaled)
        texture = swrTextureForItem(swr, tpagIndex);
    else if (swrEnsureTextureIsLoaded(swr, (uint32_t) pageId))
        texture = swr->textures[pageId];
    if (!texture) return;
    
    int sx = tpag->sourceX + srcOffX;
    int sy = tpag->sourceY + srcOffY;
    int sw = srcW;
    int sh = srcH;
    
    float dx = x;
    float dy = y;
    int dw = swrCeiling(xscale * sw);
    int dh = swrCeiling(yscale * sh);
    
    if (tpag->sourceWidth != tpag->targetWidth) {
        sx = sx * tpag->sourceWidth / tpag->targetWidth;
        sw = sw * tpag->sourceWidth / tpag->targetWidth;
    }
    if (tpag->sourceHeight != tpag->targetHeight) {
        sy = sy * tpag->sourceHeight / tpag->targetHeight;
        sh = sh * tpag->sourceHeight / tpag->targetHeight;
    }
    sx -= texture->originX;
    sy -= texture->originY;
    
    if (UNLIKELY(swrMustRotate(angleDeg)))
    {
        swrDrawSpriteRotated(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha, angleDeg, pivotX * dw, pivotY * dh);
    }
    else
    {
        swrDrawSprite(renderer, dx, dy, dw, dh, texture, sx, sy, sw, sh, color, alpha);
    }
}

static void SWRenderer_drawSpritePos(Renderer* renderer, int32_t tpagIndex,
                                     float x1, float y1, float x2, float y2,
                                     float x3, float y3, float x4, float y4, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    // TODO: Implement this properly.  (I won't in this PR)
    //
    // You basically have to implement full texture UV mapping which I won't be
    // bothering with.  Somebody else can get on it.
    
    SWRenderer* swr = (SWRenderer*) renderer;
    DataWin* dwin = renderer->dataWin;

    if (tpagIndex < 0 || (uint32_t) tpagIndex >= dwin->tpag.count) {
        logError("%s: tpagIndex of %d is invalid\n", __func__, tpagIndex);
        return;
    }

    TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || swr->totalTextureCount <= (uint32_t) pageId) {
        logError("%s: tpagIndex of %d is invalid, as pageId of %d is invalid\n", __func__, tpagIndex, pageId);
        return;
    }
    int tw = tpag->targetWidth;
    int th = tpag->targetHeight;

    float ascalex = (x2 - x1) / tw;
    float ascaley = (y4 - y1) / th;
    
    (void) x3; (void) y3;
    (void) y2; (void) x4;

    UNIMP();
    
    SWRenderer_drawSprite(renderer, tpagIndex, x1, y1, 0.0f, 0.0f, ascalex, ascaley, 0.0f, renderer->drawColor, alpha);
}

static void SWRenderer_drawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2,
                                     uint32_t color, float alpha, bool outline)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    SWRenderer* swr = (SWRenderer*) renderer;
    uintpixel_t pxcolor = swrConvertPixel(color);
#ifdef SW_HAS_PREMUL_BLEND
    // A fill over a held stack of mirrored layers may join it instead of letting it out.
    if (!outline && swrMirrorHoldFill(renderer, x1, y1, x2, y2, pxcolor, alpha)) return;
#endif
    
    // Still one colour if the flush has nothing to draw; a fill with that colour is then redundant.
    bool nothingHeld = swr->mirrorLayers == 0 && swr->mirrorStage == 0 && swr->overlayCount == 0;
    bool kept = swr->uniformValid && nothingHeld;
    swrOverlayFlush(swr);
    
    if (outline)
        swrDrawRectangle(renderer, x1, y1, x2, y2, pxcolor, alpha);
    else {
        swr->uniformKept = kept;
        swrFillRectangle(renderer, x1, y1, x2, y2, pxcolor, alpha);
        swr->uniformKept = false;
    }
}

static void SWRenderer_drawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2,
                                          uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4,
                                          float alpha, bool outline)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    uintpixel_t pxcolor1 = swrConvertPixel(color1);
    uintpixel_t pxcolor2 = swrConvertPixel(color2);
    uintpixel_t pxcolor3 = swrConvertPixel(color3);
    uintpixel_t pxcolor4 = swrConvertPixel(color4);
    
    if (outline)
        swrDrawRectangleColor(renderer, x1, y1, x2, y2, pxcolor1, pxcolor2, pxcolor3, pxcolor4, alpha);
    else
        swrFillRectangleColor(renderer, x1, y1, x2, y2, pxcolor1, pxcolor2, pxcolor3, pxcolor4, alpha);
}

static void SWRenderer_drawLine(Renderer* renderer, float x1, float y1, float x2, float y2,
                                float width, uint32_t color, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    (void)renderer; (void)x1; (void)y1; (void)x2; (void)y2;
    (void)width; (void)color; (void)alpha;
    
    uintpixel_t colorCvt = swrConvertPixel(color);
#ifdef TRANSPARENT_MASK
    colorCvt |= TRANSPARENT_MASK;
#endif
    swrDrawLine(renderer, x1, y1, x2, y2, width, colorCvt, colorCvt, alpha, SWR_LINE_ALIGN_CENTER);
}

static void SWRenderer_drawTriangle(Renderer* renderer,
                                    float x1, float y1, float x2, float y2, float x3, float y3,
                                    uint32_t color1, uint32_t color2, uint32_t color3,
                                    float alpha, bool outline)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    if (outline)
    {
        uintpixel_t color1cvt = swrConvertPixel(color1);
        uintpixel_t color2cvt = swrConvertPixel(color2);
        uintpixel_t color3cvt = swrConvertPixel(color3);
        swrDrawLine(renderer, x1, y1, x2, y2, 1, color1cvt, color2cvt, alpha, SWR_LINE_ALIGN_CENTER);
        swrDrawLine(renderer, x1, y1, x3, y3, 1, color1cvt, color3cvt, alpha, SWR_LINE_ALIGN_CENTER);
        swrDrawLine(renderer, x2, y2, x3, y3, 1, color3cvt, color3cvt, alpha, SWR_LINE_ALIGN_CENTER);
    }
    else
    {
        swrDrawTriangle(renderer, x1, y1, x2, y2, x3, y3, color1, color2, color3, alpha);
    }
}

static void SWRenderer_drawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2,
                                     float width, uint32_t color1, uint32_t color2, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    swrDrawLine(renderer, x1, y1, x2, y2, width, swrConvertPixel(color1), swrConvertPixel(color2), alpha, SWR_LINE_ALIGN_CENTER);
}

static void SWRenderer_drawText(Renderer* renderer, const char* text, float x, float y,
                                float xscale, float yscale, float angleDeg, float lineSeparation)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    swrDrawText(swr, text, x, y, xscale, yscale, angleDeg, renderer->drawColor, renderer->drawAlpha, lineSeparation);
}

static void SWRenderer_drawTextColor(Renderer* renderer, const char* text, float x, float y,
                                     float xscale, float yscale, float angleDeg,
                                     int32_t c1, int32_t c2, int32_t c3, int32_t c4, MAYBE_UNUSED float alpha,
                                     float lineSeparation)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    // TODO: allow c2, c3, c4
    (void) c2;
    (void) c3;
    (void) c4;
    
    swrDrawText(swr, text, x, y, xscale, yscale, angleDeg, c1, renderer->drawAlpha, lineSeparation);
}

static void SWRenderer_drawTextUI(Renderer* renderer, const char* text, float x, float y,
                                  float xscale, float yscale, float angleDeg,
                                  int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha,
                                  float lineSeparation)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    (void) xscale;
    (void) yscale;
    (void) angleDeg;
    (void) c2;
    (void) c3;
    (void) c4;
    (void) lineSeparation;

    swrDrawDebugText(swr, text, (int) x, (int) y, c1, alpha);
}

static void SWRenderer_drawSpriteTiled(Renderer* renderer, int32_t tpagIndex,
                                       float originX, float originY, float x, float y,
                                       float xscale, float yscale, bool tileX, bool tileY,
                                       float roomW, float roomH, uint32_t color, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    DataWin* dwin = renderer->dataWin;

    if (0 > tpagIndex || dwin->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || swr->totalTextureCount <= (uint32_t) pageId) return;
    SWTexture* texture = swrTextureForItem(swr, tpagIndex);
    if (!texture) return;

    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float tileW = (float) tpag->boundingWidth * axScale;
    float tileH = (float) tpag->boundingHeight * ayScale;
    if (0 >= tileW || 0 >= tileH) return;

    float startX, endX, startY, endY;
    if (tileX) {
        startX = fmodf(x - originX * axScale, tileW);
        if (startX > 0) startX -= tileW;
        endX = roomW;
    } else {
        startX = x - originX * axScale;
        endX = startX + tileW;
    }
    if (tileY) {
        startY = fmodf(y - originY * ayScale, tileH);
        if (startY > 0) startY -= tileH;
        endY = roomH;
    } else {
        startY = y - originY * ayScale;
        endY = startY + tileH;
    }
    
    int sx = tpag->sourceX - texture->originX;
    int sy = tpag->sourceY - texture->originY;
    int sw = tpag->sourceWidth;
    int sh = tpag->sourceHeight;

    int localX0 = tpag->targetX - originX;
    int localY0 = tpag->targetY - originY;
    int localX1 = localX0 + tpag->sourceWidth;
    int localY1 = localY0 + tpag->sourceHeight;
    int sx0 = xscale * localX0;
    int sy0 = yscale * localY0;
    int sx1 = xscale * localX1;
    int sy1 = yscale * localY1;

    for (int dy = startY; endY > dy; dy += tileH) {
        int cy = dy + (int)(originY * ayScale);
        int vy0 = cy + sy0;
        int vy1 = cy + sy1;
        int dh = vy1 - vy0;

        for (int dx = startX; endX > dx; dx += tileW) {
            int cx = dx + (int)(originX * axScale);
            int vx0 = cx + sx0;
            int vx1 = cx + sx1;
            int dw = vx1 - vx0;

            swrDrawSprite(renderer, vx0, vy0, dw, dh, texture, sx, sy, sw, sh, color, alpha);
        }
    }
}

static void SWRenderer_drawSurfaceTiled(Renderer* renderer, int32_t surfaceID, float x, float y, float xscale, float yscale, float roomW, float roomH, uint32_t color, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;

    if (0 > surfaceID || swr->surfaceCount <= (size_t) surfaceID) return;

    SWSurface* surfaceP = swr->surfaces[surfaceID];
    if (!surfaceP) return;

    swrCommitShadowWritesToSurfaceIfNeeded(swr, surfaceP);
    SWTexture* surface = surfaceP->texture;

    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float tileW = (float) surface->width * axScale;
    float tileH = (float) surface->height * ayScale;
    if (0 >= tileW || 0 >= tileH) return;
    
    float originX = 0, originY = 0;

    float startX, endX, startY, endY;
    startX = fmodf(x - originX * axScale, tileW);
    if (startX > 0) startX -= tileW;
    endX = roomW;
    startY = fmodf(y - originY * ayScale, tileH);
    if (startY > 0) startY -= tileH;
    endY = roomH;
    
    int sx = 0, sy = 0;
    int sw = surface->width;
    int sh = surface->height;

    int localX0 = -originX;
    int localY0 = -originY;
    int localX1 = localX0 + surface->width;
    int localY1 = localY0 + surface->height;
    int sx0 = xscale * localX0;
    int sy0 = yscale * localY0;
    int sx1 = xscale * localX1;
    int sy1 = yscale * localY1;

    for (int dy = startY; endY > dy; dy += tileH) {
        int cy = dy + (int)(originY * ayScale);
        int vy0 = cy + sy0;
        int vy1 = cy + sy1;
        int dh = vy1 - vy0;

        for (int dx = startX; endX > dx; dx += tileW) {
            int cx = dx + (int)(originX * axScale);
            int vx0 = cx + sx0;
            int vx1 = cx + sx1;
            int dw = vx1 - vx0;

            swrDrawSprite(renderer, vx0, vy0, dw, dh, surface, sx, sy, sw, sh, color, alpha);
        }
    }
}

// The main loop clears the window (clearFrameBuffer) before every frame, and
// the runner then clears the whole target again with the room's background
// colour before drawing views. On slow memory two full-screen fills per frame
// are expensive, so the first one is only recorded and is skipped when the
// second one arrives. Anything else that could touch the main buffer first
// performs it.
static void swrFlushPendingClear(SWRenderer* swr)
{
    if (!swr->pendingClear) return;
    swr->pendingClear = false;
    if (!swr->mainFb) return;
    swrFillPixels(swr->mainFb, (size_t) swr->mainWidth * swr->mainHeight, swr->pendingClearColor);
}

static void SWRenderer_clearScreen(Renderer* renderer, uint32_t color, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    // A clear of the whole main buffer makes the pending one redundant.
    if (swr->fb == swr->mainFb && swr->fbPitch == swr->width)
        swr->pendingClear = false;
    else
        swrFlushPendingClear(swr);
    
    color = swrConvertPixel(color);
#ifdef TRANSPARENT_MASK
    if (alpha >= 0.95f) {
        color |= TRANSPARENT_MASK;
    }
    else if (alpha < 0.01f) {
        color &= ~TRANSPARENT_MASK;
    }
    else {
    #if PIXEL_SIZE == 32
        int alphai = (int)(255.0f * alpha);
        if (alphai < 0) alphai = 0;
        if (alphai > 255) alphai = 255;
        color |= (alphai << 24);
    #elif PIXEL_SIZE == 16
        color |= (alpha > 0.5f);
    #endif
    }
#else
    if (alpha < 0.5f)
        color = PXL_TRANSPARENT;
#endif
    
    for (int y = 0; y < swr->height; y++) {
        swrFillPixels(&swr->fb[y * swr->fbPitch], (size_t) swr->width, (uintpixel_t) color);
    }
    if (swr->fb == swr->mainFb && swr->fbPitch == swr->width) {
        swr->uniformValid = true;
        swr->uniformColor = (uintpixel_t) color;
    }
}

static void SWRenderer_gpuSetBlendEnable(Renderer* renderer, bool enable)
{
    swrOverlayFlush((SWRenderer*) renderer);
    UNIMP();
    
    SWRenderer* swr = (SWRenderer*) renderer;
    if (!enable)
        swr->blendMode = bm_normal;
}

static void SWRenderer_gpuSetAlphaTestEnable(Renderer* renderer, bool enable)
{
    swrOverlayFlush((SWRenderer*) renderer);
    UNIMP();
    (void)renderer; (void)enable;
}

static void SWRenderer_gpuSetAlphaTestRef(Renderer* renderer, uint8_t ref)
{
    swrOverlayFlush((SWRenderer*) renderer);
    UNIMP();
    (void)renderer; (void)ref;
}

static void SWRenderer_gpuSetBlendMode(Renderer* renderer, int32_t mode)
{
    swrOverlayFlush((SWRenderer*) renderer);
    // these are the only ones we support right now
    if (mode != bm_normal && mode != bm_add && mode != bm_subtract) {
        logWarn("swr: unsupported blend mode %d\n", mode);
        mode = bm_normal;
    }

    SWRenderer* swr = (SWRenderer*) renderer;
    swr->blendMode = mode;
    
    //logDebug("swr: switching to blend mode %d\n", mode);
    
    if (swr->usingAlphaBlendState)
    {
        swr->usingAlphaBlendState = false;
        SWRenderer_gpuSetColorWriteEnable(renderer, true, true, true, true);
    }
}

static void SWRenderer_gpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (!swr->drawingToSurface) {
        logWarn("swr: gpuSetColorWriteEnable not supported for main framebuffer\n");
        return;
    }
    
    logWarn("SWRenderer_gpuSetColorWriteEnable(%d, %d, %d, %d)\n", red, green, blue, alpha);
    SWSurface* currSurf = swr->surfaces[swr->currentSurfaceIndex];
    
    swrCommitShadowWritesToSurfaceIfNeeded(swr, currSurf);
    
    swr->writeMask =
        (red ? WRITE_MASK_RED : 0) |
        (green ? WRITE_MASK_GREEN : 0) |
        (blue ? WRITE_MASK_BLUE : 0) |
        (alpha ? WRITE_MASK_ALPHA : 0);
    
    // no need to change other properties, because the width and height are the same.
    // but we ALWAYS need to re-fetch the writable surface texture since the old one
    // may have been freed.
    swr->fb = swrWritableSurfaceTexture(swr, swr->currentSurfaceIndex)->buffer;
    
    if (currSurf->shadowTexture) {
        assert(currSurf->texture->width == currSurf->shadowTexture->width);
        assert(currSurf->texture->height == currSurf->shadowTexture->height);
        assert(currSurf->texture->buffer != currSurf->shadowTexture->buffer);
    }
}

static void SWRenderer_gpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha)
{
    *red = false;
    *green = false;
    *blue = false;
    *alpha = false;
    
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (!swr->drawingToSurface) {
        logWarn("swr: gpuGetColorWriteEnable not supported for main framebuffer\n");
        return;
    }
    
    *red = (swr->writeMask & WRITE_MASK_RED) != 0;
    *green = (swr->writeMask & WRITE_MASK_GREEN) != 0;
    *blue = (swr->writeMask & WRITE_MASK_BLUE) != 0;
    *alpha = (swr->writeMask & WRITE_MASK_ALPHA) != 0;
}

static void SWRenderer_gpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha, int32_t dfactor_alpha)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    if (sfactor == bm_src_alpha && dfactor == bm_one) {
        swr->blendMode = bm_add;
    }
    else if (sfactor == bm_src_alpha && (dfactor == bm_dest_alpha || dfactor == bm_inv_src_alpha)) {
        swr->blendMode = bm_normal;
    }
    else {
        swr->blendMode = bm_normal;
        logWarn("swr: unsupported ext blend mode combo: sfactor=%d  dfactor=%d\n", sfactor, dfactor);
    }
    
    // alpha handling now
    bool unhandled_alpha = false;
    if (swr->drawingToSurface && (sfactor_alpha == bm_dest_alpha && dfactor_alpha == bm_zero)) {
        swr->usingAlphaBlendState = true;
        SWRenderer_gpuSetColorWriteEnable(renderer, true, true, true, false);
    }
    else {
        swr->usingAlphaBlendState = false;
        SWRenderer_gpuSetColorWriteEnable(renderer, true, true, true, true);
        unhandled_alpha = true;
    }
    
    if (unhandled_alpha) {
        logWarn("swr: unsupported ext blend mode combo: sfactoralpha=%d  dfactoralpha=%d\n", sfactor_alpha, dfactor_alpha);
    }
}

static void SWRenderer_gpuSetTexFilter(Renderer* renderer, bool enable)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
    (void) enable;
    UNIMP();
}

static void SWRenderer_flush(Renderer* renderer)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    UNIMP();
    
    if (swr->drawingToSurface) {
        bool red, green, blue, alpha;
        SWRenderer_gpuGetColorWriteEnable(renderer, &red, &green, &blue, &alpha);
        SWRenderer_gpuSetColorWriteEnable(renderer, red, green, blue, alpha);
    }
}

static bool SWRenderer_gpuGetBlendEnable(Renderer* renderer)
{
    UNIMP();
    (void)renderer;
    return false;
}

static void SWRenderer_gpuSetFog(Renderer* renderer, bool enable, uint32_t color)
{
    swrOverlayFlush((SWRenderer*) renderer);
    UNIMP();
    (void)renderer; (void)enable; (void)color;
}

static int32_t SWRenderer_createSurface(Renderer* renderer, int32_t width, int32_t height)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    int32_t slot = -1;
    for (size_t i = 0; i < swr->surfaceCount; i++)
    {
        if (swr->surfaces[i] == NULL) {
            slot = (int32_t) i;
            break;
        }
    }
    
    if (slot < 0) {
        logError("swr: Could not create surface, too many exist at once.\n");
        return slot;
    }
    
    swr->surfaces[slot] = swrCreateSurface(width, height);
    return slot;
}

static bool SWRenderer_surfaceExists(Renderer* renderer, int32_t surfaceID)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount)
        return false;
    
    return swr->surfaces[surfaceID] != NULL;
}

static bool SWRenderer_setRenderTarget(Renderer* renderer, int32_t surfaceID, bool implicitApplicationSurface)
{
    swrOverlayFlush((SWRenderer*) renderer);
    return swrSwitchToSurface(renderer, surfaceID, implicitApplicationSurface);
}

static float SWRenderer_getSurfaceWidth(Renderer* renderer, int32_t surfaceID)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    if (surfaceID == APPLICATION_SURFACE_ID)
        return (float)(int)((swr->drawingToSurface ? swr->mainWidth : swr->width) / swr->scaleX);
    
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL)
        return 0.0f;
    
    return (float) swr->surfaces[surfaceID]->texture->width;
}

static float SWRenderer_getSurfaceHeight(Renderer* renderer, int32_t surfaceID)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    if (surfaceID == APPLICATION_SURFACE_ID) 
        return (float)(int)((swr->drawingToSurface ? swr->mainHeight : swr->height) / swr->scaleY);
    
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL)
        return 0.0f;
    
    return (float) swr->surfaces[surfaceID]->texture->height;
}

static void SWRenderer_drawSurface(Renderer* renderer, int32_t surfaceID,
                                   int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight,
                                   float x, float y, float xscale, float yscale, float angleDeg,
                                   uint32_t color, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    SWTexture* surface, localSurface;
    if (surfaceID == APPLICATION_SURFACE_ID) {
        localSurface.buffer = swr->drawingToSurface ? swr->mainFb : swr->fb;
        localSurface.width = swr->drawingToSurface ? swr->mainWidth : swr->width;
        localSurface.height = swr->drawingToSurface ? swr->mainHeight : swr->height;
        surface = &localSurface;
    } else {
        if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL) {
            logError("swr: Invalid surface id %d for drawSurface\n", surfaceID);
            return;
        }

        swrCommitShadowWritesToSurfaceIfNeeded(swr, swr->surfaces[surfaceID]);
        surface = swr->surfaces[surfaceID]->texture;
    }
    
    if (srcWidth < 0) {
        srcWidth = surface->width;
        swrReverseTransformSizeIfNeeded(swr, &xscale, NULL);
    }
    if (srcHeight < 0) {
        srcHeight = surface->height;
        swrReverseTransformSizeIfNeeded(swr, NULL, &yscale);
    }
    
    int sx = srcLeft;
    int sy = srcTop;
    int sw = srcWidth;
    int sh = srcHeight;
    
    int tw = (int)(srcWidth * swr->scaleX);
    int th = (int)(srcHeight * swr->scaleY);
    
    float dx = x;
    float dy = y;
    int dw = (int)(xscale * tw);
    int dh = (int)(yscale * th);

    if (UNLIKELY(swrMustRotate(angleDeg)))
    {
        float pivotX = (x - dx) * swrSgn(xscale);
        float pivotY = (y - dy) * swrSgn(yscale);
        
        if (tw != sw)
            pivotX *= (float)tw / sw;
        if (th != sh)
            pivotY *= (float)th / sh;
        
        swrDrawSpriteRotated(renderer, dx, dy, dw, dh, surface, sx, sy, sw, sh, color, alpha, angleDeg, pivotX, pivotY);
    }
    else
    {
        swrDrawSprite(renderer, dx, dy, dw, dh, surface, sx, sy, sw, sh, color, alpha);
    }
}

static void SWRenderer_surfaceResize(Renderer* renderer, int32_t surfaceID, int32_t width, int32_t height)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (surfaceID == APPLICATION_SURFACE_ID) {
        logError("swr: Don't support resizing the application window with this.  There must be another way! (need to set to %dx%d)\n", width, height);
        return;
    }
    
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL) {
        logError("swr: Cannot resize surface id %d, it's invalid\n", surfaceID);
        return;
    }
    
    swrFreeSurface(swr->surfaces[surfaceID]);
    swr->surfaces[surfaceID] = swrCreateSurface(width, height);
}

static void SWRenderer_surfaceFree(Renderer* renderer, int32_t surfaceID)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (surfaceID == APPLICATION_SURFACE_ID) {
        logError("swr: Don't support SWRenderer_surfaceCopy the application window with this.  There must be another way!\n");
        return;
    }
    
    if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL) {
        logError("swr: Cannot resize surface id %d, it's invalid\n", surfaceID);
        return;
    }
    
    swrFreeSurface(swr->surfaces[surfaceID]);
    swr->surfaces[surfaceID] = NULL;
}

static void SWRenderer_surfaceCopy(Renderer* renderer,
                                   int32_t DestSurfaceID, int32_t DestX, int32_t DestY,
                                   int32_t SrcSurfaceID, int32_t SrcX, int32_t SrcY,
                                   int32_t SrcW, int32_t SrcH, bool part)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    SWTexture temp1, temp2;
    SWTexture *dstSurf, *srcSurf;
    bool freeSrcSurf = false;
    
    if (DestSurfaceID == APPLICATION_SURFACE_ID) {
        dstSurf = &temp1;
        temp1.width = swr->mainWidth;
        temp1.height = swr->mainHeight;
        temp1.buffer = swr->mainFb;
    }
    else if (DestSurfaceID < 0 || (size_t) DestSurfaceID >= swr->surfaceCount || swr->surfaces[DestSurfaceID] == NULL) {
        logError("swr: Cannot resize surface id %d, it's invalid (dest in surfaceCopy)\n", DestSurfaceID);
        return;
    }
    else {
        dstSurf = swrWritableSurfaceTexture(swr, DestSurfaceID);
    }
    
    if (SrcSurfaceID == APPLICATION_SURFACE_ID) {
        // TODO: resizing might be expensive
        temp2.width = swr->mainWidth;
        temp2.height = swr->mainHeight;
        temp2.buffer = swr->mainFb;
        if (swr->mainWidth == swr->portW && swr->mainHeight == swr->portH) {
            srcSurf = &temp2;
        }
        else {
            srcSurf = swrCropSectionFromTexture(&temp2, swr->gameW, swr->gameH, swr->portX, swr->portY, swr->maxX, swr->maxY);
            freeSrcSurf = true;
        }
    }
    else if (SrcSurfaceID < 0 || (size_t) SrcSurfaceID >= swr->surfaceCount || swr->surfaces[SrcSurfaceID] == NULL) {
        logError("swr: Cannot resize surface id %d, it's invalid (src in surfaceCopy)\n", SrcSurfaceID);
        return;
    }
    else {
        swrCommitShadowWritesToSurfaceIfNeeded(swr, swr->surfaces[SrcSurfaceID]);
        srcSurf = swr->surfaces[SrcSurfaceID]->texture;
    }

    if (!part)
    {
        // Not "part" means copy the whole source texture.
        SrcX = SrcY = 0;
        SrcW = srcSurf->width;
        SrcH = srcSurf->height;
    }
    
    if (SrcX + SrcW < 0) return;
    if (SrcY + SrcH < 0) return;
    if (SrcX >= srcSurf->width) return;
    if (SrcY >= srcSurf->height) return;
    if (DestX + SrcW < 0) return;
    if (DestY + SrcH < 0) return;
    if (DestX >= dstSurf->width) return;
    if (DestY >= dstSurf->height) return;
    
    if (SrcY < 0) {
        SrcH += SrcY;
        DestY -= SrcY;
        SrcY = 0;
    }
    if (SrcX < 0) {
        SrcW += SrcX;
        DestX -= SrcX;
        SrcX = 0;
    }
    if (SrcX + SrcW >= srcSurf->width)
        SrcW = srcSurf->width - SrcX;
    if (SrcY + SrcH >= srcSurf->height)
        SrcH = srcSurf->height - SrcY;
    
    if (DestX + SrcW >= dstSurf->width)
        SrcW = dstSurf->width - DestX;
    if (DestY + SrcH >= dstSurf->height)
        SrcH = dstSurf->height - DestY;
    
    for (int dy = 0; dy < SrcH; dy++) {
        /***/ uintpixel_t* dstLine = &dstSurf->buffer[(dy + DestY) * dstSurf->width];
        const uintpixel_t* srcLine = &srcSurf->buffer[(dy + SrcY)  * srcSurf->width];
        for (int dx = 0; dx < SrcW; dx++) {
            dstLine[dx + DestX] = srcLine[dx + SrcX];
        }
    }
    
    if (freeSrcSurf)
        swrFreeTexture(srcSurf);
}

static bool SWRenderer_surfaceGetPixels(Renderer* renderer, int32_t surfaceID, uint8_t* outRGBA)
{
    swrOverlayFlush((SWRenderer*) renderer);
    UNIMP();
    (void)renderer; (void)surfaceID; (void)outRGBA;
    return false;
}

static int32_t SWRenderer_gpuGetBlendMode(Renderer* renderer)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    return swr->blendMode;
}

static int32_t SWRenderer_createSpriteFromSurface(Renderer* renderer, int32_t surfaceID,
                                                   int32_t x, int32_t y, int32_t w, int32_t h,
                                                   bool removeback, bool smooth,
                                                   int32_t xorig, int32_t yorig)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    (void) smooth;
    
    int cropLeft = x;
    int cropTop = y;
    int cropRight = x + w;
    int cropBottom = y + h;
    
    SWTexture *srcTex, temp1;
    if (surfaceID == APPLICATION_SURFACE_ID)
    {
        srcTex = &temp1;
        temp1.width = swr->width;
        temp1.height = swr->height;
        temp1.buffer = swr->fb;
        
        swrTransformPosIntIfNeeded(swr, &cropLeft, &cropTop);
        swrTransformPosIntIfNeeded(swr, &cropRight, &cropBottom);
    }
    else
    {
        if (surfaceID < 0 || (size_t) surfaceID >= swr->surfaceCount || swr->surfaces[surfaceID] == NULL){
            logError("%s: Invalid surface ID %d\n", __func__, surfaceID);
            return -1;
        }
        SWSurface* surf = swr->surfaces[surfaceID];
        swrCommitShadowWritesToSurfaceIfNeeded(swr, surf);
        srcTex = surf->texture;
    }
    
    int32_t texturePageId = swrFindSurfaceTextureSlot(swr);
    int32_t tpagIndex = swrFindSurfaceTPagSlot(swr);
    if (texturePageId == -1 || tpagIndex == -1) {
        logError("%s: Sprite overflow!!\n", __func__);
        return 0;
    }
    
    SWTexture* tex = swrCropSectionFromTexture(srcTex, w, h, cropLeft, cropTop, cropRight, cropBottom);
    if (removeback)
        swrRemoveBackgroundFromTexture(tex);
    
    swr->textures[texturePageId] = tex;

    // TODO[MrPowerGamerBR]: This is supposed to be refactored, not to modify data.win structs directly.
    DataWin* dw = swr->base.dataWin;
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t) w;
    tpag->sourceHeight = (uint16_t) h;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t) w;
    tpag->targetHeight = (uint16_t) h;
    tpag->boundingWidth = (uint16_t) w;
    tpag->boundingHeight = (uint16_t) h;
    tpag->texturePageId = texturePageId;
    
    uint32_t spriteIndex = DataWin_allocSpriteSlot(dw, swr->originalSpriteCount);
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    // name was set by DataWin_allocSpriteSlot ("__newsprite<N>"); don't overwrite it here
    sprite->width = (uint32_t) tpag->targetWidth;
    sprite->height = (uint32_t) tpag->targetHeight;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1;
    sprite->tpagIndices = (int32_t*) safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t) tpagIndex;
    sprite->maskCount = 0;
    sprite->masks = nullptr;

    logInfo("%s: Allocated surface sprite with ID %d\n", __func__, spriteIndex);
    return spriteIndex;
}

static void SWRenderer_deleteSprite(Renderer* renderer, int32_t spriteIndex)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    DataWin* dw = renderer->dataWin;
    if (0 > spriteIndex || dw->sprt.count <= (uint32_t) spriteIndex) return;

    // Refuse to delete original data.win sprites
    if (swr->originalSpriteCount > (uint32_t) spriteIndex) {
        logError("%s: Cannot delete sprite with index %d, it's invalid.\n", __func__, spriteIndex);
        return;
    }

    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0) return; // already deleted

    for (uint32_t i = 0; i < sprite->textureCount; i++)
    {
        int32_t tpagIdx = sprite->tpagIndices[i];
        if (tpagIdx >= 0 && (uint32_t) tpagIdx >= swr->originalTPagCount) {
            TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
            int16_t pageId = tpag->texturePageId;
            if (pageId >= 0 && swr->totalTextureCount > (uint32_t) pageId) {
                swrFreeTexture(swr->textures[pageId]);
                swr->textures[pageId] = NULL;
            }
            // Mark TPAG slot as free for reuse
            tpag->texturePageId = -1;
        }
    }
    
    free(sprite->tpagIndices);
    sprite->tpagIndices = NULL;
    
    const char* keepName = sprite->name;
    memset(sprite, 0, sizeof(Sprite));
    sprite->name = keepName;

    logInfo("SWR: Deleted sprite %d\n", spriteIndex);
}

static void SWRenderer_drawTiledPart(Renderer* renderer, int32_t tpagIndex,
                                     int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH,
                                     float dstX, float dstY, float dstW, float dstH,
                                     uint32_t color, float alpha)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    UNIMP();
    (void)renderer; (void)tpagIndex;
    (void)srcX; (void)srcY; (void)srcW; (void)srcH;
    (void)dstX; (void)dstY; (void)dstW; (void)dstH;
    (void)color; (void)alpha;
}

static int32_t SWRenderer_ensureApplicationSurface(Renderer* renderer, int32_t width, int32_t height)
{
    swrOverlayFlush((SWRenderer*) renderer);
    // We don't evict surfaces, and especially not the primary framebuffer,
    // but if we did, this is where we would restore it.
    (void) renderer;
    (void) width;
    (void) height;
    
    return APPLICATION_SURFACE_ID;
}

static RendererVtable swrVtable;

void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    uintpixel_t pxcolor = swrConvertPixel(color);
    
#ifdef SW_PLATFORM_FRAMEBUFFER
    // This is the first thing drawn each frame, so it has to go to the buffer
    // the coming frame will use, not the one that was just presented.
    if (swr->fbIsPlatform && !swrAcquirePlatformFramebuffer(swr, swr->width, swr->height)) return;
#endif
    swr->pendingClear = true;
    swr->pendingClearColor = pxcolor;
}

static uint32_t SWRenderer_spriteGetTexture(Renderer* renderer, int32_t tpagIndex)
{
    (void) renderer;

    return (uint32_t) tpagIndex + 1;
}

static uint32_t SWRenderer_surfaceGetTexture(Renderer* renderer, int32_t surfaceID)
{
    (void) renderer;
    (void) surfaceID;

    return (uint32_t) -1;
}

static float SWRenderer_textureGetTexelWidth(Renderer* renderer, uint32_t texID)
{
    (void) renderer;
    (void) texID;
    
    return 1.0f;
}

static float SWRenderer_textureGetTexelHeight(Renderer* renderer, uint32_t texID)
{
    (void) renderer;
    (void) texID;
    
    return 1.0f;
}

static bool SWRenderer_textureGetUVs(Renderer* renderer, uint32_t texID, float* outUVs)
{
    (void) renderer;
    (void) texID;
    (void) outUVs;
    
    return false;
}

static void SWRenderer_textureSetStage(Renderer* renderer, int32_t slot, uint32_t texID)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
    (void) slot;
    (void) texID;
}

static bool SWRenderer_shaderIsCompiled(Renderer* renderer, int32_t shader)
{
    (void) renderer;
    (void) shader;
    
    return false;
}

static bool SWRenderer_shadersSupported(void)
{
    return false;
}

static void SWRenderer_gpuSetShader(Renderer* renderer, int32_t shaderIndex)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
    (void) shaderIndex;
}

static void SWRenderer_gpuResetShader(Renderer* renderer)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
}

static int32_t SWRenderer_shaderGetUniform(Renderer* renderer, int32_t shaderIndex, char* uniform)
{
    (void) renderer;
    (void) shaderIndex;
    (void) uniform;
    
    return 0;
}

static int32_t SWRenderer_shaderGetSamplerIndex(Renderer* renderer, int32_t shaderIndex, char* uniform)
{
    (void) renderer;
    (void) shaderIndex;
    (void) uniform;
    
    return 0;
}

static void SWRenderer_shaderSetUniformF(Renderer* renderer, int32_t handle, int32_t count, float value1, float value2, float value3, float value4)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
    (void) handle;
    (void) count;
    (void) value1;
    (void) value2;
    (void) value3;
    (void) value4;
}

static void SWRenderer_shaderSetUniformI(Renderer* renderer, int32_t handle, int32_t count, int32_t value1, int32_t value2, int32_t value3, int32_t value4)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
    (void) handle;
    (void) count;
    (void) value1;
    (void) value2;
    (void) value3;
    (void) value4;
}

static void SWRenderer_applyProjection(Renderer* renderer, const Matrix4f* worldToClip, const Matrix4f* idk)
{
    swrOverlayFlush((SWRenderer*) renderer);
    (void) renderer;
    (void) worldToClip;
    (void) idk;
    UNIMP();
}

static void SWRenderer_primitiveBegin(Renderer* renderer, int32_t primitiveType)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    swr->primitiveType = primitiveType;
    swr->vertexCount = 0;
    swr->primitiveBegun = true;
    //logDebug("swr: primitiveBegin(%d)\n", primitiveType);
}

static void SWRenderer_primitiveBeginTexture(Renderer* renderer, int32_t primitiveType, int32_t texture)
{
    swrOverlayFlush((SWRenderer*) renderer);
    static bool shownWarning = false;
    if (!shownWarning) {
        shownWarning = true;
        logError("SWR: Do not support primitiveBeginTexture.  Redirect to primitiveBegin.\n");
    }
    
    (void) texture;
    SWRenderer_primitiveBegin(renderer, primitiveType);
}

static void swrPrimitiveLine(Renderer* renderer, SWVertex* vtx0, SWVertex* vtx1)
{
    float alphaAvg = (swrGetAlpha(vtx0->color) + swrGetAlpha(vtx1->color)) / 2;
    SWRenderer_drawLineColor(renderer, vtx0->x, vtx0->y, vtx1->x, vtx1->y, 1.0f, vtx0->color, vtx1->color, alphaAvg);
}

static void swrPrimitiveTriangle(Renderer* renderer, SWVertex* vtx0, SWVertex* vtx1, SWVertex* vtx2)
{
    const bool bWireFrameMode = false; // for debugging
    
    float alphaAvg = (swrGetAlpha(vtx0->color) + swrGetAlpha(vtx1->color) + swrGetAlpha(vtx2->color)) / 3;
    SWRenderer_drawTriangle(renderer, vtx0->x, vtx0->y, vtx1->x, vtx1->y, vtx2->x, vtx2->y, vtx0->color, vtx1->color, vtx2->color, alphaAvg, bWireFrameMode);
}

static void SWRenderer_primitiveEnd(Renderer* renderer)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    //logWarn("SWR: Ending with %d vertices, primitive type %d\n", swr->vertexCount, swr->primitiveType);
    switch (swr->primitiveType)
    {
        case PRIMITIVE_POINTS:
        {
            for (int i = 0; i < swr->vertexCount; i++)
            {
                SWVertex *vtx = &swr->vertexData[i];
                swrPlotPixel(renderer, vtx->x, vtx->y, swrConvertPixel(vtx->color), swrGetAlpha(vtx->color));
            }
            break;
        }
        case PRIMITIVE_LINES:
        {
            for (int i = 0; i + 1 < swr->vertexCount; i += 2)
                swrPrimitiveLine(renderer, &swr->vertexData[i], &swr->vertexData[i + 1]);
            
            break;
        }
        case PRIMITIVE_LINE_STRIP:
        {
            for (int i = 0; i + 1 < swr->vertexCount; i++)
                swrPrimitiveLine(renderer, &swr->vertexData[i], &swr->vertexData[i + 1]);
            
            break;
        }
        case PRIMITIVE_TRIANGLES:
        {
            for (int i = 0; i + 2 < swr->vertexCount; i += 3)
                swrPrimitiveTriangle(renderer, &swr->vertexData[i], &swr->vertexData[i + 1], &swr->vertexData[i + 2]);
            
            break;
        }
        case PRIMITIVE_TRIANGLE_STRIP:
        {
            if (swr->vertexCount < 3)
                break;
            
            for (int i = 0; i < swr->vertexCount; i++)
                swrPrimitiveTriangle(renderer, &swr->vertexData[i], &swr->vertexData[i + 1], &swr->vertexData[i + 2]);
            
            break;
        }
        case PRIMITIVE_TRIANGLE_FAN:
        {
            for (int i = 1; i + 1 < swr->vertexCount; i += 2)
                swrPrimitiveTriangle(renderer, &swr->vertexData[0], &swr->vertexData[i], &swr->vertexData[i + 1]);
            
            break;
        }
        default:
        {
            logError("SWR: Unimplemented primitive type %d", swr->primitiveType);
            break;
        }
    }
    
    swr->primitiveBegun = false;
    swr->vertexCount = 0;
}

static void SWRenderer_drawVertex(Renderer* renderer, float x, float y, float z, uint32_t color, float alphaMod, float u, float v)
{
    swrOverlayFlush((SWRenderer*) renderer);
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (swr->vertexCount >= swr->maxVertexCount)
    {
        // TODO: we could just expand this?
        if (!swr->primitiveOverflow)
            logError("SWR: Vertex overflow.  Vertices will be ignored until the next primitiveEnd.");
        
        swr->primitiveOverflow = true;
    }
    
    SWVertex* pVertex = &swr->vertexData[swr->vertexCount++];
    
    Pixel32ABGR pixel;
    pixel.l = color;
    pixel.p.a = (uint8_t)(pixel.p.a * alphaMod);
    
    pVertex->color = swrConvertPixel(pixel.l);
    pVertex->x = x;
    pVertex->y = y;
    
    //logDebug("swr:      drawVertex(%f, %f)\n", x, y);
    
    // Texture mapping and the third dimension are not supported.
    (void) z;
    (void) u;
    (void) v;
}

static void SWRenderer_drawVertexBuffer(Renderer* renderer, VertexBuffer* buffer, int32_t primitive, int32_t texture, int32_t offset, int32_t count)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return;
    swrOverlayFlush((SWRenderer*) renderer);
    // TODO
    
    (void) renderer;
    (void) buffer;
    (void) primitive;
    (void) texture;
    (void) offset;
    (void) count;
    
    UNIMP();
}
// ===[ Tile run cache ]===
// A room's tiles are hundreds of small draws that come out the same every
// frame. A run of them that nothing else is drawn between is composed once
// into a picture the size of the run's bounding box, in room coordinates, and
// that picture is drawn in their place for as long as the run stays the same.
// The key is a hash of everything that decides what the tiles look like, so a
// game that moves, adds, hides or recolours tiles just gets a new picture.
#define SWR_TILE_RUN_ENTRIES 4
#define SWR_TILE_RUN_MIN_TILES 16
#define SWR_TILE_RUN_MAX_PIXELS (1024 * 1024)
#define SWR_TILE_RUN_MAX_SIDE 4096
#define SWR_TILE_RUN_IDLE_FRAMES 300

typedef struct {
    uint64_t key;
    uintpixel_t* pixels;    // NULL when the entry is free
    int x, y, width, height;
    uint32_t lastUsedFrame;
} SWTileRun;

static SWTileRun swrTileRuns[SWR_TILE_RUN_ENTRIES];

bool swrTileRunsFree(void)
{
    bool freed = false;
    for (int e = 0; e < SWR_TILE_RUN_ENTRIES; e++) {
        if (swrTileRuns[e].pixels == NULL) continue;
        free(swrTileRuns[e].pixels);
        swrTileRuns[e].pixels = NULL;
        freed = true;
    }
    return freed;
}

static uint64_t swrTileRunHash(uint64_t hash, const void* data, size_t bytes)
{
    const uint8_t* at = (const uint8_t*) data;
    for (size_t i = 0; i < bytes; i++) hash = (hash ^ at[i]) * 1099511628211ull;
    return hash;
}

// Draws the run from its cached picture, building it first if need be. Returns false if the run cannot be drawn that way.
static bool swrDrawTileRunCached(Renderer* renderer, RoomTile** tiles, const float* offsets, int32_t count)
{
    SWRenderer* swr = (SWRenderer*) renderer;
#if PIXEL_SIZE != 16
    (void) swr; (void) tiles; (void) offsets; (void) count;
    return false;
#else
    // The picture is in room pixels and holds no partial coverage, so it can
    // only stand in for tiles drawn unscaled, opaque and with normal blending.
    if (count < SWR_TILE_RUN_MIN_TILES) return false;
    if (swr->drawingToSurface || swr->blendMode != bm_normal) return false;
    if (swr->scaleX != 1.0f || swr->scaleY != 1.0f) return false;
    
    uint64_t key = 14695981039346656037ull;
    int left = INT32_MAX, top = INT32_MAX, right = INT32_MIN, bottom = INT32_MIN;
    for (int32_t t = 0; t < count; t++)
    {
        const RoomTile* tile = tiles[t];
        if (tile->scaleX != 1.0f || tile->scaleY != 1.0f || tile->alpha < 1.0f) return false;
        
        int tileLeft = swrFloor((float) tile->x + offsets[t * 2]);
        int tileTop = swrFloor((float) tile->y + offsets[t * 2 + 1]);
        if (tileLeft < left) left = tileLeft;
        if (tileTop < top) top = tileTop;
        if (tileLeft + (int) tile->width > right) right = tileLeft + (int) tile->width;
        if (tileTop + (int) tile->height > bottom) bottom = tileTop + (int) tile->height;
        
        int32_t ints[7] = { tile->x, tile->y, tile->useSpriteDefinition, tile->backgroundDefinition, tile->sourceX, tile->sourceY, (int32_t) tile->color };
        uint32_t size[2] = { tile->width, tile->height };
        key = swrTileRunHash(key, ints, sizeof(ints));
        key = swrTileRunHash(key, size, sizeof(size));
        key = swrTileRunHash(key, &offsets[t * 2], 2 * sizeof(float));
    }
    
    int width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0 || width > SWR_TILE_RUN_MAX_SIDE || height > SWR_TILE_RUN_MAX_SIDE) return false;
    if (width * height > SWR_TILE_RUN_MAX_PIXELS) return false;
    
    // Find the picture, or the entry to build it in: a free one, else the one unused for longest.
    SWTileRun* run = NULL;
    SWTileRun* spare = &swrTileRuns[0];
    for (int e = 0; e < SWR_TILE_RUN_ENTRIES; e++)
    {
        SWTileRun* entry = &swrTileRuns[e];
        if (entry->pixels != NULL && swr->frameCounter - entry->lastUsedFrame > SWR_TILE_RUN_IDLE_FRAMES) {
            free(entry->pixels);
            entry->pixels = NULL;
        }
        if (entry->pixels != NULL && entry->key == key) { run = entry; break; }
        if (entry->pixels == NULL) {
            if (spare->pixels != NULL) spare = entry;
        } else if (spare->pixels != NULL && entry->lastUsedFrame < spare->lastUsedFrame) {
            spare = entry;
        }
    }
    
    if (run == NULL)
    {
        // An entry used this frame belongs to another run of the room being drawn; leave it be.
        if (spare->pixels != NULL && spare->lastUsedFrame == swr->frameCounter) return false;
        free(spare->pixels);
        spare->pixels = NULL;
        
        uintpixel_t* pixels = (uintpixel_t*) calloc((size_t) width * height, sizeof(uintpixel_t));
        if (pixels == NULL) return false;
        
        // Point the renderer at the picture, with a view that maps the run's
        // bounding box onto it, and draw the tiles the ordinary way.
        SWRenderer saved = *swr;
        swr->fb = pixels;
        swr->fbPitch = (uint16_t) width;
        swr->viewX = left; swr->viewY = top;
        swr->portX = 0; swr->portY = 0;
        swr->portW = width; swr->portH = height;
        swr->maxX = width; swr->maxY = height;
        for (int32_t t = 0; t < count; t++)
            Renderer_drawTile(renderer, tiles[t], offsets[t * 2], offsets[t * 2 + 1]);
        swrOverlayFlush(swr);
        swr->fb = saved.fb;
        swr->fbPitch = saved.fbPitch;
        swr->viewX = saved.viewX; swr->viewY = saved.viewY;
        swr->portX = saved.portX; swr->portY = saved.portY;
        swr->portW = saved.portW; swr->portH = saved.portH;
        swr->maxX = saved.maxX; swr->maxY = saved.maxY;
        
        run = spare;
        run->key = key;
        run->pixels = pixels;
        run->x = left; run->y = top;
        run->width = width; run->height = height;
    }
    run->lastUsedFrame = swr->frameCounter;
    
    SWTexture picture;
    picture.buffer = run->pixels;
    picture.width = (uint16_t) run->width;
    picture.height = (uint16_t) run->height;
    picture.originX = picture.originY = 0;
    picture.lastUsedFrame = swr->frameCounter;
    swrDrawSprite(renderer, (float) run->x, (float) run->y, (float) run->width, (float) run->height,
                  &picture, 0, 0, run->width, run->height, 0xFFFFFF, 1.0f);
    return true;
#endif
}

// A run that cannot be drawn from a picture (too big, scaled, translucent, a
// few tiles only) is drawn tile by tile, leaving out the tiles that cannot
// reach the port: a room's tiles are mostly somewhere else, and each one
// costs a whole sprite draw to find that out further down.
static bool SWRenderer_drawTileRun(Renderer* renderer, RoomTile** tiles, const float* offsets, int32_t count)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return true;
    SWRenderer* swr = (SWRenderer*) renderer;
    swrOverlayFlush(swr);
    if (swrDrawTileRunCached(renderer, tiles, offsets, count)) return true;
    
    for (int32_t t = 0; t < count; t++)
    {
        const RoomTile* tile = tiles[t];
        // Where the tile lands in the buffer, whichever way its scale points, with a pixel to spare.
        float x = ((float) tile->x + offsets[t * 2] - (float) swr->viewX) * swr->scaleX + (float) swr->portX;
        float y = ((float) tile->y + offsets[t * 2 + 1] - (float) swr->viewY) * swr->scaleY + (float) swr->portY;
        float w = (float) tile->width * tile->scaleX * swr->scaleX, h = (float) tile->height * tile->scaleY * swr->scaleY;
        float left = w < 0.0f ? x + w : x, right = w < 0.0f ? x : x + w;
        float top = h < 0.0f ? y + h : y, bottom = h < 0.0f ? y : y + h;
        if (right < (float) swr->portX - 1.0f || left > (float) swr->maxX + 1.0f) continue;
        if (bottom < (float) swr->portY - 1.0f || top > (float) swr->maxY + 1.0f) continue;
        Renderer_drawTile(renderer, tiles[t], offsets[t * 2], offsets[t * 2 + 1]);
    }
    return true;
}

// ===[ Tile layers ]===
// A GMS2 room is built from tile layers: a grid of cells, each naming a tile
// of one tileset. Drawn cell by cell through drawSpritePart, a layer costs a
// sprite draw for every cell of the room on every frame, in view or not.
// Here only the cells in view are drawn, and a cell that is neither mirrored,
// flipped nor rotated is copied straight from the tileset: a row at a time
// when the tile has no transparent pixels, not at all when it has nothing
// but. What comes out is what the cell-by-cell draws would have produced.
#define SWR_TILESETS 8
enum { SWR_TILE_UNKNOWN, SWR_TILE_EMPTY, SWR_TILE_SOLID, SWR_TILE_MIXED };

typedef struct {
    int32_t tpagIndex;
    uint32_t count;
    uint8_t* kinds;     // one SWR_TILE_* per tile index; NULL when the entry is free
    uint32_t lastUsedFrame;
} SWTileset;

static SWTileset swrTilesets[SWR_TILESETS];
bool swrTileLayerFast = true;

static uint8_t* swrTileKinds(SWRenderer* swr, int32_t tpagIndex, uint32_t count)
{
    SWTileset* spare = &swrTilesets[0];
    for (int e = 0; e < SWR_TILESETS; e++) {
        SWTileset* entry = &swrTilesets[e];
        if (entry->kinds != NULL && entry->tpagIndex == tpagIndex && entry->count == count) {
            entry->lastUsedFrame = swr->frameCounter;
            return entry->kinds;
        }
        if (entry->kinds == NULL) {
            if (spare->kinds != NULL) spare = entry;
        } else if (spare->kinds != NULL && entry->lastUsedFrame < spare->lastUsedFrame)
            spare = entry;
    }
    uint8_t* kinds = (uint8_t*) calloc((size_t) count + 1, 1);
    if (kinds == NULL) return NULL;
    free(spare->kinds);
    spare->kinds = kinds;
    spare->tpagIndex = tpagIndex;
    spare->count = count;
    spare->lastUsedFrame = swr->frameCounter;
    return kinds;
}

static bool SWRenderer_drawTileLayer(Renderer* renderer, Background* tileset, const uint32_t* cells,
                                     uint32_t tilesX, uint32_t tilesY, float offsetX, float offsetY)
{
    if (SWR_SKIPPED((SWRenderer*) renderer)) return true;
    SWRenderer* swr = (SWRenderer*) renderer;
#if PIXEL_SIZE != 16
    (void) tileset; (void) cells; (void) tilesX; (void) tilesY; (void) offsetX; (void) offsetY;
    return false;
#else
    DataWin* dwin = renderer->dataWin;
    int32_t tpagIndex = tileset->tpagIndex;
    if (!swrTileLayerFast || tpagIndex < 0 || (uint32_t) tpagIndex >= dwin->tpag.count) return false;
    if (swr->scaleX <= 0.0f || swr->scaleY <= 0.0f || tilesX == 0 || tilesY == 0) return false;
    swrOverlayFlush(swr);
    
    int tileW = (int) tileset->gms2TileWidth, tileH = (int) tileset->gms2TileHeight;
    int borderX = (int) tileset->gms2OutputBorderX, borderY = (int) tileset->gms2OutputBorderY;
    uint32_t columns = tileset->gms2TileColumns;
    
    // The cells that can reach the port, with one to spare each way for a rotated tile.
    float viewLeft = (float) swr->viewX - offsetX, viewTop = (float) swr->viewY - offsetY;
    float viewRight = viewLeft + (float) swr->portW / swr->scaleX, viewBottom = viewTop + (float) swr->portH / swr->scaleY;
    int firstX = swrFloor(viewLeft / (float) tileW) - 1, lastX = swrFloor(viewRight / (float) tileW) + 1;
    int firstY = swrFloor(viewTop / (float) tileH) - 1, lastY = swrFloor(viewBottom / (float) tileH) + 1;
    if (firstX < 0) firstX = 0;
    if (firstY < 0) firstY = 0;
    if (lastX > (int) tilesX - 1) lastX = (int) tilesX - 1;
    if (lastY > (int) tilesY - 1) lastY = (int) tilesY - 1;
    
    // A copy stands in for a draw only at full size, with normal blending, of an item stored at the size it is drawn.
    TexturePageItem* tpag = &dwin->tpag.items[tpagIndex];
    bool direct = swr->scaleX == 1.0f && swr->scaleY == 1.0f && swr->blendMode == bm_normal &&
                  tpag->sourceWidth == tpag->targetWidth && tpag->sourceHeight == tpag->targetHeight;
    uint8_t* kinds = direct ? swrTileKinds(swr, tpagIndex, tileset->gms2TileCount) : NULL;
    SWTexture* texture = kinds != NULL ? swrTextureForItem(swr, tpagIndex) : NULL;
    
    for (int ty = firstY; ty <= lastY; ty++)
    {
        for (int tx = firstX; tx <= lastX; tx++)
        {
            uint32_t cell = cells[(uint32_t) ty * tilesX + (uint32_t) tx];
            uint32_t tileIndex = cell & 0x0007FFFF;
            if (tileIndex == 0 || tileIndex > tileset->gms2TileCount) continue;
            
            int srcX = (int) (tileIndex % columns) * (tileW + 2 * borderX) + borderX;
            int srcY = (int) (tileIndex / columns) * (tileH + 2 * borderY) + borderY;
            bool mirror = (cell & 0x10000000) != 0, flip = (cell & 0x20000000) != 0, rotate = (cell & 0x40000000) != 0;
            
            if (texture != NULL && !mirror && !flip && !rotate)
            {
                int sx = tpag->sourceX + srcX - texture->originX;
                int sy = tpag->sourceY + srcY - texture->originY;
                if (sx >= 0 && sy >= 0 && sx + tileW <= texture->width && sy + tileH <= texture->height)
                {
                    uint8_t kind = kinds[tileIndex];
                    if (kind == SWR_TILE_UNKNOWN)
                    {
                        int opaque = 0;
                        for (int y = 0; y < tileH; y++) {
                            const uintpixel_t* src = &texture->buffer[(sy + y) * texture->width + sx];
                            for (int x = 0; x < tileW; x++) opaque += swrIsOpaque(src[x]) ? 1 : 0;
                        }
                        kind = opaque == 0 ? SWR_TILE_EMPTY : opaque == tileW * tileH ? SWR_TILE_SOLID : SWR_TILE_MIXED;
                        kinds[tileIndex] = kind;
                    }
                    if (kind == SWR_TILE_EMPTY) continue;
                    
                    float fx = (float) (tx * tileW) + offsetX, fy = (float) (ty * tileH) + offsetY;
                    swrTransformPosIfNeeded(swr, &fx, &fy);
                    int dx = swrFloor(fx), dy = swrFloor(fy);
                    int x0 = dx < swr->portX ? swr->portX : dx, x1 = dx + tileW > swr->maxX ? swr->maxX : dx + tileW;
                    int y0 = dy < swr->portY ? swr->portY : dy, y1 = dy + tileH > swr->maxY ? swr->maxY : dy + tileH;
                    if (x0 >= x1 || y0 >= y1) continue;
                    
                    int width = x1 - x0;
                    for (int y = y0; y < y1; y++)
                    {
                        const uintpixel_t* src = &texture->buffer[(sy + (y - dy)) * texture->width + sx + (x0 - dx)];
                        uintpixel_t* dst = &swr->fb[y * swr->fbPitch + x0];
                        if (kind == SWR_TILE_SOLID)
                            memcpy(dst, src, (size_t) width * sizeof(uintpixel_t));
                        else for (int x = 0; x < width; x++) {
                            uintpixel_t pixel = src[x];
                            if (swrIsOpaque(pixel)) dst[x] = pixel;
                        }
                    }
                    continue;
                }
            }
            
            // Anything else is drawn the way the runner would have drawn it.
            float angleDeg = rotate ? 90.0f : 0.0f;
            float pivotX = (float) (tx * tileW) + offsetX + (float) tileW / 2.0f;
            float pivotY = (float) (ty * tileH) + offsetY + (float) tileH / 2.0f;
            float dstX = (float) (tx * tileW) + offsetX + (mirror ? (float) tileW : 0.0f);
            float dstY = (float) (ty * tileH) + offsetY + (flip ? (float) tileH : 0.0f);
            SWRenderer_drawSpritePart(renderer, tpagIndex, (float) srcX, (float) srcY, (float) tileW, (float) tileH, dstX, dstY,
                                      mirror ? -1.0f : 1.0f, flip ? -1.0f : 1.0f, angleDeg, pivotX, pivotY, 0xFFFFFF, 1.0f);
            if (texture != NULL) texture = swrTextureForItem(swr, tpagIndex); // the draw may have moved it
        }
    }
    return true;
#endif
}

#ifdef SW_DRAW_PROFILE
// Times each kind of draw call and hands the totals to the platform, which
// can then say what a slow frame was spent drawing.
void platformDrawProfile(int kind, uint64_t nanos);
enum { SWR_PROF_SPRITE, SWR_PROF_PART, SWR_PROF_TEXT, SWR_PROF_TILED, SWR_PROF_RECT };
#define SWR_PROFILED(kind, call) do { uint64_t start_ = nowNanos(); call; platformDrawProfile(kind, nowNanos() - start_); } while (0)

static void SWRenderer_profDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha)
{
    SWR_PROFILED(SWR_PROF_SPRITE, SWRenderer_drawSprite(renderer, tpagIndex, x, y, originX, originY, xscale, yscale, angleDeg, color, alpha));
}

static void SWRenderer_profDrawSpritePart(Renderer* renderer, int32_t tpagIndex, float srcOffX, float srcOffY, float srcW, float srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha)
{
    SWR_PROFILED(SWR_PROF_PART, SWRenderer_drawSpritePart(renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX, pivotY, color, alpha));
}

static void SWRenderer_profDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, float lineSeparation)
{
    SWR_PROFILED(SWR_PROF_TEXT, SWRenderer_drawText(renderer, text, x, y, xscale, yscale, angleDeg, lineSeparation));
}

static void SWRenderer_profDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha, float lineSeparation)
{
    SWR_PROFILED(SWR_PROF_TEXT, SWRenderer_drawTextColor(renderer, text, x, y, xscale, yscale, angleDeg, c1, c2, c3, c4, alpha, lineSeparation));
}

static void SWRenderer_profDrawSpriteTiled(Renderer* renderer, int32_t tpagIndex, float originX, float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH, uint32_t color, float alpha)
{
    SWR_PROFILED(SWR_PROF_TILED, SWRenderer_drawSpriteTiled(renderer, tpagIndex, originX, originY, x, y, xscale, yscale, tileX, tileY, roomW, roomH, color, alpha));
}

static bool SWRenderer_profDrawTileRun(Renderer* renderer, RoomTile** tiles, const float* offsets, int32_t count)
{
    bool drawn;
    SWR_PROFILED(SWR_PROF_PART, drawn = SWRenderer_drawTileRun(renderer, tiles, offsets, count));
    return drawn;
}

static bool SWRenderer_profDrawTileLayer(Renderer* renderer, Background* tileset, const uint32_t* cells, uint32_t tilesX, uint32_t tilesY, float offsetX, float offsetY)
{
    bool drawn;
    SWR_PROFILED(SWR_PROF_PART, drawn = SWRenderer_drawTileLayer(renderer, tileset, cells, tilesX, tilesY, offsetX, offsetY));
    return drawn;
}

static void SWRenderer_profDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline)
{
    SWR_PROFILED(SWR_PROF_RECT, SWRenderer_drawRectangle(renderer, x1, y1, x2, y2, color, alpha, outline));
}
#endif


Renderer* SWRenderer_create(void)
{
    SWRenderer* swr = (SWRenderer*) safeCalloc(1, sizeof(SWRenderer));
    swr->base.vtable = &swrVtable;
    swrVtable.init                     = SWRenderer_init;
    swrVtable.destroy                  = SWRenderer_destroy;
    swrVtable.beginFrame               = SWRenderer_beginFrame;
    swrVtable.endFrameInit             = SWRenderer_endFrameInit;
    swrVtable.endFrameEnd              = SWRenderer_endFrameEnd;
    swrVtable.beginView                = SWRenderer_beginView;
    swrVtable.endView                  = SWRenderer_endView;
    swrVtable.beginGUI                 = SWRenderer_beginGUI;
    swrVtable.setGuiProjection         = SWRenderer_setGuiProjection;
    swrVtable.endGUI                   = SWRenderer_endGUI;
    swrVtable.drawSprite               = SWRenderer_drawSprite;
    swrVtable.drawSpritePart           = SWRenderer_drawSpritePart;
    swrVtable.drawSpritePos            = SWRenderer_drawSpritePos;
    swrVtable.drawRectangle            = SWRenderer_drawRectangle;
    swrVtable.drawRectangleColor       = SWRenderer_drawRectangleColor;
    swrVtable.drawLine                 = SWRenderer_drawLine;
    swrVtable.drawTriangle             = SWRenderer_drawTriangle;
    swrVtable.drawLineColor            = SWRenderer_drawLineColor;
    swrVtable.drawText                 = SWRenderer_drawText;
    swrVtable.drawTextColor            = SWRenderer_drawTextColor;
    swrVtable.flush                    = SWRenderer_flush;
    swrVtable.clearScreen              = SWRenderer_clearScreen;
    swrVtable.createSpriteFromSurface  = SWRenderer_createSpriteFromSurface;
    swrVtable.deleteSprite             = SWRenderer_deleteSprite;
    swrVtable.gpuSetBlendMode          = SWRenderer_gpuSetBlendMode;
    swrVtable.gpuSetBlendModeExt       = SWRenderer_gpuSetBlendModeExt;
    swrVtable.gpuSetBlendEnable        = SWRenderer_gpuSetBlendEnable;
    swrVtable.gpuSetAlphaTestEnable    = SWRenderer_gpuSetAlphaTestEnable;
    swrVtable.gpuSetAlphaTestRef       = SWRenderer_gpuSetAlphaTestRef;
    swrVtable.gpuSetColorWriteEnable   = SWRenderer_gpuSetColorWriteEnable;
    swrVtable.gpuGetColorWriteEnable   = SWRenderer_gpuGetColorWriteEnable;
    swrVtable.gpuGetBlendEnable        = SWRenderer_gpuGetBlendEnable;
    swrVtable.gpuGetBlendMode          = SWRenderer_gpuGetBlendMode;
    swrVtable.gpuSetTexFilter          = SWRenderer_gpuSetTexFilter;
    swrVtable.gpuSetFog                = SWRenderer_gpuSetFog;
    swrVtable.drawSpriteTiled          = SWRenderer_drawSpriteTiled;
    swrVtable.drawSurfaceTiled         = SWRenderer_drawSurfaceTiled;
    swrVtable.createSurface            = SWRenderer_createSurface;
    swrVtable.surfaceExists            = SWRenderer_surfaceExists;
    swrVtable.setRenderTarget          = SWRenderer_setRenderTarget;
    swrVtable.ensureApplicationSurface = SWRenderer_ensureApplicationSurface;
    swrVtable.getSurfaceWidth          = SWRenderer_getSurfaceWidth;
    swrVtable.getSurfaceHeight         = SWRenderer_getSurfaceHeight;
    swrVtable.drawSurface              = SWRenderer_drawSurface;
    swrVtable.surfaceResize            = SWRenderer_surfaceResize;
    swrVtable.surfaceFree              = SWRenderer_surfaceFree;
    swrVtable.surfaceCopy              = SWRenderer_surfaceCopy;
    swrVtable.surfaceGetPixels         = SWRenderer_surfaceGetPixels;
    swrVtable.drawTiledPart            = SWRenderer_drawTiledPart;
    swrVtable.spriteGetTexture         = SWRenderer_spriteGetTexture;
    swrVtable.surfaceGetTexture        = SWRenderer_surfaceGetTexture;
    swrVtable.textureGetTexelWidth     = SWRenderer_textureGetTexelWidth;
    swrVtable.textureGetTexelHeight    = SWRenderer_textureGetTexelHeight;
    swrVtable.textureGetUVs            = SWRenderer_textureGetUVs;
    swrVtable.textureSetStage          = SWRenderer_textureSetStage;
    swrVtable.gpuSetShader             = SWRenderer_gpuSetShader;
    swrVtable.gpuResetShader           = SWRenderer_gpuResetShader;
    swrVtable.shaderGetUniform         = SWRenderer_shaderGetUniform;
    swrVtable.shaderGetSamplerIndex    = SWRenderer_shaderGetSamplerIndex;
    swrVtable.shaderSetUniformF        = SWRenderer_shaderSetUniformF;
    swrVtable.shaderSetUniformI        = SWRenderer_shaderSetUniformI;
    swrVtable.shaderIsCompiled         = SWRenderer_shaderIsCompiled;
    swrVtable.shadersSupported         = SWRenderer_shadersSupported;
    swrVtable.applyProjection          = SWRenderer_applyProjection;
    swrVtable.primitiveBegin           = SWRenderer_primitiveBegin;
    swrVtable.primitiveBeginTexture    = SWRenderer_primitiveBeginTexture;
    swrVtable.primitiveEnd             = SWRenderer_primitiveEnd;
    swrVtable.drawVertex               = SWRenderer_drawVertex;
    swrVtable.drawVertexBuffer         = SWRenderer_drawVertexBuffer;
    swrVtable.drawTextUI               = SWRenderer_drawTextUI;
#ifdef SW_DRAW_PROFILE
    swrVtable.drawSprite               = SWRenderer_profDrawSprite;
    swrVtable.drawSpritePart           = SWRenderer_profDrawSpritePart;
    swrVtable.drawText                 = SWRenderer_profDrawText;
    swrVtable.drawTextColor            = SWRenderer_profDrawTextColor;
    swrVtable.drawSpriteTiled          = SWRenderer_profDrawSpriteTiled;
    swrVtable.drawRectangle            = SWRenderer_profDrawRectangle;
#endif
    swrVtable.drawTileRun              = SWRenderer_drawTileRun;
#ifdef SW_DRAW_PROFILE
    swrVtable.drawTileRun              = SWRenderer_profDrawTileRun;
#endif
    swrVtable.drawTileLayer            = SWRenderer_drawTileLayer;
#ifdef SW_DRAW_PROFILE
    swrVtable.drawTileLayer            = SWRenderer_profDrawTileLayer;
#endif
    
    swrVtable.drawTile                 = NULL;
    
    swr->base.drawColor = 0xFFFFFF;
    swr->base.drawAlpha = 1.0f;
    swr->base.drawFont = -1;
    swr->base.drawHalign = 0;
    swr->base.drawValign = 0;
    swr->base.circlePrecision = 24;

    return (Renderer*) swr;
}
