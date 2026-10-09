#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include "sw_renderer_private.h"

// ==== Internal functions ====

static void swrDrawTriangleTransformed(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, uintpixel_t color1, uintpixel_t color2, uintpixel_t color3, int alpha);

FORCE_INLINE void swrPlotPixel_(Renderer* renderer, int x, int y, uintpixel_t color, int blendmode, int alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (x < swr->portX || y < swr->portY) return;
    if (x >= swr->maxX || y >= swr->maxY) return;
    
    alphaBlend(&swr->fb[y * swr->fbPitch + x], color, blendmode, alpha);
}

// See the minification path in swrDrawSpriteInternal. Off by default; the
// platform turns it on when it renders a room below its native resolution.
bool swrSmoothMinify = false;

// Mirrored layers (see swrMirrorFlush). Layers fainter than swrMirrorFaintAlpha
// (of 256) are left out: at 16-bit colour they move a channel by one step at most.
bool swrMirrorMerge = true;

// See sw_drawing.h.
bool swrSkipFrame = false;
int swrMirrorFaintAlpha = 8;

// Trades accuracy for speed where the two can be told apart only by looking
// closely: a mirrored stack is worked out for every second pixel each way, and
// sprite draws at 8/256 opacity or less (the outer passes of glowing text) are
// left out. The platform sets it from the player's speed/accuracy choice.
bool swrFavorSpeed = false;

static void swrDrawHLineInt(Renderer* renderer, int dx, int dy, int dw, uintpixel_t color, UNUSED uintpixel_t color2, int alpha)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    
    if (dy < swr->portY) return;
    if (dy >= swr->maxY) return;
    if (dx < swr->portX) { dw += dx - swr->portX; dx = swr->portX; }
    if (dx + dw >= swr->maxX) dw = swr->maxX - dx;
    if (dw <= 0) return;
    
    int blendmode = swr->blendMode;
    
    if (color == color2)
    {
        uintpixel_t *line = &swr->fb[dy * swr->fbPitch + dx];
#ifdef SW_HAS_PREMUL_BLEND
        if (blendmode == bm_normal && swrIsPartialAlpha(alpha))
        {
            uint32_t srcRedBlue = swrSpreadRedBlue(color) * alpha;
            uint32_t srcGreen = swrGreen(color) * alpha;
            uint32_t dstalpha = 256 - alpha;
            for (int i = 0; i < dw; i++)
                line[i] = swrBlendPremultiplied(line[i], srcRedBlue, srcGreen, dstalpha);
            return;
        }
#endif
        for (int i = 0; i < dw; i++)
            alphaBlend(&line[i], color, blendmode, alpha);
    }
    else
    {
        uintpixel_t *line = &swr->fb[dy * swr->fbPitch + dx];
        uint32_t inc = (65536U << 14) / dw;
        uint32_t weightfp = 0;
        for (int i = 0; i < dw; i++, weightfp += inc)
        {
            uint32_t weight2 = (weightfp >> 14);
            if (weight2 > 65535) weight2 = 65535;
            uint32_t weight1 = 65535 - weight2;
            uintpixel_t result = swrTwoWayBlend(color, color2, (uint16_t) weight1, (uint16_t) weight2);
            alphaBlend(&line[i], result, blendmode, alpha);
        }
    }
}

static void swrDrawVLineInt(Renderer* renderer, int dx, int dy, int dh, uintpixel_t color, UNUSED uintpixel_t color2, int alpha)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    
    if (dx < swr->portX) return;
    if (dx >= swr->maxX) return;
    if (dy < swr->portY) { dh += dy - swr->portY; dy = swr->portY; }
    if (dy + dh >= swr->maxY) dh = swr->maxY - dy;
    if (dh <= 0) return;
    
    int blendmode = swr->blendMode;
    
    if (color == color2)
    {
        for (int i = 0; i < dh; i++)
        {
            uintpixel_t *line = &swr->fb[(dy + i) * swr->fbPitch + dx];
            alphaBlend(&line[0], color, blendmode, alpha);
        }
    }
    else
    {
        uint32_t inc = (65536U << 14) / dh;
        uint32_t weightfp = 0;
        for (int i = 0; i < dh; i++, weightfp += inc)
        {
            uint32_t weight2 = (weightfp >> 14);
            if (weight2 > 65535) weight2 = 65535;
            uint32_t weight1 = 65535 - weight2;
            uintpixel_t result = swrTwoWayBlend(color, color2, (uint16_t) weight1, (uint16_t) weight2);
            uintpixel_t *line = &swr->fb[(dy + i) * swr->fbPitch + dx];
            alphaBlend(&line[0], result, blendmode, alpha);
        }
    }
}

static void swrDrawLineInt(Renderer* renderer, int x1, int y1, int x2, int y2, int width, uintpixel_t color1, uintpixel_t color2, int alpha, int alignment)
{
    if (x1 == x2)
    {
        if (alignment == SWR_LINE_ALIGN_CENTER)
            x1 -= width / 2;
        else if (alignment == SWR_LINE_ALIGN_M1)
            x1 -= width;
        
        for (int i = 0; i < width; i++) {
            swrDrawVLineInt(renderer, x1 + i, swrMin(y1, y2), swrAbs(y1 - y2), color1, color2, alpha);
        }
        return;
    }
    if (y1 == y2)
    {
        if (alignment == SWR_LINE_ALIGN_CENTER)
            y1 -= width / 2;
        else if (alignment == SWR_LINE_ALIGN_M1)
            y1 -= width;
        
        for (int i = 0; i < width; i++) {
            swrDrawHLineInt(renderer, swrMin(x1, x2), y1 + i, swrAbs(x1 - x2), color1, color2, alpha);
        }
        return;
    }
    
    if (UNLIKELY(width > 1))
    {
        // HACK: Just draw two triangles instead.
        int xd = x2 - x1, yd = y2 - y1;
        int line_length_sqr = xd * xd + yd * yd;
        if (line_length_sqr <= 0)
            return;
        
        float line_length = sqrtf(line_length_sqr);
        float xds = (float)-yd * (width * 0.5f) / line_length;
        float yds = (float)+xd * (width * 0.5f) / line_length;
        
        float x1l = x1 - xds, y1l = y1 - yds;
        float x1r = x1 + xds, y1r = y1 + yds;
        float x2l = x2 - xds, y2l = y2 - yds;
        float x2r = x2 + xds, y2r = y2 + yds;
        
        swrDrawTriangleTransformed(renderer, x1l, y1l, x1r, y1r, x2l, y2l, color1, color1, color2, alpha);
        swrDrawTriangleTransformed(renderer, x2r, y2r, x1r, y1r, x2l, y2l, color2, color1, color2, alpha);
        return;
    }
    
    SWRenderer* swr = (SWRenderer*) renderer;
    int dx = x2 - x1, dy = y2 - y1;
    int dx1 = swrAbs(dx), dy1 = swrAbs(dy), xe, ye, x, y;
    int px = 2 * dy1 - dx1, py = 2 * dx1 - dy1;
    int blendmode = swr->blendMode;
    
    uintpixel_t color = color1;
    uint32_t inc = (65536U << 14);
    uint32_t weightfp = 0;
    
    if (dy1 <= dx1)
    {
        if (dx >= 0)
        {
            x = x1, y = y1, xe = x2;
        }
        else
        {
            x = x2, y = y2, xe = x1;
        }
        
        if (dx1 > 0) {
            inc /= dx1;
        } else {
            inc = 0;
        }
        
        swrPlotPixel_(renderer, x, y, color, blendmode, alpha);
        
        while (x < xe)
        {
            x++;
            if (px < 0)
            {
                px += 2 * dy1;
            }
            else
            {
                if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) y++; else y--;
                px += 2 * (dy1 - dx1);
            }

            uint32_t weight2 = (weightfp >> 14);
            if (weight2 > 65535) weight2 = 65535;
            uint32_t weight1 = 65535 - weight2;
            weightfp += inc;
            color = swrTwoWayBlend(color1, color2, (uint16_t) weight1, (uint16_t) weight2);

            swrPlotPixel_(renderer, x, y, color, blendmode, alpha);
        }
    }
    else
    {
        if (dy >= 0)
        {
            x = x1, y = y1, ye = y2;
        }
        else
        {
            x = x2, y = y2, ye = y1;
        }
        
        if (dy1 > 0) {
            inc /= dy1;
        } else {
            inc = 0;
        }
        
        swrPlotPixel_(renderer, x, y, color, blendmode, alpha);
        
        while (y < ye)
        {
            y++;
            if (py <= 0)
            {
                py += 2 * dx1;
            }
            else
            {
                if ((dx < 0 && dy < 0) || (dx > 0 && dy > 0)) x++; else x--;
                py += 2 * (dx1 - dy1);
            }

            uint32_t weight2 = (weightfp >> 14);
            if (weight2 > 65535) weight2 = 65535;
            uint32_t weight1 = 65535 - weight2;
            weightfp += inc;
            color = swrTwoWayBlend(color1, color2, (uint16_t) weight1, (uint16_t) weight2);

            swrPlotPixel_(renderer, x, y, color, blendmode, alpha);
        }
    }
}

