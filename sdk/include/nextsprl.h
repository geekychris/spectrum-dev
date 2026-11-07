/*
 * nextsprl.h - a frame's hardware sprites as one list of 16 x 16 sprites
 * with 4-bit patterns (128 of them, 128 bytes each), each with its own
 * palette offset; sent to the sprite hardware at once (DMA) at the end of
 * the frame's drawing. And depth masking against l2wpoly.h's depth plane,
 * so a sprite can pass behind what was drawn into Layer 2 nearer than it.
 *
 *   spl_init()                  4-bit transparency: pen 0 (NextReg 0x4B = 0;
 *                               8-bit sprites then use index 0 too), all
 *                               sprites hidden
 *   spl_begin()                 start the frame's list (sprite spl_first
 *                               first; spl_first, default 0, leaves the
 *                               sprites below it to other code)
 *   spl_add(x, y, pat, pal, flags)
 *                               one sprite with its top-left at (x, y) on the
 *                               320 x 256 sprite plane: 4-bit pattern pat
 *                               (0..127), palette offset pal (0..15: pens are
 *                               16 * pal + pixel), flags SPR_MIRROR_X /
 *                               SPR_MIRROR_Y / SPR_ROTATE (nextgfx.h). Off
 *                               screen: skipped. Later sprites are drawn over
 *                               earlier ones. Returns 0 if the list is full.
 *   spl_end()                   send the list; sprites the last list showed
 *                               and this one doesn't are hidden
 *   spl_count                   sprites in the list so far
 *   spl_patterns(pat, src, n)   n 4-bit patterns from src (128 bytes each,
 *                               row-major, two pixels a byte, the left one in
 *                               the high nibble) into slots pat.. (DMA)
 *   spl_zmask(dst, src, x, y, key, zpage)
 *                               dst = pattern src (128 bytes) with every pixel
 *                               hidden whose screen pixel, for a sprite at
 *                               (x, y), holds a depth below key in the depth
 *                               plane at zpage (l2wpoly.h); returns how many
 *                               pixels are left (0: wholly hidden). The plane
 *                               is mapped through l2wpoly's depth window
 *                               (l2wp_slots: slot l2wp_zslot, which then gets
 *                               page l2wp_zrestore back); src and dst must not
 *                               be in that window.
 *
 * Test: projects/nextlibtest (nextsprl checks).
 */
#ifndef NEXTSPRL_H
#define NEXTSPRL_H

#include <stdint.h>

#define SPL_MAX 128

extern uint8_t spl_count, spl_first;

extern void spl_init(void);
extern void spl_begin(void);
extern uint8_t spl_add(int16_t x, int16_t y, uint8_t pat, uint8_t pal, uint8_t flags);
extern void spl_end(void);
extern void spl_patterns(uint8_t pat, const uint8_t *src, uint8_t n);
extern uint16_t spl_zmask(uint8_t *dst, const uint8_t *src, int16_t x, int16_t y, uint8_t key, uint8_t zpage);

#endif
