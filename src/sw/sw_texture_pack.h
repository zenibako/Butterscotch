#ifndef _SW_TEXTURE_PACK_H
#define _SW_TEXTURE_PACK_H

#include "sw_renderer_private.h"

// Optional pack of texture pages already converted to the renderer's 16-bit
// pixel format, so low-end targets can skip PNG decoding and the RGBA
// intermediate buffer that comes with it. Built offline from data.win.
//
// File layout (all little-endian):
//   char     magic[4]   "UTX1"
//   uint32_t pageCount
//   pageCount x { uint16_t width, height; uint32_t offset, size; }
//   page data, run-length encoded as 16-bit words:
//     control word with bit 15 set:   repeat the next word (control & 0x7FFF) + 1 times
//     control word with bit 15 clear: copy the next (control + 1) words verbatim
#define SW_TEXTURE_PACK_MAGIC "UTX1"
#define SW_TEXTURE_PACK_RUN_FLAG 0x8000
#define SW_TEXTURE_PACK_MAX_COUNT 0x8000

#ifndef SW_TEXTURE_PACK_PATH
#define SW_TEXTURE_PACK_PATH "textures.bin"
#endif

// Returns false if there is no pack, or it has no entry for this page.
bool swrTexturePackGetSize(uint32_t pageId, int* outW, int* outH);

// Decodes a page into `buffer`, which must hold width * height pixels.
bool swrTexturePackDecode(uint32_t pageId, uintpixel_t* buffer);

#endif//_SW_TEXTURE_PACK_H
