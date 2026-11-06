/*
 * l2rle.h - more for Layer 2's 320 x 256 mode (nextgfx.h's l2w_*): pictures
 * stored as column RLE ("pieces"), drawn through a colour table, and the
 * small things a picture-plus-sprites game does to it.
 *
 *   l2w_clear(c)          the whole screen to colour c (DMA)
 *   l2w_plot(x, y, c)
 *   l2w_span(x, y, len, and, or)
 *                         a row of pixels p = (p & and) | or: with a palette
 *                         whose upper half is the lower half darker, or 0x80
 *                         shades (a shadow) and and 0x7F takes it away
 *   l2w_blit_rle(x, y, piece, lut)
 *                         a piece through a colour table lut (256 bytes, or
 *                         0 = as is); columns off the screen are skipped, rows
 *                         must stay inside 0..255 (y + h <= 256)
 *
 * They draw through MMU slot l2w_slot and put page l2w_restore back after,
 * like the rest of l2w_*.
 *
 * Column RLE, as sdk/tools/nextpic.py piece() writes it:
 *
 *   u8 w, u8 h, then for each of the w columns: runs of
 *     u8 skip (transparent pixels before the run), u8 len, len pixels
 *   ended by a skip of 0xFF.
 *
 * Only the opaque runs are stored, so a piece is also its own silhouette
 * (nextspr.h's sprg_mask_rle hides a sprite behind it). Layer 2's 320 mode
 * is column-major, so a run is a straight copy down one column.
 *
 * l2w_blit_rle copies the table to 0xF200-0xF2FF (above frame_init's IM 2
 * table, below the stack).
 */
#ifndef L2RLE_H
#define L2RLE_H

#include <stdint.h>
#include "nextgfx.h"

extern void l2w_clear(uint8_t c);
extern void l2w_plot(int16_t x, uint8_t y, uint8_t c);
extern void l2w_span(int16_t x, uint8_t y, uint8_t len, uint8_t and_mask, uint8_t or_mask);
extern void l2w_blit_rle(int16_t x, int16_t y, const uint8_t *piece, const uint8_t *lut);

#endif
