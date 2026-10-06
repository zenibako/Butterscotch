#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sw_texture_pack.h"

#if PIXEL_SIZE == 16 && !defined IS_BIG_ENDIAN

typedef struct
{
    uint16_t width, height;
    uint32_t offset, size;
}
PackEntry;

#define PACK_READ_WORDS 8192

static FILE* packFile = NULL;
static PackEntry* packEntries = NULL;
static uint32_t packCount = 0;
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
    uint32_t count = 0;
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, SW_TEXTURE_PACK_MAGIC, 4) != 0 ||
        fread(&count, sizeof(count), 1, file) != 1 || count == 0 || count > 4096) {
        logWarn("SWR: %s is not a texture pack, ignoring it.\n", SW_TEXTURE_PACK_PATH);
        fclose(file);
        return;
    }
    
    PackEntry* entries = (PackEntry*) malloc(count * sizeof(PackEntry));
    if (!entries || fread(entries, sizeof(PackEntry), count, file) != count) {
        logWarn("SWR: Texture pack index is truncated, ignoring the pack.\n");
        free(entries);
        fclose(file);
        return;
    }
    
    packFile = file;
    packEntries = entries;
    packCount = count;
    logInfo("SWR: Texture pack with %u pages.\n", (unsigned) count);
}

bool swrTexturePackGetSize(uint32_t pageId, int* outW, int* outH)
{
    if (!packProbed) packOpen();
    if (pageId >= packCount) return false;
    
    const PackEntry* entry = &packEntries[pageId];
    if (entry->size == 0 || entry->width == 0 || entry->height == 0) return false;
    
    *outW = entry->width;
    *outH = entry->height;
    return true;
}

bool swrTexturePackDecode(uint32_t pageId, uintpixel_t* buffer)
{
    if (pageId >= packCount) return false;
    
    const PackEntry* entry = &packEntries[pageId];
    if (fseek(packFile, (long) entry->offset, SEEK_SET) != 0) return false;
    
    static uint16_t words[PACK_READ_WORDS];
    size_t wordsLeft = entry->size / sizeof(uint16_t);
    uintpixel_t* out = buffer;
    uintpixel_t* end = buffer + (size_t) entry->width * entry->height;
    
    // Decoder state carried across read chunks.
    uint32_t literalsLeft = 0;
    uint32_t pendingRun = 0;
    
    while (wordsLeft > 0)
    {
        size_t want = wordsLeft < PACK_READ_WORDS ? wordsLeft : PACK_READ_WORDS;
        if (fread(words, sizeof(uint16_t), want, packFile) != want) return false;
        wordsLeft -= want;
        
        for (size_t i = 0; i < want; i++)
        {
            uint16_t word = words[i];
            if (pendingRun > 0) {
                if (out + pendingRun > end) return false;
                for (uint32_t n = 0; n < pendingRun; n++) out[n] = word;
                out += pendingRun;
                pendingRun = 0;
            } else if (literalsLeft > 0) {
                if (out >= end) return false;
                *out++ = word;
                literalsLeft--;
            } else if (word & SW_TEXTURE_PACK_RUN_FLAG) {
                pendingRun = (uint32_t)(word & 0x7FFF) + 1;
            } else {
                literalsLeft = (uint32_t) word + 1;
            }
        }
    }
    
    return out == end;
}

#else

bool swrTexturePackGetSize(UNUSED uint32_t pageId, UNUSED int* outW, UNUSED int* outH) { return false; }
bool swrTexturePackDecode(UNUSED uint32_t pageId, UNUSED uintpixel_t* buffer) { return false; }

#endif
