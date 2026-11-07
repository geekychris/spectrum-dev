/*
 * palfx.h - loading whole Next palettes from 9-bit colours, with an effect
 * applied on the way: darken, brighten towards a colour (flashes, fades).
 *
 * Colours are 9-bit RRRGGGBBB (uint16_t, 0..511). Palettes are chosen by
 * NextReg 0x43's selector (bits 6-4):
 *
 *   PAL_ULA 0x00, PAL_L2 0x10, PAL_SPR 0x20, PAL_TILE 0x30, and +0x40 for
 *   the second palette of each (PAL_L2_2 0x50, PAL_SPR_2 0x60 ...)
 *
 *   palfx_load(sel, first, n, src)            as they are
 *   palfx_load_fx(sel, first, n, src, mul, add)
 *                                           each component c becomes
 *                                           min(7, c * mul / 8 + add's):
 *                                           mul 8 add 0 = as is, mul 4 = half
 *                                           brightness, mul 7 add ink/3 = a
 *                                           flash towards an ink
 *   palfx_load_bytes(sel, first, src, nbytes)
 *                                           raw NextReg 0x44 bytes (two an
 *                                           entry: RRRGGGBB, then B's low
 *                                           bit) sent by DMA: ~2 T-states a
 *                                           byte, for palettes rewritten
 *                                           every frame (colour cycling)
 *   palfx_active(l2_second, spr_second)       which palettes are shown (keeps
 *                                           the selector bits it doesn't own)
 *
 * The selector's "shown" bits (NextReg 0x43 bits 3-1) are kept as they are
 * while loading. A copper list that switches them (see nextcopper.h) and the
 * CPU loading a palette both write NextReg 0x43/0x40: load palettes while
 * the copper isn't writing (e.g. in the vertical blank).
 */
#ifndef PALFX_H
#define PALFX_H

#include <stdint.h>

#define PAL_ULA  0x00
#define PAL_L2   0x10
#define PAL_SPR  0x20
#define PAL_TILE 0x30
#define PAL_SECOND 0x40
#define RGB9(r, g, b) ((uint16_t)(((r) & 7) << 6 | ((g) & 7) << 3 | ((b) & 7)))

extern void palfx_load(uint8_t sel, uint8_t first, uint16_t n, const uint16_t *src);
extern void palfx_load_fx(uint8_t sel, uint8_t first, uint16_t n, const uint16_t *src,
                        uint8_t mul, uint16_t add);
extern void palfx_load_bytes(uint8_t sel, uint8_t first, const uint8_t *src, uint16_t nbytes);
extern uint16_t pal_fx(uint16_t c, uint8_t mul, uint16_t add);
extern void palfx_active(uint8_t l2_second, uint8_t spr_second);

#endif
