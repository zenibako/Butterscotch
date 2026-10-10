/* Section ordering for an experiment (make hot): the functions a sprite draw
 * goes through, side by side at the start of the program's code, so that
 * they cannot push one another out of the instruction cache.
 * Used with ld --section-ordering-file. */
.text : {
    *(.text.start)
    *crt1.o(.text .text.*)
    *(.text.builtin_draw_sprite)
    *(.text.builtin_draw_sprite_ext)
    *(.text.SWRenderer_profDrawSprite)
    *(.text.SWRenderer_drawSprite)
    *(.text.swrDrawSprite)
    *(.text.swrDrawSpriteInternal)
    *(.text.swrTextureForItem)
    *(.text.swrTexturePackGetSize)
    *(.text.swrOverlayFlush)
    *(.text.swrClearSettle)
    *(.text.swrTileRunsFlush)
    *(.text.swrGridFlush)
    *(.text.swrMirrorHold)
    *(.text.swrOverlayHold)
    *(.text.platformDrawCount)
    *(.text.platformBusyTick)
    *(.text.swrSpriteCostProbe)
    *(.text.memcpy)
}
