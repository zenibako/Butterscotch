#include <stdlib.h>
#include "sw_renderer_private.h"
#include "sw_texture_pack.h"
#include "image/image_decoder.h"
#include "gettime.h"

// Texture pages are decoded on first use and kept until the cache goes over
// TEXTURE_CACHE_BYTES, at which point the least recently used page goes.
//
// Pages drawn during the current frame are never evicted to make room: a
// frame that needs more pages than the budget allows runs over budget
// instead of re-decoding every page every frame. They are only given up when
// an allocation actually fails.

static size_t pageBytes(const SWTexture* texture)
{
    return (size_t) texture->width * texture->height * sizeof(uintpixel_t);
}

static size_t cachedBytes(const SWRenderer* swr)
{
    size_t total = 0;
    for (size_t i = 0; i < swr->textureCount; i++) {
        if (swr->textures[i]) total += pageBytes(swr->textures[i]);
    }
    return total;
}

void swrEvictTextureFromCache(SWRenderer* swr, int textureIndex)
{
    SWTexture* texture = swr->textures[textureIndex];
    swr->textures[textureIndex] = NULL;
    
    swrFreeTexture(texture);
}

// Evicts the least recently used page. Returns false if nothing qualified.
static bool evictLeastRecentlyUsed(SWRenderer* swr, bool includeCurrentFrame)
{
    int victim = -1;
    uint32_t oldest = 0;
    
    for (size_t i = 0; i < swr->textureCount; i++) {
        const SWTexture* texture = swr->textures[i];
        if (!texture) continue;
        if (!includeCurrentFrame && texture->lastUsedFrame == swr->frameCounter) continue;
        
        uint32_t age = swr->frameCounter - texture->lastUsedFrame;
        if (victim == -1 || age > oldest) {
            victim = (int) i;
            oldest = age;
        }
    }
    
    if (victim == -1) return false;
    
    logInfo("SWR: Unloaded TXTR page %d%s\n", victim, includeCurrentFrame ? " (in use, out of memory)" : "");
    swrEvictTextureFromCache(swr, victim);
    return true;
}

static void makeRoomFor(SWRenderer* swr, size_t bytes)
{
    while (cachedBytes(swr) + bytes > TEXTURE_CACHE_BYTES) {
        if (!evictLeastRecentlyUsed(swr, false)) break;
    }
}

// Allocates, giving up cached pages (even ones in use this frame) and then
// the renderer's other rebuildable pictures rather than failing.
static void* allocOrEvict(SWRenderer* swr, size_t bytes)
{
    for (;;) {
        void* ptr = malloc(bytes);
        if (ptr) return ptr;
        if (evictLeastRecentlyUsed(swr, true)) continue;
        if (!swrTileRunsFree()) return NULL;
        logInfo("SWR: Dropped the tile pictures (out of memory)\n");
    }
}