#ifndef SW_NO_SEMI_TRANSPARENT_TEXTURE_SUPPORT

FORCE_INLINE void swrCalculateAlphaBlending(
    uintpixel_t* px,
    uintpixel_t src,
    uintpixel_t tintColor,
    int alpha,
    int blendmode
)
{
    // perform alpha blending for real now.  a good bit slower
    uint8_t alphau;
    uintpixel_t color;
    int aalpha;
    
    alphau = swrGetAlphaU8(src);
    
    // first pass tint because GM does not do premultiplied alpha
    color = tint(swrAlphaToColor(alphau), src);
    
    // second pass tint for coloring
    color = tint(tintColor, color);
    
    // recalculate alpha
    aalpha = (alpha * alphau) >> 8;
    alphaBlend(px, color, blendmode, aalpha);
}

#endif

// Stacked overlays. A game can draw the same solid sprite over the same area
// many times in a row: Undertale adds one full-screen fade object per frame
// for as long as the player stands in a doorway, a dozen or more deep. Each
// is a read-blend-write of every pixel, and N blends with a constant colour
// equal one blend with the combined colour and coverage. So a solid sprite
// draw is held back, and draws of the same area that follow it directly are
// folded into it. A draw nothing follows comes out exactly as it did before;
// a folded stack is rounded once instead of once per layer.
static void swrOverlayFlushHeld(SWRenderer* swr)
{
#ifdef SW_HAS_PREMUL_BLEND
    int count = swr->overlayCount;
    if (count == 0) return;
    swr->overlayCount = 0;
    
    uint32_t dstalpha, srcRedBlue, srcGreen;
    uintpixel_t fill = swr->overlayFirstColor;
    if (count == 1) {
        int alpha = swr->overlayFirstAlpha;
        dstalpha = swrIsPartialAlpha(alpha) ? (uint32_t) (256 - alpha) : 0;
        srcRedBlue = swrSpreadRedBlue(fill) * (uint32_t) alpha;
        srcGreen = swrGreen(fill) * (uint32_t) alpha;
    } else {
        uint32_t red = (uint32_t) (swr->overlayRed + 0.5f);
        uint32_t green = (uint32_t) (swr->overlayGreen + 0.5f);
        uint32_t blue = (uint32_t) (swr->overlayBlue + 0.5f);
        dstalpha = (uint32_t) (swr->overlayKeep * 256.0f + 0.5f);
        srcRedBlue = (red << 16) | blue;
        srcGreen = green;
        fill = (uintpixel_t) (0x8000 | ((red >> 8) << 10) | ((green >> 8) << 5) | (blue >> 8));
    }
    
    for (int y = 0; y < swr->overlayH; y++)
    {
        uintpixel_t* dstline = &swr->overlayFb[(swr->overlayY + y) * swr->overlayPitch + swr->overlayX];
        if (dstalpha == 0) {
            swrFillPixels(dstline, (size_t) swr->overlayW, fill);
            continue;
        }
        // Flat-coloured art has long runs of one colour: blend each run once.
        uintpixel_t lastDst = dstline[0];
        uintpixel_t lastOut = swrBlendPremultiplied(lastDst, srcRedBlue, srcGreen, dstalpha);
        for (int x = 0; x < swr->overlayW; x++) {
            uintpixel_t dst = dstline[x];
            if (dst != lastDst) {
                lastDst = dst;
                lastOut = swrBlendPremultiplied(dst, srcRedBlue, srcGreen, dstalpha);
            }
            dstline[x] = lastOut;
        }
    }
#else
    (void) swr;
#endif
}

#ifdef SW_HAS_PREMUL_BLEND
#define SW_OVERLAY_MAX_SOURCE_TEXELS 4096

// Holds the draw back if it is a solid overlay, folding it into the one
// already held where it covers the same pixels. Returns false if the caller
// has to draw it. Takes the clipped rectangles of an unflipped draw; lastCol
// and lastRow are the furthest texels the draw would sample.
static bool swrOverlayHold(SWRenderer* swr, int dx, int dy, int dw, int dh, SWTexture* texture,
                           int sx, int sy, int lastCol, int lastRow, uintpixel_t tintColor, int alpha)
{
    if (lastCol >= texture->width || lastRow >= texture->height) return false;
    if ((lastCol - sx + 1) * (lastRow - sy + 1) > SW_OVERLAY_MAX_SOURCE_TEXELS) return false;
    
    uintpixel_t texel = texture->buffer[sy * texture->width + sx];
    if (!swrIsOpaque(texel)) return false;
    for (int row = sy; row <= lastRow; row++) {
        const uintpixel_t* srcline = &texture->buffer[row * texture->width];
        for (int col = sx; col <= lastCol; col++)
            if (srcline[col] != texel) return false;
    }
    
    uintpixel_t color = tint(tintColor, texel);
    float cover = alpha > 253 ? 1.0f : (float) alpha / 256.0f;
    float red = (float) ((color >> 10) & 0x1F) * 256.0f;
    float green = (float) swrGreen(color) * 256.0f;
    float blue = (float) (color & 0x1F) * 256.0f;
    
    bool sameArea = swr->overlayCount > 0 && swr->overlayFb == swr->fb && swr->overlayPitch == swr->fbPitch &&
                    swr->overlayX == dx && swr->overlayY == dy && swr->overlayW == dw && swr->overlayH == dh;
    if (sameArea) {
        swr->overlayCount++;
        swr->overlayKeep *= 1.0f - cover;
        swr->overlayRed = swr->overlayRed * (1.0f - cover) + red * cover;
        swr->overlayGreen = swr->overlayGreen * (1.0f - cover) + green * cover;
        swr->overlayBlue = swr->overlayBlue * (1.0f - cover) + blue * cover;
        return true;
    }
    
    swrOverlayFlush(swr);
    swr->overlayCount = 1;
    swr->overlayFb = swr->fb;
    swr->overlayPitch = swr->fbPitch;
    swr->overlayX = dx; swr->overlayY = dy; swr->overlayW = dw; swr->overlayH = dh;
    swr->overlayFirstColor = color;
    swr->overlayFirstAlpha = alpha;
    swr->overlayKeep = 1.0f - cover;
    swr->overlayRed = red * cover;
    swr->overlayGreen = green * cover;
    swr->overlayBlue = blue * cover;
    return true;
}
#endif

static void swrDrawSpriteInternal(
    Renderer* renderer, int dx, int dy, int dw, int dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uintpixel_t tintColor, int alpha
);

#ifdef SW_HAS_PREMUL_BLEND
#define SW_MIRROR_MIN_PIXELS 4096

static void swrMirrorReplay(SWRenderer* swr, const SWSpriteCall* call)
{
    swrDrawSpriteInternal((Renderer*) swr, call->dx, call->dy, call->dw, call->dh, call->texture,
                          call->sx, call->sy, call->sw, call->sh, call->tintColor, call->alpha);
}

static void swrMirrorFillRows(SWRenderer* swr, int x, int y, int w, int h, uintpixel_t color, int alpha)
{
    for (int row = 0; row < h; row++)
        swrDrawHLineInt((Renderer*) swr, x, y + row, w, color, color, alpha);
}

