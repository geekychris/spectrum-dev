/*
 * sprclip.h - XOR a pre-shifted sprite (spr_preshift's layout, zxgame.h)
 * that may hang off either side of the 48K screen: x is signed, so an
 * object can slide off the left edge as well as the right one (zxgame.h's
 * spr_xor takes x 0..255 and clips only at the right).
 *
 *   sprc_xor(x, y, pre, w, h)   x from -8*(w+1)+1 to 255 (anything further
 *                               out draws nothing), y 0..191 (clipped at
 *                               the bottom); w bytes wide, h lines
 *
 * XOR it again at the same place to erase it. All assembly (sprclip.asm):
 * the line loop steps the screen address instead of looking each line up;
 * a 16 x 16 sprite (3 bytes a line) takes ~2300 T-states. Assembly callers
 * can use asm_sprc_xor: HL = x, A = y, DE = pre, B = w, C = h.
 */
#ifndef ZX_SPRCLIP_H
#define ZX_SPRCLIP_H
#include <stdint.h>
extern void sprc_xor(int16_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h);
#endif
