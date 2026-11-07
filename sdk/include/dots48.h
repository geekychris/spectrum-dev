/*
 * dots48.h - point lists XORed onto the 48K screen (dots48.asm, ~90 T-states
 * a point): particles, stars, sparks. XOR the same list again to erase it, so
 * a frame is: dots_xor(old) ... dots_xor(new). Points are (x, y) byte pairs;
 * y must be 0..191 (clip before adding). Uses zx_row (gfx_init()).
 */
#ifndef ZX_DOTS48_H
#define ZX_DOTS48_H
#include <stdint.h>
extern void dots_xor(const uint8_t *xy, uint8_t n);
#endif