// Works a whole stack out for the unflipped quarter in one pass and writes
// each pixel to all four quarters. Layer by layer it does the arithmetic the
// ordinary draws would have done. What lies under the stack is either one
// known colour, so the buffer is not read at all, or (readUnder) the same in
// all four quarters and read from the unflipped one. `step` is 1, or 2 to work
// out every second pixel each way and repeat it (swrFavorSpeed).
static void swrMirrorCompose(SWRenderer* swr, int layers, int step, bool readUnder)
{
    typedef struct {
        const uintpixel_t* buffer;
        const uintpixel_t* srcline;
        int texWidth, sx, sy, xstep, ystep;
        uintpixel_t tintColor;
        uint32_t alpha, dstalpha, srcRedBlue, srcGreen, lastPixel;
        bool solid;
    } Active;
    Active active[SW_MIRROR_MAX_LAYERS];
    int count = 0;
    
    for (int layer = 0; layer < layers; layer++)
    {
        const SWMirrorLayer* held = &swr->mirrorStack[layer];
        const SWSpriteCall* call = &held->calls[0];
        Active* a = &active[count];
        a->solid = held->solid;
        a->alpha = (uint32_t) call->alpha;
        a->dstalpha = 256 - a->alpha;
        a->tintColor = call->tintColor;
        if (held->solid) {
            a->srcRedBlue = swrSpreadRedBlue(call->tintColor) * a->alpha;
            a->srcGreen = swrGreen(call->tintColor) * a->alpha;
        } else {
            if (call->alpha < swrMirrorFaintAlpha) continue;
            a->buffer = call->texture->buffer;
            a->texWidth = call->texture->width;
            a->sx = held->sx; a->sy = held->sy;
            a->xstep = held->xstep; a->ystep = held->ystep;
            a->lastPixel = 0xFFFFFFFF;
            a->srcRedBlue = a->srcGreen = 0;
        }
        count++;
    }
    
    const int fp_prec = 14;
    int cx = swr->mirrorCx, cy = swr->mirrorCy, w = swr->mirrorW, h = swr->mirrorH;
    uintpixel_t under = swr->mirrorUnder;
    
    for (int y = 0; y < h; y += step)
    {
        for (int i = 0; i < count; i++) {
            Active* a = &active[i];
            if (!a->solid)
                a->srcline = &a->buffer[(a->sy + (int) (((int32_t) y * a->ystep) >> fp_prec)) * a->texWidth + a->sx];
        }
        uintpixel_t* below = &swr->fb[(cy + y) * swr->fbPitch + cx];
        uintpixel_t* above = &swr->fb[(cy - 1 - y) * swr->fbPitch + cx];
        bool second = step == 2 && y + 1 < h;
        uintpixel_t* below2 = below + swr->fbPitch;
        uintpixel_t* above2 = above - swr->fbPitch;
        
        for (int x = 0; x < w; x += step)
        {
            uintpixel_t color = readUnder ? below[x] : under;
            for (int i = 0; i < count; i++)
            {
                Active* a = &active[i];
                if (!a->solid) {
                    uintpixel_t pixel = a->srcline[(int) (((int32_t) x * a->xstep) >> fp_prec)];
                    if (!swrIsOpaque(pixel)) continue;
                    if (pixel != a->lastPixel) {
                        uintpixel_t tinted = tint(a->tintColor, pixel);
                        a->srcRedBlue = swrSpreadRedBlue(tinted) * a->alpha;
                        a->srcGreen = swrGreen(tinted) * a->alpha;
                        a->lastPixel = pixel;
                    }
                }
                color = swrBlendPremultiplied(color, a->srcRedBlue, a->srcGreen, a->dstalpha);
            }
            
            below[x] = color; below[-1 - x] = color;
            above[x] = color; above[-1 - x] = color;
            if (step == 2) {
                bool wide = x + 1 < w;
                if (wide) {
                    below[x + 1] = color; below[-2 - x] = color;
                    above[x + 1] = color; above[-2 - x] = color;
                }
                if (second) {
                    below2[x] = color; below2[-1 - x] = color;
                    above2[x] = color; above2[-1 - x] = color;
                    if (wide) {
                        below2[x + 1] = color; below2[-2 - x] = color;
                        above2[x + 1] = color; above2[-2 - x] = color;
                    }
                }
            }
        }
    }
}

// Mirrored layers. Deltarune's character creation screens draw a small image
// stretched over each quarter of the screen, flipped so that the four meet in
// the middle, at low opacity, six layers deep, and then darken the lot with a
// translucent fill: seven screens of blending per frame. Where the picture
// underneath is itself the same in all four quarters (there, a cleared screen),
// each layer's quarters come out as mirror images of one another, so one
// quarter is worked out and the other three are copies of it. The layers are
// held back until something else is drawn; if the picture underneath turns out
// not to be symmetric they are drawn the ordinary way.
static void swrMirrorFlush(SWRenderer* swr)
{
    int layers = swr->mirrorLayers, stage = swr->mirrorStage;
    if (layers == 0 && stage == 0) return;
    swr->mirrorLayers = 0;
    swr->mirrorStage = 0;
    swr->mirrorReplaying = true;
    
    // The held draws belong to the target and port they were made for.
    uintpixel_t* fb = swr->fb;
    uint16_t pitch = swr->fbPitch;
    int port[6] = { swr->portX, swr->portY, swr->portW, swr->portH, swr->maxX, swr->maxY };
    int blendMode = swr->blendMode;
    swr->fb = swr->mirrorFb;
    swr->fbPitch = (uint16_t) swr->mirrorPitch;
    swr->portX = swr->mirrorPort[0]; swr->portY = swr->mirrorPort[1];
    swr->portW = swr->mirrorPort[2]; swr->portH = swr->mirrorPort[3];
    swr->maxX = swr->mirrorPort[4]; swr->maxY = swr->mirrorPort[5];
    swr->blendMode = bm_normal;
    
    int cx = swr->mirrorCx, cy = swr->mirrorCy, w = swr->mirrorW, h = swr->mirrorH;
    if (layers > 0 && swr->mirrorUnderKnown)
    {
        swrMirrorCompose(swr, layers, swrFavorSpeed ? 2 : 1, false);
    }
    else if (layers > 0)
    {
        swrOverlayFlushHeld(swr); // anything held before the layers lies under them
        
        bool symmetric = true;
        for (int y = 0; y < h && symmetric; y++)
        {
            const uintpixel_t* below = &swr->fb[(cy + y) * swr->fbPitch + cx];
            const uintpixel_t* above = &swr->fb[(cy - 1 - y) * swr->fbPitch + cx];
            for (int x = 0; x < w; x++)
            {
                uintpixel_t pixel = below[x];
                if (below[-1 - x] != pixel || above[x] != pixel || above[-1 - x] != pixel) { symmetric = false; break; }
            }
        }
        
        if (symmetric)
            swrMirrorCompose(swr, layers, swrFavorSpeed ? 2 : 1, true);
        else for (int layer = 0; layer < layers; layer++)
        {
            const SWMirrorLayer* held = &swr->mirrorStack[layer];
            if (held->solid) {
                swrMirrorFillRows(swr, cx - w, cy - h, 2 * w, 2 * h, held->calls[0].tintColor, held->calls[0].alpha);
                continue;
            }
            if (held->calls[0].alpha < swrMirrorFaintAlpha) continue;
            for (int quarter = 0; quarter < 4; quarter++)
                swrMirrorReplay(swr, &held->calls[quarter]);
            swrOverlayFlushHeld(swr);
        }
    }
    
    // Quarters of a layer that was never completed are ordinary draws.
    for (int quarter = 0; quarter < stage; quarter++)
        swrMirrorReplay(swr, &swr->mirrorStack[layers].calls[quarter]);
    swrOverlayFlushHeld(swr);
    
    swr->fb = fb;
    swr->fbPitch = pitch;
    swr->portX = port[0]; swr->portY = port[1]; swr->portW = port[2]; swr->portH = port[3];
    swr->maxX = port[4]; swr->maxY = port[5];
    swr->blendMode = blendMode;
    swr->mirrorReplaying = false;
    swr->uniformValid = false;
}

// Holds the draw back if it can be a quarter of a mirrored layer. Takes the
// draw as it was asked for (call) and as it came out of clipping. Returns
// false if the caller has to draw it; nothing is held then.
static bool swrMirrorHold(SWRenderer* swr, const SWSpriteCall* call, int dx, int dy, int dw, int dh,
                          bool flipX, bool flipY, int sx, int sy, int xstep, int ystep, int unit)
{
    bool eligible = swrMirrorMerge && swr->blendMode == bm_normal && swrIsPartialAlpha(call->alpha) &&
                    dw * dh >= SW_MIRROR_MIN_PIXELS && xstep <= unit && ystep <= unit;
    if (!eligible) {
        // A draw too faint to change anything (alphaBlend skips it) is no reason to let a stack out.
        bool invisible = swr->blendMode == bm_normal && call->alpha < 4;
        if (invisible && (swr->mirrorLayers > 0 || swr->mirrorStage > 0)) return true;
        swrMirrorFlush(swr);
        return false;
    }
    
    int stage = swr->mirrorStage;
    if (stage > 0)
    {
        // The quarters follow the unflipped one anticlockwise: left, above left, above.
        SWMirrorLayer* held = &swr->mirrorStack[swr->mirrorLayers];
        const SWSpriteCall* first = &held->calls[0];
        int wantX = (stage == 3) ? swr->mirrorCx : swr->mirrorCx - swr->mirrorW;
        int wantY = (stage == 1) ? swr->mirrorCy : swr->mirrorCy - swr->mirrorH;
        bool match = swr->fb == swr->mirrorFb && swr->fbPitch == swr->mirrorPitch &&
                     flipX == (stage != 3) && flipY == (stage != 1) &&
                     dx == wantX && dy == wantY && dw == swr->mirrorW && dh == swr->mirrorH &&
                     call->texture == first->texture && call->alpha == first->alpha &&
                     call->tintColor == first->tintColor &&
                     sx == held->sx && sy == held->sy && xstep == held->xstep && ystep == held->ystep;
        if (match) {
            held->calls[stage] = *call;
            if (stage == 3) {
                swr->mirrorLayers++;
                swr->mirrorStage = 0;
            } else
                swr->mirrorStage = stage + 1;
            return true;
        }
        swrMirrorFlush(swr); // not a mirrored layer after all; this draw may start one
    }
    
    if (flipX || flipY) {
        swrMirrorFlush(swr);
        return false;
    }
    
    bool sameQuad = swr->mirrorLayers > 0 && swr->fb == swr->mirrorFb && swr->fbPitch == swr->mirrorPitch &&
                    dx == swr->mirrorCx && dy == swr->mirrorCy && dw == swr->mirrorW && dh == swr->mirrorH;
    if (!sameQuad || swr->mirrorLayers >= SW_MIRROR_MAX_LAYERS)
        swrMirrorFlush(swr);
    if (swr->mirrorLayers == 0) {
        swr->mirrorFb = swr->fb;
        swr->mirrorPitch = swr->fbPitch;
        swr->mirrorPort[0] = swr->portX; swr->mirrorPort[1] = swr->portY;
        swr->mirrorPort[2] = swr->portW; swr->mirrorPort[3] = swr->portH;
        swr->mirrorPort[4] = swr->maxX; swr->mirrorPort[5] = swr->maxY;
        swr->mirrorCx = dx; swr->mirrorCy = dy; swr->mirrorW = dw; swr->mirrorH = dh;
        // Known only if nothing is waiting to be drawn underneath.
        swr->mirrorUnderKnown = swr->uniformValid && swr->fb == swr->mainFb && swr->overlayCount == 0;
        swr->mirrorUnder = swr->uniformColor;
    }
    SWMirrorLayer* held = &swr->mirrorStack[swr->mirrorLayers];
    held->solid = false;
    held->sx = sx; held->sy = sy;
    held->xstep = xstep; held->ystep = ystep;
    held->calls[0] = *call;
    swr->mirrorStage = 1;
    return true;
}

