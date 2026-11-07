/*
 * l2wseg.h - column-segment images on Layer 2's 320 x 256 mode (nextgfx.h's
 * l2w_*): pictures of a few flat colours, each pixel a code 1..3 drawn
 * through the pen l2ws_pen[code] (0 = transparent), so one image draws in
 * any colours. Made by sdk/tools/segimg.py (encode, decode; the format is
 * described there). For shapes drawn on the host for every view a game
 * needs, e.g. the faces of 3D blocks (projects/stack_tower): flat colours
 * down a column are a few runs, and Layer 2's 320 mode is column-major.
 *
 *   l2ws_pen[1..3]          the pens codes 1..3 draw with
 *   l2ws_blit(img, x, mirror)
 *                           the image's column c at x + c (its x0 included),
 *                           or with mirror != 0 at x - c: a shape and its
 *                           mirror image from one picture. Columns must be
 *                           on the screen (0..319), rows inside 0..255
 *
 * Draws through MMU slot l2w_slot into l2w_bank, puts page l2w_restore back
 * (as the rest of l2w_*); the image itself must be mapped elsewhere.
 * About 24 T-states a pixel plus ~90 a run.
 */
#ifndef L2WSEG_H
#define L2WSEG_H

#include <stdint.h>
#include "nextgfx.h"

extern uint8_t l2ws_pen[4];
extern void l2ws_blit(const uint8_t *img, int16_t x, uint8_t mirror);

#endif
