#ifndef _SW_TEXTURE_PACK_H
#define _SW_TEXTURE_PACK_H

#include "sw_renderer_private.h"

// Optional pack of texture pages already converted to the renderer's 16-bit
// pixel format, so low-end targets can skip PNG decoding and the RGBA
// intermediate buffer that comes with it. Built offline from data.win.
//
// Each page is stored as a grid of square tiles that are compressed one by
// one, so that the part of a page a sprite occupies can be read without the
// rest: a 2048x2048 page is 8 MB decoded, and a battle needs a few monsters
// off it.
//
// File layout (all little-endian):
//   char     magic[4]   "UTX2"
//   uint32_t pageCount
//   uint32_t tileSize   side of a tile in pixels; tiles on the right and
//                       bottom edges of a page are cut to fit it
//   uint32_t tileCount  across all pages
//   pageCount x { uint16_t width, height; uint32_t firstTile; }
//                       a page's tiles are numbered row by row from firstTile;
//                       width 0 means the pack does not hold the page
//   tileCount x { uint32_t offset, size; }
//                       where the tile's data is in the file, in bytes;
//                       size 0 is a tile that is transparent throughout
//   tile data, run-length encoded as 16-bit words:
//     control word with bit 15 set:   repeat the next word (control & 0x7FFF) + 1 times
//     control word with bit 15 clear: copy the next (control + 1) words verbatim
#define SW_TEXTURE_PACK_MAGIC "UTX2"
#define SW_TEXTURE_PACK_RUN_FLAG 0x8000
#define SW_TEXTURE_PACK_MAX_COUNT 0x8000
#define SW_TEXTURE_PACK_TILE 128

#ifndef SW_TEXTURE_PACK_PATH
#define SW_TEXTURE_PACK_PATH "textures.bin"
#endif

// Returns false if there is no pack, or it has no entry for this page.
bool swrTexturePackGetSize(uint32_t pageId, int* outW, int* outH);

// Decodes a page into `buffer`, which must hold width * height pixels.
bool swrTexturePackDecode(uint32_t pageId, uintpixel_t* buffer);

// Decodes the w x h rectangle of a page whose top left corner is (x, y) into
// `buffer`. The rectangle must lie within the page.
bool swrTexturePackDecodeRect(uint32_t pageId, int x, int y, int w, int h, uintpixel_t* buffer);

// Why the last decode failed: "read" (with errno left as the C library set
// it) or "data" for a tile that does not decode. Empty after a success.
const char* swrTexturePackLastError(void);

#endif//_SW_TEXTURE_PACK_H