// A translucent fill of exactly the area a held stack covers joins the stack
// as one more layer. Takes swrFillRectangle's arguments; returns false if the
// fill has to be drawn.
bool swrMirrorHoldFill(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t pxcolor, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    if (swr->mirrorLayers == 0 || swr->mirrorStage != 0 || swr->mirrorLayers >= SW_MIRROR_MAX_LAYERS) return false;
    if (swr->fb != swr->mirrorFb || swr->fbPitch != swr->mirrorPitch || swr->blendMode != bm_normal) return false;
    int alphaInt = swrIntAlpha(alpha);
    if (!swrIsPartialAlpha(alphaInt)) return false;
    
    // The area swrFillRectangle would cover, clipped as swrDrawHLineInt clips it.
    swrTransformPosIfNeeded(swr, &x1, &y1);
    swrTransformPosIfNeeded(swr, &x2, &y2);
    int x1i = swrFloor(x1), x2i = swrCeiling(x2), y1i = swrFloor(y1), y2i = swrCeiling(y2);
    int xd = x2i - x1i, yd = y2i - y1i;
    if (xd < 0) { x1i = x2i; xd = -xd; }
    if (yd < 0) { y1i = y2i; yd = -yd; }
    if (xd <= 0 || yd <= 0) return false;
    int left = x1i < swr->portX ? swr->portX : x1i;
    int right = x1i + xd >= swr->maxX ? swr->maxX : x1i + xd;
    int top = y1i < swr->portY ? swr->portY : y1i;
    int bottom = y1i + yd + 1 > swr->maxY ? swr->maxY : y1i + yd + 1;
    
    if (left != swr->mirrorCx - swr->mirrorW || right != swr->mirrorCx + swr->mirrorW ||
        top != swr->mirrorCy - swr->mirrorH || bottom != swr->mirrorCy + swr->mirrorH)
        return false;
    
    SWMirrorLayer* held = &swr->mirrorStack[swr->mirrorLayers++];
    held->solid = true;
    held->calls[0].tintColor = pxcolor;
    held->calls[0].alpha = alphaInt;
    return true;
}
#endif

void swrOverlayFlush(SWRenderer* swr)
{
#ifdef SW_HAS_PREMUL_BLEND
    if (!swr->mirrorReplaying) swrMirrorFlush(swr);
#endif
    swrOverlayFlushHeld(swr);
    swr->uniformValid = false; // every caller is about to draw
}

static void swrDrawSpriteInternal(
    Renderer* renderer, int dx, int dy, int dw, int dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uintpixel_t tintColor, int alpha
)
{
    SWRenderer *swr = (SWRenderer*) renderer;
#ifdef SW_HAS_PREMUL_BLEND
    const SWSpriteCall asked = { dx, dy, dw, dh, texture, sx, sy, sw, sh, tintColor, alpha };
#endif
    
    bool flipX = false, flipY = false;
    if (dw < 0) { dx += dw; dw = -dw; flipX = true; }
    if (dh < 0) { dy += dh; dh = -dh; flipY = true; }
    
    //basic out of bound checks
    if (dw == 0 || dh == 0) return;
    if (sw == 0) sw = 1;
    if (sh == 0) sh = 1;
    if (dx + dw <= swr->portX) return;
    if (dy + dh <= swr->portY) return;
    if (dx >= swr->maxX) return;
    if (dy >= swr->maxY) return;
    
    int odw = dw, odh = dh;
    int osw = sw, osh = sh;
    
    int minx = swr->portX, miny = swr->portY, maxx = swr->portX + swr->portW, maxy = swr->portY + swr->portH;
    
    //out of bounds adjustment checks
    int diffxl = 0, diffyl = 0, diffxu = 0, diffyu = 0;
    if (dx < minx) { diffxl = minx - dx; dx = minx; dw -= diffxl; }
    if (dy < miny) { diffyl = miny - dy; dy = miny; dh -= diffyl; }
    if (dx + dw > maxx) { diffxu = dx + dw - maxx; dw -= diffxu; }
    if (dy + dh > maxy) { diffyu = dy + dh - maxy; dh -= diffyu; }
    
    if (diffxl != 0 || diffyl != 0 || diffxu != 0 || diffyu != 0)
    {
        //adjust source coordinates too
        diffxl = (int)((long)diffxl * osw / odw);
        diffyl = (int)((long)diffyl * osh / odh);
        diffxu = (int)((long)(diffxu + 1) * osw / odw);
        diffyu = (int)((long)(diffyu + 1) * osh / odh);
        sx += flipX ? diffxu : diffxl;
        sy += flipY ? diffyu : diffyl;
        sw -= diffxl + diffxu;
        sh -= diffyl + diffyu;
        if (sw <= 0 || sh <= 0) return;
    }
    
    //clip the source coords into bounds too
    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw >= texture->width)  { sw = texture->width  - sx; }
    if (sy + sh >= texture->height) { sh = texture->height - sy; }
    if (sw <= 0 || sh <= 0) return;
    
    //okay, now we can finally get on with rendering
    
    int ixs = 0, oxs = 1, iys = 0, oys = 1;
    if (flipX) ixs = dw - 1, oxs = -1;
    if (flipY) iys = dh - 1, oys = -1;
    
    // tweak these if stuff doesn't look right
    typedef int32_t fixedp_t;
    const int fp_prec = 14;

    fixedp_t ystep = (sh == dh) ? (1 << fp_prec) : ((fixedp_t) osh << fp_prec) / odh;
    fixedp_t xstep = (sw == dw) ? (1 << fp_prec) : ((fixedp_t) osw << fp_prec) / odw;
    fixedp_t oxs2 = oxs * xstep;
    fixedp_t oys2 = oys * ystep;
    fixedp_t ixs2 = ixs * xstep;
    fixedp_t iys2 = iys * ystep;
    
    int blendmode = swr->blendMode;
    
#ifdef SW_HAS_PREMUL_BLEND
    // Nothing comes of a draw this faint: alphaBlend skips it, and a 16-bit texel is opaque or absent.
    // Leaving here keeps what is held, and what is known about the buffer, as it was.
    if (blendmode == bm_normal && alpha < 4) return;
    
    // A quarter of a mirrored layer is held back; anything else lets held layers out first.
    if (!swr->mirrorReplaying &&
        swrMirrorHold(swr, &asked, dx, dy, dw, dh, flipX, flipY, sx, sy, (int) xstep, (int) ystep, 1 << fp_prec))
        return;
    swr->uniformValid = false;
    if (swrFavorSpeed && alpha <= 8 && blendmode == bm_normal) return; // too faint to miss; see swrFavorSpeed
#endif
    
