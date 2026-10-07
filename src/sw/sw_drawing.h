#ifndef _SW_DRAWING_H
#define _SW_DRAWING_H

enum {
    SWR_LINE_ALIGN_CENTER,
    SWR_LINE_ALIGN_M1,
    SWR_LINE_ALIGN_P1,
};

bool swrSwitchToSurface(Renderer* renderer, int32_t targetSurfaceId, bool restoreOldView);
// When true, sprites drawn at roughly half size are box-filtered instead of point-sampled.
extern bool swrSmoothMinify;

void swrPlotPixel(Renderer* renderer, float x, float y, uintpixel_t color, float alpha);
void swrDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uintpixel_t color, uintpixel_t color2, float alpha, int alignment);
void swrDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t color, float alpha);
void swrDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t color1, uintpixel_t color2, uintpixel_t color3, uintpixel_t color4, float alpha);
void swrFillRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t color, float alpha);
void swrFillRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uintpixel_t color1, uintpixel_t color2, uintpixel_t color3, uintpixel_t color4, float alpha);
// Draws whatever solid overlay swrDrawSprite is holding back. Anything else
// that reads or writes pixels, or changes where they go, must call this first.
void swrOverlayFlush(SWRenderer* swr);
void swrDrawSprite(Renderer* renderer, float dx, float dy, float dw, float dh, SWTexture* texture, int sx, int sy, int sw, int sh, uint32_t tintColor, float alpha);
void swrDrawSpriteRotated(Renderer* renderer, float dx, float dy, float dw, float dh, SWTexture* texture, int sx, int sy, int sw, int sh, uint32_t tintColor, float alpha, float angleDeg, float pivotX, float pivotY);
void swrDrawTriangle(Renderer* renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha);

#endif//_SW_DRAWING_H
