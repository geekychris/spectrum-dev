/*
 * l2wpoly.h - filled convex polygons and rectangles on 320 x 256
 * column-major pictures (Layer 2's 320 mode, nextgfx.h l2w_*, or a copy of
 * one kept in memory pages), drawn as vertical spans, clipped to a
 * rectangle, with an optional depth plane: each pixel drawn also gets a
 * depth byte (smaller = nearer), which nextsprl.h's spl_zmask() uses to put
 * hardware sprites behind what was drawn nearer than them.
 *
 * A plane is ten consecutive 8K pages: column x is in page first + x / 32,
 * pixel (x, y) at offset (x % 32) * 256 + y. Layer 2's plane starts at page
 * l2w_bank * 2. A depth plane has the same layout (only the pages of the
 * columns you draw in are touched).
 *
 *   l2wp_target(page, zpage)    draw into the plane at `page`; zpage != 0:
 *                               also write l2wp_key into the depth plane at
 *                               zpage
 *   l2wp_clip(x0, y0, x1, y1)   what is drawn: x0 <= x < x1, y0 <= y < y1
 *                               (default the whole 320 x 256)
 *   l2wp_key                    the depth byte written with each pixel
 *   l2wp_poly(xy, n, colour)    a convex polygon, n <= L2WP_MAXV vertices
 *                               (x, y int16 pairs, either winding). Column
 *                               x is drawn for xmin <= x < xmax, and in it
 *                               rows from the upper edge to below the lower
 *                               one (each edge evaluated at integer x)
 *   l2wp_rect(x0, y0, x1, y1, colour)
 *                               x0 <= x < x1, y0 <= y < y1
 *   l2wp_copy(dst, src, x0, y0, x1, y1)
 *                               a rectangle of plane src into plane dst
 *                               (e.g. a clean copy of the background back
 *                               into Layer 2), clipped to the screen
 *   l2wp_fill(page, x0, y0, x1, y1, v)
 *                               a rectangle of a plane set to v (e.g. a
 *                               depth plane cleared to 0xFF), clipped to
 *                               the screen
 *
 *   l2wp_slots(tslot, trestore, zslot, zrestore)
 *                               the MMU slots used as windows: the target
 *                               plane through slot tslot, the depth or
 *                               source plane through zslot; when done they
 *                               get pages trestore and zrestore back
 *                               (default: slot 3 / page 11, slot 0 / 0xFF,
 *                               the ROM). Vertex lists and the code calling
 *                               these must not be in either window, and
 *                               nothing that runs meanwhile (interrupts) may
 *                               need what the windows normally hold.
 *
 * Spans of 40 pixels or more, copies and fills go by the zxnDMA (dma.c),
 * shorter spans by LDIR.
 *
 * Test: projects/nextlibtest (l2wpoly checks).
 */
#ifndef L2WPOLY_H
#define L2WPOLY_H

#include <stdint.h>

#define L2WP_MAXV 8

extern uint8_t l2wp_key;
extern uint8_t l2wp_tslot, l2wp_trestore, l2wp_zslot, l2wp_zrestore;   /* l2wp_slots() */

extern void l2wp_slots(uint8_t tslot, uint8_t trestore, uint8_t zslot, uint8_t zrestore);
extern void l2wp_target(uint8_t page, uint8_t zpage);
extern void l2wp_clip(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
extern void l2wp_poly(const int16_t *xy, uint8_t n, uint8_t colour);
extern void l2wp_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t colour);
extern void l2wp_copy(uint8_t dst, uint8_t src, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
extern void l2wp_fill(uint8_t page, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v);

#endif