#ifdef SW_HAS_PREMUL_BLEND
    // Solid overlays drawn at full size or enlarged may be held back and merged.
    if (swr->overlayMergeAllowed && blendmode == bm_normal && alpha >= 4 && !flipX && !flipY &&
        xstep <= (1 << fp_prec) && ystep <= (1 << fp_prec))
    {
        int lastCol = sx + (int) (((fixedp_t) (dw - 1) * xstep) >> fp_prec);
        int lastRow = sy + ((dh == sh) ? dh - 1 : (int) (((fixedp_t) (dh - 1) * ystep) >> fp_prec));
        if (swrOverlayHold(swr, dx, dy, dw, dh, texture, sx, sy, lastCol, lastRow, tintColor, alpha))
            return;
    }
    swrOverlayFlush(swr);
#endif
    
#ifdef SW_HAS_PREMUL_BLEND
    // Shrinking by about half with nearest-neighbour sampling drops every
    // other texel, which makes small text unreadable (a 640x480 screen drawn
    // into 320x240). When enabled, average each 2x2 block of texels instead
    // and use the opaque share of the block as coverage.
    if (swrSmoothMinify && blendmode == bm_normal && !flipX && !flipY &&
        xstep > (3 << (fp_prec - 1)) && ystep > (3 << (fp_prec - 1)) && alpha >= 4)
    {
        int srcRight = sx + sw - 1, srcBottom = sy + sh - 1;
        uint32_t lastColor = 0xFFFFFFFF;
        uintpixel_t lastTinted = 0;
        
        fixedp_t ys2 = iys2;
        for (int y = 0; y < dh; y++, ys2 += oys2)
        {
            uintpixel_t* dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
            int row0 = sy + (int)(ys2 >> fp_prec);
            if (row0 > srcBottom) break;
            int row1 = row0 < srcBottom ? row0 + 1 : -1;
            const uintpixel_t* src0 = &texture->buffer[row0 * texture->width];
            const uintpixel_t* src1 = row1 >= 0 ? &texture->buffer[row1 * texture->width] : NULL;
            
            fixedp_t xs2 = ixs2;
            for (int x = 0; x < dw; x++, xs2 += oxs2)
            {
                int col0 = sx + (int)(xs2 >> fp_prec);
                if (col0 > srcRight) break;
                int col1 = col0 < srcRight ? col0 + 1 : -1;
                
                // Texels outside the sprite's source rectangle count as transparent.
                uintpixel_t t0 = src0[col0];
                uintpixel_t t1 = col1 >= 0 ? src0[col1] : 0;
                uintpixel_t t2 = src1 ? src1[col0] : 0;
                uintpixel_t t3 = (src1 && col1 >= 0) ? src1[col1] : 0;
                
                uintpixel_t color;
                int coverageAlpha = alpha;
                if (t0 == t1 && t0 == t2 && t0 == t3) {
                    // Flat area (most of any sprite): nothing to average.
                    if (!swrIsOpaque(t0)) continue;
                    color = t0;
                } else {
                    uint32_t red = 0, green = 0, blue = 0, covered = 0;
                    #define SW_ACCUMULATE(texel) \
                        if (swrIsOpaque(texel)) { \
                            red += ((texel) >> 10) & 0x1F; green += ((texel) >> 5) & 0x1F; blue += (texel) & 0x1F; covered++; \
                        }
                    SW_ACCUMULATE(t0) SW_ACCUMULATE(t1) SW_ACCUMULATE(t2) SW_ACCUMULATE(t3)
                    #undef SW_ACCUMULATE
                    if (covered == 0) continue;
                    
                    // Divide by the 1..4 covered texels without a hardware divide: x * (65536 / n + 1) >> 16.
                    static const uint32_t reciprocal[5] = { 0, 65536, 32768, 21846, 16384 };
                    uint32_t scale = reciprocal[covered];
                    color = (uintpixel_t)(0x8000 | (((red * scale) >> 16) << 10) | (((green * scale) >> 16) << 5) | ((blue * scale) >> 16));
                    coverageAlpha = (alpha * (int) covered) >> 2;
                }
                
                if (color != lastColor) {
                    lastTinted = tint(tintColor, color);
                    lastColor = color;
                }
                
                if (coverageAlpha > 253)
                    dstline[x] = lastTinted;
                else if (coverageAlpha >= 4)
                    dstline[x] = swrBlendPremultiplied(dstline[x], swrSpreadRedBlue(lastTinted) * coverageAlpha,
                                                       swrGreen(lastTinted) * coverageAlpha, 256 - coverageAlpha);
            }
        }
        return;
    }
    
    // Translucent sprites (fades, overlays): premultiply the tinted source
    // colour and reuse it while consecutive source pixels are the same.
    if (blendmode == bm_normal && swrIsPartialAlpha(alpha))
    {
        uint32_t dstalpha = 256 - alpha;
        uint32_t lastPixel = 0xFFFFFFFF;
        uint32_t srcRedBlue = 0, srcGreen = 0;
        
        fixedp_t ys2 = iys2;
        for (int y = 0, ys = iys; y < dh; y++, ys += oys, ys2 += oys2)
        {
            uintpixel_t* dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
            const uintpixel_t* srcline;
            if (dh == sh)
                srcline = &texture->buffer[(sy + ys) * texture->width + sx];
            else
                srcline = &texture->buffer[(sy + (int)(ys2 >> fp_prec)) * texture->width + sx];
            
            fixedp_t xs2 = ixs2;
            for (int x = 0; x < dw; x++, xs2 += oxs2)
            {
                uintpixel_t pixel = srcline[(int)(xs2 >> fp_prec)];
                if (!swrIsOpaque(pixel))
                    continue;
                
                if (pixel != lastPixel) {
                    uintpixel_t tinted = tint(tintColor, pixel);
                    srcRedBlue = swrSpreadRedBlue(tinted) * alpha;
                    srcGreen = swrGreen(tinted) * alpha;
                    lastPixel = pixel;
                }
                dstline[x] = swrBlendPremultiplied(dstline[x], srcRedBlue, srcGreen, dstalpha);
            }
        }
        return;
    }
#endif
    
#if PIXEL_SIZE == 16 && !defined SW_DITHERED_BLENDING
    // Fully opaque draws, the common case: copy opaque texels straight
    // through, tinting once per run of identical source pixels. alphaBlend()
    // reduces to a plain store for bm_normal at this alpha.
    if (blendmode == bm_normal && alpha > 253)
    {
        bool untinted = (tintColor & 0x7FFF) == 0x7FFF;
        uint32_t lastPixel = 0xFFFFFFFF;
        uintpixel_t lastTinted = 0;
        
        fixedp_t ys2 = iys2;
        for (int y = 0, ys = iys; y < dh; y++, ys += oys, ys2 += oys2)
        {
            uintpixel_t* dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
            const uintpixel_t* srcline;
            if (dh == sh)
                srcline = &texture->buffer[(sy + ys) * texture->width + sx];
            else
                srcline = &texture->buffer[(sy + (int)(ys2 >> fp_prec)) * texture->width + sx];
            
            fixedp_t xs2 = ixs2;
            if (untinted && !flipX && xstep == (1 << fp_prec))
            {
                // Unscaled: no stepping through the source needed.
                for (int x = 0; x < dw; x++)
                {
                    uintpixel_t pixel = srcline[x];
                    if (swrIsOpaque(pixel))
                        dstline[x] = pixel;
                }
            }
            else if (untinted)
            {
                for (int x = 0; x < dw; x++, xs2 += oxs2)
                {
                    uintpixel_t pixel = srcline[(int)(xs2 >> fp_prec)];
                    if (swrIsOpaque(pixel))
                        dstline[x] = pixel;
                }
            }
            else
            {
                for (int x = 0; x < dw; x++, xs2 += oxs2)
                {
                    uintpixel_t pixel = srcline[(int)(xs2 >> fp_prec)];
                    if (!swrIsOpaque(pixel))
                        continue;
                    
                    if (pixel != lastPixel) {
                        lastTinted = tint(tintColor, pixel);
                        lastPixel = pixel;
                    }
                    dstline[x] = lastTinted;
                }
            }
        }
        return;
    }
