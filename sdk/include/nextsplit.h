/*
 * nextsplit.h - split screens with the copper: the display cut into
 * horizontal panes (e.g. one per player), each with its own clip windows,
 * so a sprite or a piece of Layer 2 drawn for one pane never spills into
 * the next; plus row-exact register changes inside a pane.
 *
 * Rows are the 320 x 256 display's (the sprites' and Layer 2 320 mode's
 * coordinates; 0..31 is the top border). Builds on nextcopper.h: a list
 * is split_begin() .. split_end(), with copper_move() and the calls here
 * in raster order (top to bottom).
 *
 *   split_begin()                 start a new list (the running one keeps
 *                                 running until split_end() replaces it:
 *                                 build during the frame, upload in the
 *                                 vertical blank)
 *   split_pane(y0, y1, layers)    from row y0 on, clip the given layers to
 *                                 rows y0..y1, the full width; set at the
 *                                 copper's restart (the vertical blank) for
 *                                 a pane at the top (y0 = 0), else on the
 *                                 row above y0. layers: SPLIT_SPRITES,
 *                                 SPLIT_L2, SPLIT_TILES (Layer 2 clips in
 *                                 its 320 mode: x in pixel pairs; sprites
 *                                 in "over the border" mode with clipping,
 *                                 NextReg 0x15 bit 5)
 *   split_row(y)                  wait for the horizontal blank before row
 *                                 y: a copper_move() after it lands before
 *                                 the row's first pixel (e.g. NextReg 0x43
 *                                 to switch a row's palette). ZEsarUX draws
 *                                 a line after the line's copper moves, so
 *                                 there the change shows from row y - 1
 *   split_end()                   upload and run (copper_end)
 *
 * The CPU and a list that writes NextReg 0x43 share it (palette loads):
 * load palettes while the list isn't writing it (between its last 0x43
 * move and the first one of the next frame).
 */
#ifndef NEXTSPLIT_H
#define NEXTSPLIT_H
#include <stdint.h>
#include "nextcopper.h"

#define SPLIT_SPRITES 0x02
#define SPLIT_L2      0x01
#define SPLIT_TILES   0x08

extern void split_begin(void);
extern void split_pane(uint8_t y0, uint8_t y1, uint8_t layers);
extern void split_row(uint8_t y);
#define split_end() copper_end()
#endif
