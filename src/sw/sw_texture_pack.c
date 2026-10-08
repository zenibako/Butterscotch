#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sw_texture_pack.h"

#if PIXEL_SIZE == 16 && !defined IS_BIG_ENDIAN

typedef struct
{
    uint16_t width, height;
    uint32_t firstTile;
}
PackPage;

typedef struct
{
    uint32_t offset, size;
}
PackTile;

#define TILE SW_TEXTURE_PACK_TILE
// A tile that does not compress at all is its pixels plus one control word.
#define TILE_MAX_WORDS (TILE * TILE + 16)

static FILE* packFile = NULL;
static PackPage* packPages = NULL;
static PackTile* packTiles = NULL;
static uint32_t packCount = 0;
static uint32_t packTileCount = 0;
static bool packProbed = false;

static void packOpen(void)
{
    packProbed = true;
    
    FILE* file = fopen(SW_TEXTURE_PACK_PATH, "rb");
    if (!file) {
        logInfo("SWR: No texture pack (%s), decoding PNG pages instead.\n", SW_TEXTURE_PACK_PATH);
        return;
    }
    
    char magic[4];
    uint32_t header[3] = { 0, 0, 0 }; // pageCount, tileSize, tileCount
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, SW_TEXTURE_PACK_MAGIC, 4) != 0 ||
        fread(header, sizeof(uint32_t), 3, file) != 3 || header[0] == 0 || header[0] > 4096 ||
        header[1] != TILE || header[2] == 0 || header[2] > 1024 * 1024) {
        logWarn("SWR: %s is not a texture pack this build can read (rebuild it), ignoring it.\n", SW_TEXTURE_PACK_PATH);
        fclose(file);
        return;
    }
    
    PackPage* pages = (PackPage*) malloc(header[0] * sizeof(PackPage));
    PackTile* tiles = (PackTile*) malloc(header[2] * sizeof(PackTile));
    if (!pages || !tiles || fread(pages, sizeof(PackPage), header[0], file) != header[0] ||
        fread(tiles, sizeof(PackTile), header[2], file) != header[2]) {
        logWarn("SWR: Texture pack index is truncated, ignoring the pack.\n");
        free(pages);
        free(tiles);
        fclose(file);
        return;
    }
    
    setvbuf(file, NULL, _IONBF, 0);
    packFile = file;
    packPages = pages;
    packTiles = tiles;
    packCount = header[0];
    packTileCount = header[2];
    logInfo("SWR: Texture pack with %u pages in %u tiles.\n", (unsigned) packCount, (unsigned) packTileCount);
}

bool swrTexturePackGetSize(uint32_t pageId, int* outW, int* outH)
{
    if (!packProbed) packOpen();
    if (pageId >= packCount) return false;
    
    const PackPage* page = &packPages[pageId];
    if (page->width == 0 || page->height == 0) return false;
    
    *outW = page->width;
    *outH = page->height;
    return true;
}

#ifdef PLATFORM_BUSY_TICK
// Called between the pieces of a long blocking read, so the platform can keep
// time-critical work going (feeding its audio queue, for one).
void platformBusyTick(void);
#endif

// Decodes one tile of tw x th pixels into `out`, row after row with no gaps.
static bool decodeTile(const PackTile* tile, int tw, int th, uint16_t* out)
{
    size_t pixels = (size_t) tw * th;
    if (tile->size == 0) {
        memset(out, 0, pixels * sizeof(uint16_t));
        return true;
    }
    
    static uint16_t words[TILE_MAX_WORDS];
    size_t count = tile->size / sizeof(uint16_t);
    if (count > TILE_MAX_WORDS) return false;
    if (fseek(packFile, (long) tile->offset, SEEK_SET) != 0) return false;
    if (fread(words, sizeof(uint16_t), count, packFile) != count) return false;
#ifdef PLATFORM_BUSY_TICK
    platformBusyTick();
#endif
    
    uint16_t* end = out + pixels;
    size_t i = 0;
    while (i < count)
    {
        uint16_t control = words[i++];
        if (control & SW_TEXTURE_PACK_RUN_FLAG) {
            size_t run = (size_t) (control & 0x7FFF) + 1;
            if (i >= count || out + run > end) return false;
            uint16_t word = words[i++];
            for (size_t n = 0; n < run; n++) out[n] = word;
            out += run;
        } else {
            size_t literals = (size_t) control + 1;
            if (i + literals > count || out + literals > end) return false;
            memcpy(out, &words[i], literals * sizeof(uint16_t));
            out += literals;
            i += literals;
        }
    }
    return out == end;
}

bool swrTexturePackDecodeRect(uint32_t pageId, int x, int y, int w, int h, uintpixel_t* buffer)
{
    int pageW, pageH;
    if (!swrTexturePackGetSize(pageId, &pageW, &pageH)) return false;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > pageW || y + h > pageH) return false;
    
    const PackPage* page = &packPages[pageId];
    int columns = (pageW + TILE - 1) / TILE;
    static uint16_t pixels[TILE * TILE];
    
    for (int ty = y / TILE; ty <= (y + h - 1) / TILE; ty++)
    {
        for (int tx = x / TILE; tx <= (x + w - 1) / TILE; tx++)
        {
            uint32_t index = page->firstTile + (uint32_t) (ty * columns + tx);
            if (index >= packTileCount) return false;
            
            int tileX = tx * TILE, tileY = ty * TILE;
            int tw = pageW - tileX < TILE ? pageW - tileX : TILE;
            int th = pageH - tileY < TILE ? pageH - tileY : TILE;
            
            // The part of this tile that falls inside the rectangle.
            int left = x > tileX ? x : tileX;
            int top = y > tileY ? y : tileY;
            int right = x + w < tileX + tw ? x + w : tileX + tw;
            int bottom = y + h < tileY + th ? y + h : tileY + th;
            size_t rowBytes = (size_t) (right - left) * sizeof(uintpixel_t);
            
            if (packTiles[index].size == 0) {
                for (int row = top; row < bottom; row++)
                    memset(&buffer[(size_t) (row - y) * w + (left - x)], 0, rowBytes);
                continue;
            }
            if (!decodeTile(&packTiles[index], tw, th, pixels)) return false;
            for (int row = top; row < bottom; row++)
                memcpy(&buffer[(size_t) (row - y) * w + (left - x)], &pixels[(row - tileY) * tw + (left - tileX)], rowBytes);
        }
    }
    return true;
}

bool swrTexturePackDecode(uint32_t pageId, uintpixel_t* buffer)
{
    int pageW, pageH;
    if (!swrTexturePackGetSize(pageId, &pageW, &pageH)) return false;
    return swrTexturePackDecodeRect(pageId, 0, 0, pageW, pageH, buffer);
}

#else

bool swrTexturePackGetSize(UNUSED uint32_t pageId, UNUSED int* outW, UNUSED int* outH) { return false; }
bool swrTexturePackDecode(UNUSED uint32_t pageId, UNUSED uintpixel_t* buffer) { return false; }
bool swrTexturePackDecodeRect(UNUSED uint32_t pageId, UNUSED int x, UNUSED int y, UNUSED int w, UNUSED int h, UNUSED uintpixel_t* buffer) { return false; }

#endif