#endif
    
    if (sw == dw)
    {
        fixedp_t ys2 = (fixedp_t) iys2;
        for (int y = 0, ys = iys; y < dh; y++, ys += oys, ys2 += oys2)
        {
            uintpixel_t* dstline;
            const uintpixel_t* srcline;
            dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
            if (dh == sh)
                srcline = &texture->buffer[(sy + ys) * texture->width + sx];
            else
                srcline = &texture->buffer[(sy + (int)(ys2 >> fp_prec)) * texture->width + sx];
            
            for (int x = 0, xs = ixs; x < dw; x++, xs += oxs)
            {
                uintpixel_t pixel = srcline[xs];
                if (swrIsOpaque(pixel))
                    alphaBlend(&dstline[x], tint(tintColor, pixel), blendmode, alpha);
#ifndef SW_NO_SEMI_TRANSPARENT_TEXTURE_SUPPORT
                else if (!swrIsFullyTransparent(pixel))
                    swrCalculateAlphaBlending(&dstline[x], pixel, tintColor, alpha, blendmode);
#endif
            }
        }
    }
    else
    {
        fixedp_t ys2 = iys2;
        for (int y = 0, ys = iys; y < dh; y++, ys += oys, ys2 += oys2)
        {
            uintpixel_t* dstline;
            const uintpixel_t* srcline;
            dstline = &swr->fb[(dy + y) * swr->fbPitch + dx];
            if (dh == sh)
                srcline = &texture->buffer[(sy + ys) * texture->width + sx];
            else
                srcline = &texture->buffer[(sy + (int)(ys2 >> fp_prec)) * texture->width + sx];
            
            fixedp_t xs2 = ixs2;
            for (int x = 0; x < dw; x++, xs2 += oxs2)
            {
                uintpixel_t pixel = srcline[(int)(xs2 >> fp_prec)];
                if (swrIsOpaque(pixel))
                    alphaBlend(&dstline[x], tint(tintColor, pixel), blendmode, alpha);
#ifndef SW_NO_SEMI_TRANSPARENT_TEXTURE_SUPPORT
                else if (!swrIsFullyTransparent(pixel))
                    swrCalculateAlphaBlending(&dstline[x], pixel, tintColor, alpha, blendmode);
#endif
            }
        }
    }
}

static void swrDrawSpriteRotatedInternal(
    Renderer* renderer, int dx, int dy, int dw, int dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uintpixel_t tintColor, int alpha,
    float angleDeg,
    float pivotX,
    float pivotY
)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    float angleRad = -angleDeg * M_PI / 180.0f;
    
    bool flipX = false, flipY = false;
    if (dw < 0) { dw = -dw; dx -= dw; pivotX = dw - pivotX; flipX = true; }
    if (dh < 0) { dh = -dh; dy -= dh; pivotY = dh - pivotY; flipY = true; }
    
    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);
    
    float cnrx[4], cnry[4];
    cnrx[0] = cnrx[3] = dx;
    cnry[0] = cnry[1] = dy;
    cnrx[1] = cnrx[2] = dx + dw;
    cnry[2] = cnry[3] = dy + dh;
    
    float pxa = pivotX + dx;
    float pya = pivotY + dy;
    
    float minXf = FLT_MAX, minYf = FLT_MAX, maxXf = -FLT_MAX, maxYf = -FLT_MAX;
    for (int i = 0; i < 4; i++)
    {
        float cxi = cnrx[i] - pxa;
        float cyi = cnry[i] - pya;
        float rx = cosA * cxi - sinA * cyi + pxa;
        float ry = sinA * cxi + cosA * cyi + pya;
        if (minXf > rx) minXf = rx;
        if (maxXf < rx) maxXf = rx;
        if (minYf > ry) minYf = ry;
        if (maxYf < ry) maxYf = ry;
    }

    // minX, minY, maxX, maxY now represent an AABB of pixels we should loop over
    int minX = swrFloor(minXf);
    int minY = swrFloor(minYf);
    int maxX = swrCeiling(maxXf);
    int maxY = swrCeiling(maxYf);
    
    // basic out-of-bound checks
    if (maxX < swr->portX) return;
    if (maxY < swr->portY) return;
    if (minX >= swr->maxX) return;
    if (minY >= swr->maxY) return;
    
    // however, we'll need to clip it against out of bounds first
    int minXc = minX, minYc = minY, maxXc = maxX, maxYc = maxY;
    int minx = swr->portX, miny = swr->portY, maxx = swr->portX + swr->portW, maxy = swr->portY + swr->portH;
    
    if (minXc < minx) minXc = minx;
    if (minYc < miny) minYc = miny;
    if (maxXc >= maxx) maxXc = maxx;
    if (maxYc >= maxy) maxYc = maxy;
    
    // some final clip checks
    if (minXc >= maxXc || minYc >= maxYc) return;
    
    int sox = flipX ? sw - 1 : 0;
    int soy = flipY ? sh - 1 : 0;
    int six = flipX ? -1 : 1;
    int siy = flipY ? -1 : 1;
    
    float sw_dw = (float) sw / dw;
    float sh_dh = (float) sh / dh;
    
    int blendmode = swr->blendMode;

    for (int cy = minYc; cy < maxYc; cy++)
    {
        uintpixel_t *dstline = &swr->fb[cy * swr->fbPitch];
        for (int cx = minXc; cx < maxXc; cx++)
        {
            // we need to determine the texture-space coordinate of cx/cy
            float ox = (float) cx + 0.5f - pxa;
            float oy = (float) cy + 0.5f - pya;
            
            // "undo" the rotation
            float lx =  cosA * ox + sinA * oy;
            float ly = -sinA * ox + cosA * oy;
            
            // turn it into a texture-local coordinate
            lx += pxa - dx;
            ly += pya - dy;
            
            if (lx < 0 || ly < 0 || lx >= (float) dw || ly >= (float) dh) continue;
            
            lx = lx * sw_dw;
            ly = ly * sh_dh;
            
            int tx = (int)(sox + lx * six);
            int ty = (int)(soy + ly * siy);
            
            if (tx < 0) tx = 0;
            if (ty < 0) ty = 0;
            if (tx >= sw) tx = sw - 1;
            if (ty >= sh) ty = sh - 1;
            
            tx += sx;
            ty += sy;
            
            uintpixel_t src = texture->buffer[ty * texture->width + tx];

            if (swrIsOpaque(src))
                alphaBlend(&dstline[cx], tint(tintColor, src), blendmode, alpha);
#ifndef SW_NO_SEMI_TRANSPARENT_TEXTURE_SUPPORT
            else if (!swrIsFullyTransparent(src))
                swrCalculateAlphaBlending(&dstline[cx], src, tintColor, alpha, blendmode);
#endif
        }
    }
}

static void swrDrawTriangleInternal(SWRenderer* swr, int xup, int yup, int xleft, int yleft, int xright, int yright, uintpixel_t color1, uintpixel_t color2, uintpixel_t color3, int alpha)
{
    int blendmode = swr->blendMode;
    
    // Figure out the maximum Y extent of the triangle.
    // (Note that we know yup is the minimum.)
    int xmid, ymid, xmid2 = xup, xmax, ymax;
    if (yleft < yright) {
        xmax = xright, ymax = yright;
        xmid = xleft, ymid = yleft;
        if (yright != yup)
            xmid2 = xup + (xright - xup) * (ymid - yup) / (yright - yup);
    } else {
        xmax = xleft, ymax = yleft;
        xmid = xright, ymid = yright;
        if (yleft != yup)
            xmid2 = xup + (xleft - xup) * (ymid - yup) / (yleft - yup);
    }
    
    if (color1 == color2 && color2 == color3)
    {
        // fast path: the triangle is all the same color
        for (int y = yup; y < ymax; y++)
        {
            if (y >= swr->height) break;
            
            int x1 = xup, x2 = xup;
            if (y <= ymid)
            {
                // Lines: between up and mid, and between up and max
                if (ymid != yup)
                    x1 = xup + (xmid - xup) * (y - yup) / (ymid - yup);
                
                if (ymid != yup)
                    x2 = xup + (xmid2 - xup) * (y - yup) / (ymid - yup);
            }
            else
            {
                // Lines: between mid and max, and between up and max
                if (ymax != yup)
                    x1 = xup + (xmax - xup) * (y - yup) / (ymax - yup);
                
                if (ymax != ymid)
                    x2 = xmid + (xmax - xmid) * (y - ymid) / (ymax - ymid);
            }
            
            if (x1 >= x2) {
                int tmp = x1;
                x1 = x2;
                x2 = tmp;
            }
            
            if (x1 < swr->portX) x1 = swr->portX;
            if (x1 >= swr->maxX) continue;
            if (x2 < swr->portX) continue;
            if (x2 >= swr->maxX) x2 = swr->maxX - 1;
            if (x1 > x2) continue;
            
            if (y >= 0) {
                uintpixel_t* line = &swr->fb[y * swr->width];
                for (int x = x1; x < x2; x++) {
                    alphaBlend(&line[x], color1, blendmode, alpha);
                }
            }
        }
        
        return;
    }
    
    // slow path: the triangle is all three different colors
    
    int area = (xleft - xup) * (yright - yup) - (yleft - yup) * (xright - xup);
    if (area == 0) {
        //logDebug("SWR: Area is 0, returning early. Coords: (%d,%d) (%d,%d) (%d,%d)\n", xup, yup, xleft, yleft, xright, yright);
        return;
    }
    
    // tried making this 32-bit and it didn't work.  maybe another day.
    int64_t areaReciprocal = ((int64_t) 65535 << 16) / area;
    for (int y = yup; y < ymax; y++)
    {
        if (y >= swr->height) break;
        
        int x1 = xup, x2 = xup;
        if (y <= ymid)
        {
            // Lines: between up and mid, and between up and max
            if (ymid != yup)
                x1 = xup + (xmid - xup) * (y - yup) / (ymid - yup);
            
            if (ymid != yup)
                x2 = xup + (xmid2 - xup) * (y - yup) / (ymid - yup);
        }
        else
        {
            // Lines: between mid and max, and between up and max
            if (ymax != yup)
                x1 = xup + (xmax - xup) * (y - yup) / (ymax - yup);
            
            if (ymax != ymid)
                x2 = xmid + (xmax - xmid) * (y - ymid) / (ymax - ymid);
        }
        
        if (x1 >= x2) {
            int tmp = x1;
            x1 = x2;
            x2 = tmp;
        }
        
        if (x1 < swr->portX) x1 = swr->portX;
        if (x1 >= swr->maxX) continue;
        if (x2 < swr->portX) continue;
        if (x2 >= swr->maxX) x2 = swr->maxX - 1;
        if (x1 > x2) continue;
        
        if (y < 0) continue;
        
        uintpixel_t* line = &swr->fb[y * swr->width];
        
        int e_up   = (xleft - x1) * (yright - y) - (yleft - y) * (xright - x1);
        int e_left = (xright - x1) * (yup - y) - (yright - y) * (xup - x1);
        int d_up   = (yleft - yright);
        int d_left = (yright - yup);
        
        for (int x = x1; x < x2; x++)
        {
            int w1 = (int)((e_up * areaReciprocal) >> 16);
            int w2 = (int)((e_left * areaReciprocal) >> 16);
            
            if (w1 < 0) w1 = 0;
            if (w1 > 65535) w1 = 65535;
            if (w2 < 0) w2 = 0;
            if (w2 > 65535 - w1) w2 = 65535 - w1;
            
            uintpixel_t blended = swrThreeWayBlend(color1, color2, color3, w1, w2, 65535 - w1 - w2);
            alphaBlend(&line[x], blended, blendmode, alpha);
            
            e_up += d_up;
            e_left += d_left;
        }
    }
}

