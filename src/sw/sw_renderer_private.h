#ifndef _SW_RENDERER_PRIVATE_H
#define _SW_RENDERER_PRIVATE_H

#include "sw_renderer.h"
#include "sw_defines.h"
#include "sw_pixel_convert.h"

// Unimplemented Functions
#define UNIMP() do { logWarn("NYI %s\n", __func__); } while (0)
//#define UNIMP() do { } while (0)
#define UNIMP2() do { } while (0)

// Provide PI if not specified
#ifndef M_PI
#define M_PI 3.1415926535897932384626
#endif

// Struct Definitions
typedef struct
{
    uintpixel_t* buffer;
    uint16_t width, height;
    uint32_t lastUsedFrame; // SWRenderer.frameCounter when last drawn, for cache eviction
}
SWTexture;

typedef struct
{
    // used for almost all intents and purposes.
    SWTexture* texture;
    // upon a gpuSetColorWriteEnable change, the shadow texture is used for writing instead.
    SWTexture* shadowTexture;
}
SWSurface;

#define WRITE_MASK_ALL   (15)
#define WRITE_MASK_RED   (1)
#define WRITE_MASK_GREEN (2)
#define WRITE_MASK_BLUE  (4)
#define WRITE_MASK_ALPHA (8)

typedef struct {
    float x, y;
    uintpixel_t color;
} SWVertex;

typedef struct
{
    Renderer base;
    
    // Window Properties
    uint16_t width;
    uint16_t height;
    // Framebuffer
    uintpixel_t* fb;
    uint16_t fbPitch; // in sizeof(uintpixel_t) units, NOT in bytes!
    
    bool drawingToSurface;
    uintpixel_t* mainFb;
    uint16_t mainWidth;
    uint16_t mainHeight;
    uint16_t mainPitch;
    int lastViewX, lastViewY, lastViewW, lastViewH;
    int lastPortX, lastPortY, lastPortW, lastPortH;
    int lastGameW, lastGameH, lastMaxX, lastMaxY;
    float lastScaleX, lastScaleY;
    
    SWTexture** textures;
    SWSurface** surfaces;
    uint32_t frameCounter;
    size_t textureCount;
    size_t surfaceCount;
    size_t totalTextureCount;
    size_t originalTPagCount;
    size_t originalSpriteCount;
    
    bool viewActive;
    int viewX, viewY, viewW, viewH;
    int portX, portY, portW, portH;
    int gameW, gameH, maxX, maxY;

    int offsetX, offsetY;
    float scaleX, scaleY;
    float defaultScaleX, defaultScaleY;
    
    int blendMode;
    bool usingAlphaBlendState;
    
    // only used for surfaces.  The application surface doesn't support these at the moment.
    int currentSurfaceIndex;
    int writeMask;
    
    SWVertex* vertexData;
    int vertexCount;
    int maxVertexCount;
    int primitiveType;
    bool primitiveBegun;
    bool primitiveOverflow;
}
SWRenderer;

// Inlined function definitions included below
#include "sw_pixel_calc.h"
#include "sw_inlined.h"
#include "sw_transform.h"

// Fills `count` pixels with one colour, two at a time where it can.
FORCE_INLINE void swrFillPixels(uintpixel_t* dst, size_t count, uintpixel_t color)
{
#if PIXEL_SIZE == 16
    if (count > 0 && ((uintptr_t) dst & 2)) {
        *dst++ = color;
        count--;
    }
    uint32_t pair = ((uint32_t) color << 16) | color;
    uint32_t* dst32 = (uint32_t*) (void*) dst;
    size_t pairs = count / 2;
    for (size_t i = 0; i < pairs; i++)
        dst32[i] = pair;
    if (count & 1)
        dst[count - 1] = color;
#else
    for (size_t i = 0; i < count; i++)
        dst[i] = color;
#endif
}

#include "sw_texture.h"
#include "sw_surface.h"
#include "sw_drawing.h"
#include "sw_texture_lru.h"
#include "sw_text.h"

#endif//_SW_RENDERER_PRIVATE_H