// The largest block malloc will hand out right now, to within 64 KB. Only
// called when a page failed to load, to say how far short the heap is.
static size_t largestFreeBlock(size_t limit)
{
    size_t low = 0, high = limit;
    while (high - low > 64 * 1024) {
        size_t middle = low + (high - low) / 2;
        void* probe = malloc(middle);
        if (probe) {
            free(probe);
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

// A page that could not be loaded is not tried again for this many frames.
// Trying on every draw call empties the cache each time (every other page is
// given up to make room, then reloaded for the next sprite) and the scene
// runs at a crawl; skipping the page's sprites for a few seconds does not.
#define TEXTURE_RETRY_FRAMES 150
#define TEXTURE_RETRY_PAGES 256
static uint32_t retryAtFrame[TEXTURE_RETRY_PAGES];

static SWTexture* loadFromPack(SWRenderer* swr, uint32_t pageId)
{
    int w, h;
    if (!swrTexturePackGetSize(pageId, &w, &h)) return NULL;
    
    size_t bytes = (size_t) w * h * sizeof(uintpixel_t);
    makeRoomFor(swr, bytes);
    
    SWTexture* texture = (SWTexture*) allocOrEvict(swr, sizeof(SWTexture));
    uintpixel_t* buffer = (uintpixel_t*) allocOrEvict(swr, bytes);
    if (!texture || !buffer || !swrTexturePackDecode(pageId, buffer)) {
        logWarn("SWR: Failed to load TXTR page %u from the texture pack.\n", pageId);
        free(buffer);
        free(texture);
        return NULL;
    }
    
    texture->buffer = buffer;
    texture->width = (uint16_t) w;
    texture->height = (uint16_t) h;
    return texture;
}

static SWTexture* loadFromDataWin(SWRenderer* swr, uint32_t pageId)
{
    DataWin* dw = swr->base.dataWin;
    Texture* txtr = &dw->txtr.textures[pageId];
    bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
    
    int w, h;
    uint8_t* pixels = NULL;
    
    for (;;) {
        if (!txtr->blobData) {
            DataWin_loadTxtrIfNeeded(dw, pageId);
        }
        
        if (txtr->blobData) {
            pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, gm2022_5, &w, &h);
            if (pixels) {
                if (!txtr->mapped) {
                    free(txtr->blobData);
                    txtr->blobData = NULL;
                } else
                    dropMappedRange(txtr->blobData, 0, txtr->blobSize);
                break;
            }
        }
        
        // Most likely out of memory: give up a cached page and try again.
        if (!evictLeastRecentlyUsed(swr, true)) {
            logError("SWR: Failed to decode TXTR page %u.\n", pageId);
            return NULL;
        }
    }
    
    makeRoomFor(swr, (size_t) w * h * sizeof(uintpixel_t));
    SWTexture* texture = swrCreateTexture(pixels, w, h);
    free(pixels);
    return texture;
}

#ifdef TEXTURE_CACHE_RESERVE_BYTES
// On targets with a small fixed heap the byte budget alone is a guess: how
// much the game itself needs varies by scene. After each load, make sure a
// block of TEXTURE_CACHE_RESERVE_BYTES can still be allocated, and give up
// the least recently used pages until it can.
static void keepHeapReserve(SWRenderer* swr)
{
    for (;;) {
        void* probe = malloc(TEXTURE_CACHE_RESERVE_BYTES);
        if (probe) {
            free(probe);
            return;
        }
        if (!evictLeastRecentlyUsed(swr, false)) return;
    }
}
#endif

// Lazily loads a TXTR page on first access.
// Returns true if the texture is ready, false if it failed to load.
bool swrEnsureTextureIsLoaded(SWRenderer* swr, uint32_t pageId)
{
    SWTexture* texture = swr->textures[pageId];
    if (LIKELY(texture != NULL)) {
        texture->lastUsedFrame = swr->frameCounter;
        return true;
    }
    
    // Only real texture pages can be loaded on demand.
    if (pageId >= swr->textureCount) return false;
    if (pageId < TEXTURE_RETRY_PAGES && retryAtFrame[pageId] != 0) {
        if (swr->frameCounter < retryAtFrame[pageId]) return false;
        retryAtFrame[pageId] = 0;
    }
    
    uint64_t loadStart = nowNanos();
    const char* source = "pack";
    int packW = 0, packH = 0;
    bool inPack = swrTexturePackGetSize(pageId, &packW, &packH);
    texture = loadFromPack(swr, pageId);
    // A page the pack holds can only have failed for want of memory, and
    // decoding its PNG needs several times more.
    if (!texture && !inPack) {
        source = "PNG";
        texture = loadFromDataWin(swr, pageId);
    }
    if (!texture) {
        size_t needed = (size_t) packW * packH * sizeof(uintpixel_t);
        logError("SWR: TXTR page %u needs %u KB; largest free block %u KB, cache %u KB\n", pageId, (unsigned) (needed / 1024),
                 (unsigned) (largestFreeBlock(needed ? needed : 16u * 1024u * 1024u) / 1024), (unsigned) (cachedBytes(swr) / 1024));
        if (pageId < TEXTURE_RETRY_PAGES) retryAtFrame[pageId] = swr->frameCounter + TEXTURE_RETRY_FRAMES;
        return false;
    }
    
    texture->lastUsedFrame = swr->frameCounter;
    swr->textures[pageId] = texture;
#ifdef TEXTURE_CACHE_RESERVE_BYTES
    keepHeapReserve(swr);
#endif
    
    logInfo("SWR: Loaded TXTR page %u (%dx%d, %s) took %u ms, cache %u KB\n", pageId, texture->width, texture->height,
            source, (unsigned)((nowNanos() - loadStart) / 1000000u), (unsigned)(cachedBytes(swr) / 1024));
    return true;
}
