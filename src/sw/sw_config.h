#ifndef __SW_CONFIG_H
#define __SW_CONFIG_H

// -======- USER CONFIG START -======-

// Change the bit-depth of the final image. Valid values are 8, 16, or 32.
// Can be overridden from the build system (-DPIXEL_SIZE=16).
#ifndef PIXEL_SIZE
#define PIXEL_SIZE 32
#endif
//#define PIXEL_SIZE 16
//#define PIXEL_SIZE 8

// Pixel formats for each mode:
// 32-bit: 0xAARRGGBB
// 16-bit: 0b0RRRRRGGGGGBBBBB
// 8-bit:  0bBBGGGRRR

// Define if you want to disable support for semi-transparent sprites.
// It should be a little bit faster in scenes with heavy use of semi-transparent pixels.
//#define SW_NO_SEMI_TRANSPARENT_TEXTURE_SUPPORT

// Define if you want dithered alpha blending and triangle color blending.
// It might be a good bit faster than doing blending the proper way.
//#define SW_DITHERED_BLENDING

// For dithered blending mode, if we want to use the slightly slower,
// but more random-looking RNG
//#define SW_USE_XORSHIFT32_FOR_DITHERING

// Define if you want tinting to be implemented inaccurately
//#define SW_INACCURATE_TINTING

// Define if you want to decrease the quality of additional blend modes and
// only fully support bm_normal.  Automatically set if you enable SW_DITHERED_BLENDING.
//#define SW_BAD_BLEND_MODE_SUPPORT

// Completely exclude bm_subtract support.
//#define SW_NO_SUBTRACT_SUPPORT

// How many bytes of decoded texture pages to keep in memory before the least
// recently used ones are unloaded. Pages needed by the current frame are kept
// even if that goes over this budget.
#ifndef TEXTURE_CACHE_BYTES
#define TEXTURE_CACHE_BYTES (256u * 1024u * 1024u)
#endif

// Amount of surfaces that can be created at the same time.
#define SURFACE_MAX_COUNT 64

// Whether to enable the debug font.
#define SW_ENABLE_DEBUG_FONT

// In debug mode, print a debug line every time a frame starts/ends.
//#define SW_DEBUG_FRAME_DRAW_BOUNDS

// -======- USER CONFIG END -======-

// Force enable dithered blending if in 8bpp mode.
#if PIXEL_SIZE == 8

#ifndef SW_DITHERED_BLENDING
#define SW_DITHERED_BLENDING
#endif

#ifndef SW_NO_SUBTRACT_SUPPORT
#define SW_NO_SUBTRACT_SUPPORT
#endif

#endif // PIXEL_SIZE == 8

#if PIXEL_SIZE != 32

// forcefully define this.  16-bit and 8-bit color modes CANNOT work with
// semi-transparent textures.
#ifndef SW_NO_SEMI_TRANSPARENT_TEXTURE_SUPPORT
#define SW_NO_SEMI_TRANSPARENT_TEXTURE_SUPPORT
#endif

#endif // PIXEL_SIZE != 32

#endif // __SW_CONFIG_H