static void swrDrawTriangleTransformed(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, uintpixel_t color1, uintpixel_t color2, uintpixel_t color3, int alpha)
{
    float xup, yup, xleft, yleft, xright, yright;
    uint32_t colorup, colorleft, colorright;
    
    SWRenderer* swr = (SWRenderer*) renderer;
    
    //which vertex is higher?
    xup = x1, yup = y1; colorup = color1;
    xleft = x2, yleft = y2; colorleft = color2;
    xright = x3, yright = y3; colorright = color3;
    if (yup > y2) {
        xup = x2, yup = y2, colorup = color2;
        xleft = x1, yleft = y1, colorleft = color1;
        //xright = x3, yright = y3;
    }
    if (yup > y3) {
        xup = x3, yup = y3, colorup = color3;
        xleft = x1, yleft = y1, colorleft = color1;
        xright = x2, yright = y2, colorright = color2;
    }
    
    if (xleft > xright) {
        float tmp = xleft;
        xleft = xright;
        xright = tmp;
        tmp = yleft;
        yleft = yright;
        yright = tmp;
        uint32_t tmp2 = colorleft;
        colorleft = colorright;
        colorright = tmp2;
    }
    
    swrDrawTriangleInternal(
        swr,
        swrFloor(xup), swrFloor(yup),
        swrFloor(xleft), swrCeiling(yleft),
        swrFloor(xright), swrCeiling(yright),
        colorup,
        colorleft,
        colorright,
        alpha
    );
}

// ==== Exposed interface ====

bool swrSwitchToSurface(Renderer* renderer, int32_t targetSurfaceId, bool restoreOldView)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    if (swr->drawingToSurface) {
        swrCommitShadowWritesToSurfaceIfNeeded(swr, swr->surfaces[swr->currentSurfaceIndex]);
    }
    
    if (targetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER)
    {
        if (!swr->drawingToSurface)
            return true;
        
        // restore the original framebuffer
        //logDebug("swr: back to original framebuffer (%d)\n", targetSurfaceId);
        swr->drawingToSurface = false;
        swr->fb = swr->mainFb;
        swr->width = swr->mainWidth;
        swr->height = swr->mainHeight;
        swr->fbPitch = swr->mainPitch;
        swr->currentSurfaceIndex = -1;
        swr->writeMask = WRITE_MASK_ALL;
        
        if (restoreOldView) {
            // restore the old transform, if needed
            swr->viewX = swr->lastViewX;
            swr->viewY = swr->lastViewY;
            swr->viewW = swr->lastViewW;
            swr->viewH = swr->lastViewH;
            swr->portX = swr->lastPortX;
            swr->portY = swr->lastPortY;
            swr->portW = swr->lastPortW;
            swr->portH = swr->lastPortH;
            swr->gameW = swr->lastGameW;
            swr->gameH = swr->lastGameH;
            swr->maxX = swr->lastMaxX;
            swr->maxY = swr->lastMaxY;
            swr->scaleX = swr->lastScaleX;
            swr->scaleY = swr->lastScaleY;
        }
        return true;
    }
    
    if (targetSurfaceId < 0 || (size_t) targetSurfaceId >= swr->surfaceCount || swr->surfaces[targetSurfaceId] == NULL) {
        logError("swr: Invalid surface id %d\n", targetSurfaceId);
        return false;
    }
    
    if (!swr->drawingToSurface)
    {
        // back up the original framebuffer
        swr->drawingToSurface = true;
        swr->mainFb = swr->fb;
        swr->mainWidth = swr->width;
        swr->mainHeight = swr->height;
        swr->mainPitch = swr->fbPitch;
        
        // and the old transform
        swr->lastViewX = swr->viewX;
        swr->lastViewY = swr->viewY;
        swr->lastViewW = swr->viewW;
        swr->lastViewH = swr->viewH;
        swr->lastPortX = swr->portX;
        swr->lastPortY = swr->portY;
        swr->lastPortW = swr->portW;
        swr->lastPortH = swr->portH;
        swr->lastGameW = swr->gameW;
        swr->lastGameH = swr->gameH;
        swr->lastMaxX = swr->maxX;
        swr->lastMaxY = swr->maxY;
        swr->lastScaleX = swr->scaleX;
        swr->lastScaleY = swr->scaleY;
    }
    
    SWTexture* surface = swr->surfaces[targetSurfaceId]->texture;
    swr->fb = surface->buffer;
    swr->width = surface->width;
    swr->height = surface->height;
    swr->fbPitch = surface->width;
    swr->drawingToSurface = true;
    swr->currentSurfaceIndex = targetSurfaceId;
    swr->writeMask = WRITE_MASK_ALL;
    
    swr->viewX = swr->portX = 0;
    swr->viewY = swr->portY = 0;
    swr->maxX = swr->viewW = swr->portW = surface->width;
    swr->maxY = swr->viewH = swr->portH = surface->height;
    swr->scaleX = swr->scaleY = 1.0f;
    
    logDebug("swr: switching to surface %d -> %p, fb %p, %dx%d\n", targetSurfaceId, surface, swr->fb, swr->width, swr->height);
    
    return true;
}

void swrPlotPixel(Renderer* renderer, float x, float y, uintpixel_t color, float alpha)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    int blendmode = swr->blendMode;
    swrPlotPixel_(renderer, (float) x, (float) y, color, blendmode, alpha);
}

void swrDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uintpixel_t color, uintpixel_t color2, float alpha, int alignment)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    swrTransformPosIfNeeded(swr, &x1, &y1);
    swrTransformPosIfNeeded(swr, &x2, &y2);
    swrTransformSizeIfNeeded(swr, &width, NULL);
    int iwidth = swrRound(width);
    swrDrawLineInt(renderer, swrFloor(x1), swrFloor(y1), swrFloor(x2), swrFloor(y2), iwidth, color, color2, swrIntAlpha(alpha), alignment);
}

void swrDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t color, float alpha)
{
    int x1i = swrRound(x1), x2i = swrRound(x2), y1i = swrRound(y1), y2i = swrRound(y2);
    swrDrawLine(renderer, x1i, y1i, x2i, y1i, 1.0f, color, color, alpha, SWR_LINE_ALIGN_P1);
    swrDrawLine(renderer, x1i, y2i, x2i, y2i, 1.0f, color, color, alpha, SWR_LINE_ALIGN_M1);
    swrDrawLine(renderer, x1i, y1i, x1i, y2i, 1.0f, color, color, alpha, SWR_LINE_ALIGN_P1);
    swrDrawLine(renderer, x2i, y1i, x2i, y2i, 1.0f, color, color, alpha, SWR_LINE_ALIGN_M1);
}

void swrDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t color1, uintpixel_t color2, uintpixel_t color3, uintpixel_t color4, float alpha)
{
    int x1i = swrRound(x1), x2i = swrRound(x2), y1i = swrRound(y1), y2i = swrRound(y2);
    swrDrawLine(renderer, x1i, y1i, x2i, y1i, 1.0f, color1, color2, alpha, SWR_LINE_ALIGN_P1);
    swrDrawLine(renderer, x1i, y2i, x2i, y2i, 1.0f, color3, color4, alpha, SWR_LINE_ALIGN_M1);
    swrDrawLine(renderer, x1i, y1i, x1i, y2i, 1.0f, color1, color3, alpha, SWR_LINE_ALIGN_P1);
    swrDrawLine(renderer, x2i, y1i, x2i, y2i, 1.0f, color2, color4, alpha, SWR_LINE_ALIGN_M1);
}

void swrFillRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t pxcolor, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    swrTransformPosIfNeeded(swr, &x1, &y1);
    swrTransformPosIfNeeded(swr, &x2, &y2);

    int alphaInt = swrIntAlpha(alpha);
    int x1i = swrFloor(x1), x2i = swrCeiling(x2), y1i = swrFloor(y1), y2i = swrCeiling(y2);
    int xd = x2i - x1i;
    int yd = y2i - y1i;
    if (xd < 0) { x1i = x2i; xd = -xd; }
    if (yd < 0) { y1i = y2i; yd = -yd; }
    if (xd <= 0 || yd <= 0) return;
    
    // A fill of the whole main buffer. While the buffer is known to hold one
    // colour, the fill turns it into one other colour that can be worked out
    // without touching it: the fill's own if opaque, the blend of the two if
    // translucent. Nothing is written when that is the colour already there
    // (a game darkening a black screen, or clearing one twice), and one plain
    // fill replaces a read-blend-write of every pixel otherwise.
    bool wholeBuffer = swr->fb == swr->mainFb && swr->blendMode == bm_normal && alphaInt >= 4 &&
                       swr->portX == 0 && swr->portY == 0 && swr->maxX >= swr->mainWidth && swr->maxY >= swr->mainHeight &&
                       x1i <= 0 && y1i <= 0 && x1i + xd >= swr->mainWidth && y1i + yd + 1 >= swr->mainHeight;
    bool opaque = alphaInt > 253;
#ifdef SW_HAS_PREMUL_BLEND
    if (wholeBuffer && swr->uniformKept) {
        uintpixel_t result = opaque ? pxcolor
            : swrBlendPremultiplied(swr->uniformColor, swrSpreadRedBlue(pxcolor) * (uint32_t) alphaInt,
                                    swrGreen(pxcolor) * (uint32_t) alphaInt, (uint32_t) (256 - alphaInt));
        if (result != swr->uniformColor) {
            for (int y = 0; y < swr->mainHeight; y++)
                swrFillPixels(&swr->fb[y * swr->fbPitch], (size_t) swr->mainWidth, result);
            swr->uniformColor = result;
        }
        swr->uniformValid = true;
        return;
    }
#endif
    
    for (int y = 0; y <= yd; y++) {
        swrDrawHLineInt(renderer, x1i, y1i + y, xd, pxcolor, pxcolor, alphaInt);
    }
    if (wholeBuffer && opaque) {
        swr->uniformValid = true;
        swr->uniformColor = pxcolor;
    }
}

void swrFillRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t pxcolor1, uintpixel_t pxcolor2, uintpixel_t pxcolor3, uintpixel_t pxcolor4, float alpha)
{
    if (pxcolor1 == pxcolor2 && pxcolor2 == pxcolor3 && pxcolor3 == pxcolor4) {
        swrFillRectangle(renderer, x1, y1, x2, y2, pxcolor1, alpha);
        return;
    }
    
    SWRenderer* swr = (SWRenderer*) renderer;
    swrTransformPosIfNeeded(swr, &x1, &y1);
    swrTransformPosIfNeeded(swr, &x2, &y2);

    int alphaInt = swrIntAlpha(alpha);
    int x1i = swrFloor(x1), x2i = swrCeiling(x2), y1i = swrFloor(y1), y2i = swrCeiling(y2);
    int xd = x2i - x1i;
    int yd = y2i - y1i;
    if (xd < 0) { x1i = x2i; xd = -xd; }
    if (yd < 0) { y1i = y2i; yd = -yd; }
    if (xd <= 0 || yd <= 0) return;
    
#ifndef SW_DITHERED_BLENDING
    uint32_t inc = (65536U << 14) / yd;
    uint32_t weightfp = 0;
    for (int y = 0; y <= yd; y++, weightfp += inc)
    {
        uint32_t weight2 = (uint32_t)(weightfp >> 14);
        if (weight2 > 65535) weight2 = 65535;
        uint32_t weight1 = 65535 - weight2;
        
        uintpixel_t intcolor1, intcolor2;
        intcolor1 = swrTwoWayBlend(pxcolor1, pxcolor4, (uint16_t) weight1, (uint16_t) weight2);
        intcolor2 = swrTwoWayBlend(pxcolor2, pxcolor3, (uint16_t) weight1, (uint16_t) weight2);
        
        swrDrawHLineInt(renderer, x1i, y1i + y, xd, intcolor1, intcolor2, alphaInt);
    }
#else
    // Dithered blending CANNOT use the above code because of its inherent randomness.
    // It chooses either one color or the other based on the alpha / probability.
    // As such, we need a slightly more complex four-way operation.
    int blendmode = swr->blendMode;
    
    uint32_t incx = (65536U << 14) / xd;
    uint32_t incy = (65536U << 14) / yd;
    uint32_t weightfpy = 0;
    for (int y = 0, ay = y1i; y <= yd; y++, ay++, weightfpy += incy)
    {
        uint32_t weightD = (uint32_t)(weightfpy >> 14);
        if (weightD > 65535) weightD = 65535;
        uint32_t weightU = 65535 - weightD;
        
        uintpixel_t *line = &swr->fb[ay * swr->fbPitch + x1i];
        uint32_t weightfpx = 0;
        for (int x = 0; x <= xd; x++, weightfpx += incx)
        {
            if (UNLIKELY(srcalpha < 250)) {
                if ((swrFastRng() & 0xFF) >= srcalpha)
                    continue;
            }
            
            uint32_t weightR = (uint32_t)(weightfpx >> 14);
            if (weightR > 65535) weightR = 65535;
            uint32_t weightL = 65535 - weightR;
            
            uint16_t weight1 = (uint16_t)((weightL * weightU) >> 16);
            uint16_t weight2 = (uint16_t)((weightR * weightU) >> 16);
            uint16_t weight3 = (uint16_t)((weightR * weightD) >> 16);
            uint16_t weight4 = (uint16_t)((weightL * weightD) >> 16);
            
            uintpixel_t result = swrFourWayBlend(pxcolor1, pxcolor2, pxcolor3, pxcolor4, weight1, weight2, weight3, weight4);
            alphaBlend(&line[x], result, blendmode, 256, 0);
        }
    }
#endif
}

void swrDrawSprite(
    Renderer* renderer, float dx, float dy, float dw, float dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uint32_t tintColor, float alpha
)
{
    SWRenderer *swr = (SWRenderer*) renderer;
    
    swrTransformPosIfNeeded(swr, &dx, &dy);
    swrTransformSizeIfNeeded(swr, &dw, &dh);
    
    swrDrawSpriteInternal(
        renderer,
        swrFloor(dx),
        swrFloor(dy),
        swrCeiling(dw),
        swrCeiling(dh),
        texture,
        sx, sy,
        sw, sh,
        swrConvertPixel(tintColor),
        swrIntAlpha(alpha)
    );
}

void swrDrawSpriteRotated(
    Renderer* renderer, float dx, float dy, float dw, float dh,
    SWTexture* texture, int sx, int sy, int sw, int sh,
    uint32_t tintColor, float alpha,
    float angleDeg,
    float pivotX,
    float pivotY
)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    
    swrTransformPosIfNeeded(swr, &dx, &dy);
    swrTransformSizeIfNeeded(swr, &pivotX, &pivotY);
    swrTransformSizeIfNeeded(swr, &dw, &dh);
    
    swrDrawSpriteRotatedInternal(
        renderer,
        swrFloor(dx),
        swrFloor(dy),
        swrCeiling(dw),
        swrCeiling(dh),
        texture,
        sx, sy,
        sw, sh,
        swrConvertPixel(tintColor),
        swrIntAlpha(alpha),
        angleDeg,
        pivotX,
        pivotY
    );
}

void swrDrawTriangle(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha)
{
    SWRenderer* swr = (SWRenderer*) renderer;
    swrTransformPosIfNeeded(swr, &x1, &y1);
    swrTransformPosIfNeeded(swr, &x2, &y2);
    swrTransformPosIfNeeded(swr, &x3, &y3);
    swrDrawTriangleTransformed(
        renderer,
        x1, y1,
        x2, y2,
        x3, y3,
        swrConvertPixel(color1),
        swrConvertPixel(color2),
        swrConvertPixel(color3),
        swrIntAlpha(alpha)
    );
}
