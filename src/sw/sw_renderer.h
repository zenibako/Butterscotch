#ifndef _SW_RENDERER_H
#define _SW_RENDERER_H

#include "renderer.h"

Renderer* SWRenderer_create(void);

typedef struct {
    int blendMode;
    int writeMask;
    bool usingAlphaBlendState;
} SWRendererSnapshotState;

/* Returns false only when an SW renderer still has in-flight draw work. */
bool SWRenderer_snapshotGetState(Renderer* renderer, SWRendererSnapshotState* state);
void SWRenderer_snapshotApplyState(Renderer* renderer, const SWRendererSnapshotState* state);

void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color);

// Platform opt-in for selected UI assets; only exact half-size Speed draws are adjusted.
void SWRenderer_setContrastAssets(Renderer* renderer, const char* const* sprites, size_t spriteCount,
                                 const char* const* fonts, size_t fontCount);
// Add transparent-ink sprites and white HUD fonts to the configured asset set.
void SWRenderer_setContrastGlyphAssets(Renderer* renderer, const char* const* sprites, size_t spriteCount,
                                      const char* const* fonts, size_t fontCount);
void SWRenderer_setContrastEnabled(Renderer* renderer, bool enabled);

#endif//_SW_RENDERER_H
