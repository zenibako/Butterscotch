/*
 * mktexpack — build a pre-converted texture pack from a GameMaker data.win.
 *
 *   mktexpack data.win textures.bin
 *
 * Decodes every TXTR page with Butterscotch's own image decoder, converts it
 * with the software renderer's own 16-bit pixel conversion (so the result is
 * exactly what the renderer would have produced on the device), cuts it
 * into tiles, run-length encodes each tile and writes the pack format
 * described in sw_texture_pack.h.
 *
 * Host tool only; assumes a little-endian machine.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_decoder.h"
#include "sw_texture_pack.h"

typedef struct {
    uint16_t width, height;
    uint32_t firstTile;
} PackPage;

typedef struct {
    uint32_t offset, size;
} PackTile;

#define TILE SW_TEXTURE_PACK_TILE

static uint32_t readU32(const uint8_t *p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint8_t *readFile(const char *path, size_t *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t) size);
    if (!data || fread(data, 1, (size_t) size, f) != (size_t) size) {
        fprintf(stderr, "%s: read failed\n", path);
        exit(1);
    }
    fclose(f);
    *outSize = (size_t) size;
    return data;
}

/* Finds a chunk in the FORM container; returns its payload and length. */
static const uint8_t *findChunk(const uint8_t *data, size_t size, const char *name, uint32_t *outLen) {
    size_t pos = 8;
    while (pos + 8 <= size) {
        uint32_t len = readU32(data + pos + 4);
        if (memcmp(data + pos, name, 4) == 0) {
            *outLen = len;
            return data + pos + 8;
        }
        pos += 8 + (size_t) len;
    }
    return NULL;
}

