/*
 * nextspr.h - big hardware sprites made of 16 x 16 patterns ("groups"),
 * streamed into pattern memory each frame, and hidden behind scenery.
 *
 * A group image is cols x rows patterns (8 bpp) stored pattern after
 * pattern, each 256 bytes row-major: pixel (x, y) is byte
 *   ((y / 16) * cols + x / 16) * 256 + (y % 16) * 16 + x % 16
 * (sdk/tools/nextpic.py group_patterns() writes them like that). Uploading one
 * is a single DMA transfer to port 0x5B.
 *
 *   sprg_upload(slot, img, n)     n patterns from img into pattern slots
 *                                 slot.. (DMA; img must be mapped)
 *   sprg_show(first, x, y, cols, rows, slot, flags)
 *                                 sprites first.. show the group with its
 *                                 top-left at (x, y) on the 320 x 256
 *                                 sprite plane; patterns slot.. in order.
 *                                 Returns the next free sprite number.
 *   sprg_show_ex(first, x, y, cols, rows, slot, flags, pal, zoom)
 *                                 the same with a palette offset (pal 0..15:
 *                                 adds 16 * pal to the patterns' indices)
 *                                 and hardware zoom (0..3: x1, x2, x4, x8,
 *                                 both axes): a group image shown 2, 4 or 8
 *                                 times its size (made at 1/2, 1/4, 1/8)
 *   sprg_hide(first, n)           hide sprites first..first+n-1 (one OTIR
 *                                 per 16)
 *   sprg_hide_from(n)             hide sprites n..127
 *   sprg_mask_rle(img, cols, rows, px, py, piece)
 *                                 make transparent every pixel of the group
 *                                 image that the column-RLE piece (l2rle.h)
 *                                 covers when its top-left is at (px, py)
 *                                 relative to the group's: the group is then
 *                                 behind that piece of the Layer 2 picture
 *   sprg_mask_rect(img, cols, rows, x0, y0, x1, y1)
 *                                 the same for a rectangle (exclusive x1, y1)
 *
 * The transparent colour is index 0xE3 (NextReg 0x4B, as next_init sets).
 * Sprite priority: a higher sprite number is drawn over a lower one.
 */
#ifndef NEXTSPR_H
#define NEXTSPR_H

#include <stdint.h>

#define SPRG_TRANSPARENT 0xE3

extern void sprg_upload(uint8_t slot, const uint8_t *img, uint8_t n);
extern uint8_t sprg_show(uint8_t first, int16_t x, int16_t y, uint8_t cols, uint8_t rows,
                         uint8_t slot, uint8_t flags);
extern uint8_t sprg_show_ex(uint8_t first, int16_t x, int16_t y, uint8_t cols, uint8_t rows,
                            uint8_t slot, uint8_t flags, uint8_t pal, uint8_t zoom);
extern void sprg_hide(uint8_t first, uint8_t n);
extern void sprg_hide_from(uint8_t n);
extern void sprg_mask_rle(uint8_t *img, uint8_t cols, uint8_t rows, int16_t px, int16_t py,
                          const uint8_t *piece);
extern void sprg_mask_rect(uint8_t *img, uint8_t cols, uint8_t rows, int16_t x0, int16_t y0,
                           int16_t x1, int16_t y1);

#endif
