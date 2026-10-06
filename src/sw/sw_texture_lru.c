#include <stdlib.h>
#include "sw_renderer_private.h"
#include "sw_texture_pack.h"
#include "image/image_decoder.h"

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
    
    swrEvictTextureFromCache(swr, victim);
    return true;
}

static void makeRoomFor(SWRenderer* swr, size_t bytes)
{
    while (cachedBytes(swr) + bytes > TEXTURE_CACHE_BYTES) {
        if (!evictLeastRecentlyUsed(swr, false)) break;
    }
}

// Allocates, giving up cached pages (even ones in use this frame) rather than failing.
static void* allocOrEvict(SWRenderer* swr, size_t bytes)
{
    for (;;) {
        void* ptr = malloc(bytes);
        if (ptr) return ptr;
        if (!evictLeastRecentlyUsed(swr, true)) return NULL;
    }
}

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
    
    const char* source = "pack";
    texture = loadFromPack(swr, pageId);
    if (!texture) {
        source = "PNG";
        texture = loadFromDataWin(swr, pageId);
    }
    if (!texture) return false;
    
    texture->lastUsedFrame = swr->frameCounter;
    swr->textures[pageId] = texture;
    
    logInfo("SWR: Loaded TXTR page %u (%dx%d, %s), cache %u KB\n", pageId, texture->width, texture->height,
            source, (unsigned)(cachedBytes(swr) / 1024));
    return true;
}