/* Run-length encodes `count` pixels into `out` (worst case count + count/32768 + 1 words). */
static size_t encodeTile(const uint16_t *pixels, size_t count, uint16_t *out) {
    size_t n = 0;
    size_t i = 0;
    while (i < count) {
        size_t run = 1;
        while (i + run < count && pixels[i + run] == pixels[i] && run < SW_TEXTURE_PACK_MAX_COUNT) run++;

        if (run >= 3) {
            out[n++] = (uint16_t) (SW_TEXTURE_PACK_RUN_FLAG | (run - 1));
            out[n++] = pixels[i];
            i += run;
            continue;
        }

        /* Literal span: extend until the next run of three or more. */
        size_t start = i;
        while (i < count && i - start < SW_TEXTURE_PACK_MAX_COUNT) {
            if (i + 2 < count && pixels[i] == pixels[i + 1] && pixels[i] == pixels[i + 2]) break;
            i++;
        }
        out[n++] = (uint16_t) (i - start - 1);
        memcpy(out + n, pixels + start, (i - start) * sizeof(uint16_t));
        n += i - start;
    }
    return n;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <data.win> <textures.bin>\n", argv[0]);
        return 2;
    }

    size_t size;
    uint8_t *data = readFile(argv[1], &size);
    if (size < 16 || memcmp(data, "FORM", 4) != 0) {
        fprintf(stderr, "%s: not a GameMaker data file\n", argv[1]);
        return 1;
    }

    uint32_t txtrLen;
    const uint8_t *txtr = findChunk(data, size, "TXTR", &txtrLen);
    if (!txtr) {
        fprintf(stderr, "%s: no TXTR chunk\n", argv[1]);
        return 1;
    }
    const uint8_t *txtrEnd = txtr + txtrLen;

    /* Count, then pointers to entries whose last field is the blob offset. An entry is
     * { uint32 scaled; uint32 blobOffset; } in WAD 16 and grows in later GameMaker versions (up to 28 bytes in
     * 2022.9+), so its size is taken from the spacing of the first two. */
    uint32_t count = readU32(txtr);
    uint32_t entrySize = count >= 2 ? readU32(txtr + 8) - readU32(txtr + 4) : 8;
    if (entrySize < 8 || entrySize > 64) {
        fprintf(stderr, "%s: unexpected TXTR entry size %u\n", argv[1], (unsigned) entrySize);
        return 1;
    }
    uint32_t *blobOffsets = calloc(count, sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) {
        uint32_t entry = readU32(txtr + 4 + 4 * i);
        blobOffsets[i] = readU32(data + entry + entrySize - 4);
    }

    /* First pass: decode and convert every page, and count the tiles. */
    PackPage *pages = calloc(count, sizeof(PackPage));
    uint16_t **pagePixels = calloc(count, sizeof(uint16_t *));
    uint32_t tileCount = 0;
    size_t rawTotal = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (blobOffsets[i] == 0) continue;

        /* A blob runs to the next blob, or to the end of the chunk. */
        const uint8_t *blob = data + blobOffsets[i];
        const uint8_t *blobEnd = txtrEnd;
        for (uint32_t j = 0; j < count; j++) {
            const uint8_t *other = data + blobOffsets[j];
            if (blobOffsets[j] != 0 && other > blob && other < blobEnd) blobEnd = other;
        }

        int w, h;
        /* Compressed pages written by GameMaker 2022.5+ carry one more header field. The entry size says the
         * file is that new from 28 bytes up; a 16-byte entry could be either, so both are tried. */
        bool newer = entrySize >= 16;
        uint8_t *rgba = ImageDecoder_decodeToRgba(blob, (size_t) (blobEnd - blob), newer, &w, &h);
        if (rgba == NULL && entrySize == 16)
            rgba = ImageDecoder_decodeToRgba(blob, (size_t) (blobEnd - blob), !newer, &w, &h);
        if (!rgba) {
            fprintf(stderr, "page %u: decode failed\n", i);
            return 1;
        }
        if (w > 0xFFFF || h > 0xFFFF) {
            fprintf(stderr, "page %u: %dx%d is too large\n", i, w, h);
            return 1;
        }

        size_t pixelCount = (size_t) w * (size_t) h;
        uint16_t *pixels = malloc(pixelCount * sizeof(uint16_t));
        const uint32_t *src = (const uint32_t *) rgba;
        for (size_t p = 0; p < pixelCount; p++) pixels[p] = swrConvertPixelTexture(src[p]);
        free(rgba);

        pages[i].width = (uint16_t) w;
        pages[i].height = (uint16_t) h;
        pages[i].firstTile = tileCount;
        pagePixels[i] = pixels;
        tileCount += (uint32_t) (((w + TILE - 1) / TILE) * ((h + TILE - 1) / TILE));
        rawTotal += pixelCount * sizeof(uint16_t);
    }

    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }

    /* Second pass: write the tiles after the space the two tables will take. */
    PackTile *tiles = calloc(tileCount, sizeof(PackTile));
    uint32_t dataStart = 16 + count * (uint32_t) sizeof(PackPage) + tileCount * (uint32_t) sizeof(PackTile);
    fseek(out, (long) dataStart, SEEK_SET);

    uint32_t offset = dataStart;
    uint32_t emptyTiles = 0;
    static uint16_t tilePixels[TILE * TILE];
    static uint16_t encoded[TILE * TILE + 16];
    for (uint32_t i = 0; i < count; i++) {
        if (pagePixels[i] == NULL) continue;
        int w = pages[i].width, h = pages[i].height;
        uint32_t tile = pages[i].firstTile;
        for (int tileY = 0; tileY < h; tileY += TILE) {
            for (int tileX = 0; tileX < w; tileX += TILE, tile++) {
                int tw = w - tileX < TILE ? w - tileX : TILE;
                int th = h - tileY < TILE ? h - tileY : TILE;
                bool empty = true;
                for (int row = 0; row < th; row++) {
                    const uint16_t *line = &pagePixels[i][(size_t) (tileY + row) * (size_t) w + (size_t) tileX];
                    memcpy(&tilePixels[row * tw], line, (size_t) tw * sizeof(uint16_t));
                    for (int col = 0; col < tw && empty; col++) empty = line[col] == 0;
                }
                if (empty) {
                    emptyTiles++;
                    continue;
                }
                size_t words = encodeTile(tilePixels, (size_t) tw * (size_t) th, encoded);
                fwrite(encoded, sizeof(uint16_t), words, out);
                tiles[tile].offset = offset;
                tiles[tile].size = (uint32_t) (words * sizeof(uint16_t));
                offset += tiles[tile].size;
            }
        }
        free(pagePixels[i]);
    }

    uint32_t header[3] = { count, TILE, tileCount };
    fseek(out, 0, SEEK_SET);
    fwrite(SW_TEXTURE_PACK_MAGIC, 1, 4, out);
    fwrite(header, sizeof(uint32_t), 3, out);
    fwrite(pages, sizeof(PackPage), count, out);
    fwrite(tiles, sizeof(PackTile), tileCount, out);
    fclose(out);

    printf("%s: %u pages in %u tiles (%u empty), %.1f MB raw -> %.1f MB\n", argv[2], (unsigned) count,
           (unsigned) tileCount, (unsigned) emptyTiles, (double) rawTotal / 1048576.0, (double) offset / 1048576.0);
    return 0;
}
