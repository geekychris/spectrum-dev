/* bt.h - Boing Twist for the Spectrum Next: memory map and shared parts.
 *
 *   0x0000-0x5FFF  the logic: bank 25 (the text, the tables, the rules'
 *                  variables) and page 52 (the rules' code); while the ball
 *                  is drawn the texture (3 pages); while its texture is
 *                  built the picture (3 pages). The tilemap (bank 5) is
 *                  mapped at 0x4000 by nexttile while it writes.
 *   0x6000-0x7FFF  a view's stream, page by page; data pages when read
 *   0x8000-0xEFFF  the program; 0xF000- the IM 2 table and the stack
 *
 * 16K banks: 5 the tilemap; 9-11 and 12-14 Layer 2 (two screens); then
 * the working banks below; the data from FIRST_DATA_BANK (gfx_data.h).
 */
#ifndef BT_H
#define BT_H
#include <stdint.h>

#define TEX_BANK   16          /* the ball's texture: 256 x 96, rows 0-63 bank 16, 64-95 bank 17 */
#define TEX_PAGE   32
#define TF_BANK    18          /* the flip's texture: 256 x 64 */
#define TF_PAGE    36
#define BOING_BANK 19          /* the Boing ball's picture, made for the grid: pages 38-40 */
#define BOING_PAGE 38
#define SAVE_PAGE  41          /* a band's rows while it twists */
#define ROOM_BANK  21          /* the room, 8 bits a pixel: banks 21-23 */
#define SOUND_BANK 24          /* the sound (tools/gen_sound.py) */
#define LOGIC_PAGE 50          /* the logic: bank 25 at 0x0000 (the text, the tables, the rules' variables) */
#define RULES_PAGE 52          /*   and page 52 at 0x4000 (the rules' code): Makefile, zpragma.inc */

#define C_GROOVE 25            /* texture colours past the picture's 25 */
#define C_RING   26
#define C_HALF   27

/* the logic's pages at 0x0000-0x5FFF and bank 5's upper page at 0x6000:
   what everything that maps those slots puts back */
void logic_map(void);

/* render.asm */
extern uint8_t rv_page, rv_off, rv_bank, rv_tex;
extern int16_t rv_cx, rv_cy;
extern uint16_t rv_pixels;
extern void rv_draw(void);
extern void rv_or_span(uint8_t y, uint8_t x, uint8_t n, uint8_t bits);
extern void rv_copy(const uint8_t *src, uint8_t *dst, uint8_t n, uint8_t turned);
extern void rv_fill(uint8_t *dst, uint8_t v, uint16_t n);   /* write-only (the window) */
extern void rv_row(uint8_t *dst, const uint8_t *prog, uint8_t n, uint8_t dv);
extern void rv_unpack4(const uint8_t *src, uint8_t *dst);

#endif
