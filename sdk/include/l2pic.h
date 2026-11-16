/*
 * l2pic.h - a picture wider than the screen, scrolled sideways on
 * Layer 2's 320 x 256 mode (nextgfx.h l2w_*). The picture is stored as
 * column RLE in banks (sdk/tools/l2pic.py packs it); Layer 2 holds the
 * 320 columns in view as a ring (picture column x in Layer 2 column
 * x mod 320), the columns that come into view are copied in as the view
 * moves, and the hardware X scroll shows the ring from the view's left
 * edge. Rows above `row0` are never touched (e.g. a status bar there,
 * shown unscrolled by a copper split: see RJ's Birthday Bash). For a
 * screen of several bands each scrolled on its own, with columns drawn by
 * a callback, see l2scroll.h.
 *
 *   l2pic_init(index_page, ncols, row0)
 *                       the index: 3 bytes a column (its 8K page, its
 *                       address with that page in MMU slot 2), in 8K page
 *                       index_page; the picture is ncols wide; rows
 *                       row0..255 are drawn. Call after l2w_init().
 *   l2pic_view(x)         make Layer 2 show columns x..x+319: copies the
 *                       columns it doesn't hold yet (all 320 after
 *                       l2pic_reset() or a jump of 320 or more); returns
 *                       the X scroll for it (x mod 320): give it to
 *                       l2pic_scroll(), or write it from the copper (NextReg
 *                       0x16 the low byte, 0x71 bit 0 bit 8)
 *   l2pic_scroll(s)       write the X scroll registers now
 *   l2pic_reset()         forget what Layer 2 holds (after drawing over it)
 *   l2pic_column(lx, x)   copy picture column x into Layer 2 column lx now
 *   l2pic_copied          columns copied so far (profiling)
 *
 * MMU slots 2 and 3 are used while copying and put back. A column is
 * runs of (count 1..255, colour) adding up to 256 - row0 rows, decoded in
 * Z80N assembly (24 T-states a pixel inside a run). Test: projects/l2pictest
 * (and sdk/tools/test_tools.py for the packer).
 */
#ifndef L2PIC_H
#define L2PIC_H
#include <stdint.h>

extern uint16_t l2pic_copied;
extern void l2pic_init(uint8_t index_page, uint16_t ncols, uint8_t row0);
extern uint16_t l2pic_view(int16_t x);
extern void l2pic_scroll(uint16_t s);
extern void l2pic_reset(void);
extern void l2pic_column(uint16_t lx, uint16_t x);
extern void l2pic_fill(uint8_t band, uint32_t wx, uint8_t *col);
/* the decoder: rows bytes of column RLE at src -> dst (one Layer 2 column, mapped) */
extern void l2pic_rle(uint8_t *dst, const uint8_t *src, uint8_t rows);

#endif
