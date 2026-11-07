/*
 * l2wrun.h - column-run images on Layer 2's 320 x 256 mode (nextgfx.h's
 * l2w_*): each pixel an offset added to a base pen when drawn, so one image
 * draws in any colour set laid out alike in the palette (pens base + 0,
 * base + 1, ...: a block's lit and shaded faces, for every colour of
 * block). Made by sdk/tools/runimg.py (encode, decode; the format is
 * described there), e.g. 3D shapes drawn on the host for every view a game
 * needs (projects/stack_tower): Layer 2's 320 mode is column-major, so a
 * run is a straight copy down a column.
 *
 *   l2wr_blit(img, x, mirror, base)
 *                           the image's column c at x + c (its x0
 *                           included), or with mirror != 0 at x - c: a
 *                           shape and its mirror image from one picture.
 *                           Columns must be on the screen (0..319)
 *
 * Draws through MMU slot l2w_slot into l2w_bank and puts page l2w_restore
 * back (as the rest of l2w_*); the image itself must be mapped elsewhere.
 * About 28 T-states a pixel and ~120 a run (an unrolled loop entered at
 * its length); the 8K page only changes every 32 columns.
 */
#ifndef L2WRUN_H
#define L2WRUN_H

#include <stdint.h>
#include "nextgfx.h"

extern void l2wr_blit(const uint8_t *img, int16_t x, uint8_t mirror, uint8_t base);

#endif
