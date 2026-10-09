#ifndef _SW_RENDERER_PRIVATE_H
#define _SW_RENDERER_PRIVATE_H

#include "sw_renderer.h"
#include "sw_defines.h"
#include "sw_pixel_convert.h"

// Frees the composed tile-run pictures (sw_renderer.c); they are rebuilt on
// the next draw. Returns false if there were none.
bool swrTileRunsFree(void);


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
    // Where buffer[0] is on its texture page. Zero for a whole page; a texture
    // that holds only one TPAG item's part of a page (see swrTextureForItem)
    // starts at that part's corner, so page coordinates minus this index it.
    uint16_t originX, originY;
    uint32_t lastUsedFrame; // SWRenderer.frameCounter when last drawn, for cache eviction
    // A copy at half the size each way, made the first time the texture is
    // drawn at half scale with swrFavorSpeed set (see swrHalfTexture), so
    // that such draws need no averaging. Only for a texture whose pixels never
    // change (immutable: one loaded from the game's data, not a surface).
    uintpixel_t* halfBuffer;
    uint8_t* halfCoverage;  // how many of its four texels each copied one stands for, 0..4; NULL when every one is 0 or 4
    bool immutable;
    uint8_t halfPhaseX, halfPhaseY; // 0 or 1: where the 2x2 blocks start (see swrHalfTexture)
    // Whether every texel is opaque, so that an opaque draw can copy rows
    // without looking at them: 0 not looked at yet, 1 yes, 2 no. Looked at
    // only for an immutable texture; halfSolid is the same for the half-size copy.
    uint8_t solid, halfSolid;
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

// The arguments of one swrDrawSpriteInternal call, kept so it can be issued later.
#define SW_MIRROR_MAX_LAYERS 12
typedef struct {
    int dx, dy, dw, dh;
    SWTexture* texture;
    int sx, sy, sw, sh;
    uintpixel_t tintColor;
    int alpha;
} SWSpriteCall;

// One layer of a mirrored stack: a sprite drawn as four quarters, or a solid
// fill of the whole mirrored area that followed them.
typedef struct {
    SWSpriteCall calls[4];      // the quarters as asked for; a fill uses calls[0]'s tintColor and alpha
    int sx, sy, xstep, ystep;   // how the unflipped quarter samples its texture, after clipping
    bool solid;
} SWMirrorLayer;

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
    // Per-TPAG-item textures for pages that are not loaded whole; see swrTextureForItem.
    SWTexture** itemTextures;
    size_t itemCount;
    size_t itemBytes;
    SWSurface** surfaces;
    uint32_t frameCounter;
    bool pendingClear;      // clearFrameBuffer was requested but not done yet (see swrFlushPendingClear)
    uintpixel_t pendingClearColor;
    bool fbIsPlatform; // mainFb belongs to the platform (SW_PLATFORM_FRAMEBUFFER), not to us
    
    // A solid-colour sprite draw that has been held back so that identical
    // draws stacked directly on top of it fold into one blend (see swrOverlayFlush).
    bool overlayMergeAllowed;   // set only while SWRenderer_drawSprite is running
    int overlayCount;           // draws held; 0 when nothing is pending
    uintpixel_t* overlayFb;
    int overlayPitch;
    int overlayX, overlayY, overlayW, overlayH;
    uintpixel_t overlayFirstColor;  // the first draw, kept exactly so that a lone draw is unchanged
    int overlayFirstAlpha;
    float overlayKeep;              // share of the destination that still shows through
    float overlayRed, overlayGreen, overlayBlue; // accumulated colour, 5-bit channel * 256
    
    // Translucent layers drawn as four mirrored quarters, held back so that one
    // quarter can be blended and copied to the other three (see swrMirrorFlush).
    bool mirrorReplaying;       // held draws are being issued; hold nothing
    int mirrorLayers;           // complete layers held
    int mirrorStage;            // quarters of the next layer seen so far, 0..3
    uintpixel_t* mirrorFb;
    int mirrorPitch;
    int mirrorPort[6];          // portX, portY, portW, portH, maxX, maxY when the first quarter was held
    int mirrorCx, mirrorCy, mirrorW, mirrorH; // the unflipped quarter's clipped rectangle
    bool mirrorUnderKnown;      // everything under the stack was one colour when it was started
    uintpixel_t mirrorUnder;
    SWMirrorLayer mirrorStack[SW_MIRROR_MAX_LAYERS + 1];
    
    // The whole main buffer holds one colour: set by a full clear or fill,
    // dropped by the next thing drawn. Lets a repeated fill be skipped and a
    // mirrored stack be worked out without reading the buffer back.
    bool uniformValid;
    uintpixel_t uniformColor;
    bool uniformKept;           // for the one swrFillRectangle call SWRenderer_drawRectangle is making
    bool tileRunEntering;       // SWRenderer_drawTileRun is flushing: leave held tile pictures held
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
