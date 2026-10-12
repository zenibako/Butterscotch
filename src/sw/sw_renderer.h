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

#endif//_SW_RENDERER_H
