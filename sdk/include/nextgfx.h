/*
 * nextgfx.h - Spectrum Next graphics for the game layer (TARGET := zxn).
 *
 *   next_init()        28 MHz, sprites + Layer 2 on, layer order S L U
 *   Layer 2            256x192, 8 bpp (RRRGGGBB, through the Layer 2 palette),
 *                      double buffered: draw into the back buffer, l2_flip()
 *                      shows it (banks 9-11 and 12-14). Drawing maps 8K pages
 *                      into MMU slot 3 (0x6000-0x7FFF) and restores it, so keep
 *                      nothing you need there while drawing.
 *   sprites            128 hardware sprites, 16x16, 8 bpp patterns (64 slots),
 *                      coordinates on the 320x256 sprite plane (paper at 32,32)
 *   palettes           9-bit (RGB333) or 8-bit entries for L2 / sprites / ULA
 *   TurboSound         ay_chip(n) selects AY 0..2 for the AY calls in zxgame.h
 */
#ifndef NEXTGFX_H
#define NEXTGFX_H

#include <stdint.h>

#define L2_W 256
#define L2_H 192
#define L2_TRANSPARENT 0xE3              /* global transparency colour */
#define RGB8(r, g, b) ((uint8_t)(((r) & 7) << 5 | ((g) & 7) << 2 | ((b) & 3)))

extern void next_init(void);
extern void next_reg(uint8_t reg, uint8_t val);
extern uint8_t next_reg_read(uint8_t reg);

extern uint8_t l2_back;                  /* 16K bank being drawn into */
extern void l2_init(void);               /* enable, both buffers cleared to 0 */
extern void l2_flip(void);               /* show the back buffer (call after frame_wait) */
extern void l2_copy_front_to_back(void); /* start the next frame from the shown one */
extern void l2_clear(uint8_t colour);    /* back buffer */
extern void l2_plot(uint8_t x, uint8_t y, uint8_t colour);
extern void l2_fill(uint8_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t colour);
extern void l2_hline(uint8_t x, uint8_t y, uint16_t w, uint8_t colour);
extern void l2_rect(uint8_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t colour);
/* w x h bytes from src; pixels == key are skipped (key 0xE3 = transparent; pass
   0x100 to copy everything) */
extern void l2_blit(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *src, uint16_t key);
/* text with the ROM font, `scale` 1..3, colour on transparent */
extern void l2_text(uint8_t x, uint8_t y, const char *s, uint8_t colour, uint8_t scale);
extern void l2_num(uint8_t x, uint8_t y, uint32_t v, uint8_t digits, uint8_t colour, uint8_t scale);
/* text in the 5x7 font (font5.h), colour on transparent; _center: centred on cx */
extern void l2_text5(uint8_t x, uint8_t y, const char *s, uint8_t colour, uint8_t scale);
extern void l2_text5_center(uint8_t cx, uint8_t y, const char *s, uint8_t colour, uint8_t scale);

extern void spr_init(void);              /* all sprites hidden, visible over the border */
extern void spr_pattern(uint8_t slot, const uint8_t *pixels256);
extern void spr_show(uint8_t n, int16_t x, int16_t y, uint8_t pattern, uint8_t flags);
#define SPR_MIRROR_X 0x08
#define SPR_MIRROR_Y 0x04
#define SPR_ROTATE   0x02
extern void spr_hide(uint8_t n);
/* a 32 x 32 sprite from four patterns pat..pat+3 (TL TR BL BR) on sprites
   first..first+3; mirror_x shows it mirrored (the halves swap) */
extern void spr_show_quad(uint8_t first, int16_t x, int16_t y, uint8_t pat, uint8_t mirror_x);
/* load `count` patterns from 8K memory pages starting at `page` (32 a page,
   e.g. data the .nex put in a bank), read through MMU slot `slot`, which
   gets page `restore` back */
extern void spr_patterns_from_pages(uint8_t page, uint8_t slot, uint8_t first, uint8_t count, uint8_t restore);

/* palette: which = 0 ULA, 1 Layer 2, 2 sprites (first palettes) */
extern void pal_set(uint8_t which, uint8_t index, uint16_t rgb333);
extern void pal_set8(uint8_t which, uint8_t index, uint8_t rrrgggbb);
extern void pal_load(uint8_t which, const uint16_t *rgb333, uint16_t n);   /* entries 0..n-1 */

/* Layer 2's 320 x 256 mode (l2wide.c): five 16K banks from l2w_bank,
   column-major (x * 256 + y), the whole display including the border, so
   its coordinates are the sprites' (no 32,32 offset). Not double buffered;
   a picture can come straight from the .nex (put it in BANK_n sections).
   Drawing maps the column's 8K page into MMU slot l2w_slot (default 3) and
   then puts page l2w_restore back there. */
extern uint8_t l2w_bank, l2w_slot, l2w_restore;
extern void l2w_init(uint8_t bank);                      /* mode, clip, visible */
extern uint8_t *l2w_column(uint16_t x);                  /* column x, row 0 (mapped) */
extern void l2w_done(void);                              /* restore the slot */
extern void l2w_fill(uint16_t x, uint8_t y, uint16_t w, uint16_t h, uint8_t colour);
/* 5x7 font text: each dot sx x sy pixels, the lower `gap` rows of a dot dark
   (gap 1 gives a dot-matrix look) */
extern void l2w_text5(uint16_t x, uint8_t y, const char *s, uint8_t colour, uint8_t sx, uint8_t sy, uint8_t gap);

extern void ay_chip(uint8_t n);          /* TurboSound: 0..2 */

#endif
