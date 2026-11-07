/*
 * attrtint.h - colour for moving objects on the 48K screen without spoiling
 * the scenery: the cells under an object take its colour only where the
 * scene's attribute is the plain background one (open sky, empty space);
 * cells with scenery in them keep theirs. A bitmap of the plain cells
 * (tint_map, 96 bytes: bit 7 - (offset & 7) of byte offset >> 3, offset =
 * row * 32 + col) says which cells may be tinted; a tinted cell goes back to
 * tint_plain.
 *
 *   tint_plain                the background attribute that may be tinted
 *   tint_capture()            after drawing the scene: map the cells of 0x5800
 *                             that hold tint_plain
 *   tint_mark(col, row, w, plain)   cells drawn on (plain = 0, e.g. text
 *                             printed over the sky) or cleared again (1)
 *   tint_is_plain(offset)
 *   tint_restore(t)           put back tint_plain where t was tinted
 *   tint_place(t, col, row, w, h, attr)   tint a rectangle (clipped to the
 *                             screen) and remember it in t
 *   tint_rect_off(t)          restore and forget
 *   tint_cell(offset, attr) / tint_uncell(offset)   single cells
 *
 * Each frame: tint_restore() every object, then tint_place() every object
 * (restore all, then tint all), so one object's restore can't undo another's
 * tint; the last one placed wins where they overlap.
 */
#ifndef ZX_ATTRTINT_H
#define ZX_ATTRTINT_H
#include <stdint.h>
extern uint8_t tint_map[96];
extern uint8_t tint_plain;
typedef struct { uint8_t col, row, w, h; } tint_area;
extern void tint_capture(void);
extern void tint_mark(uint8_t col, uint8_t row, uint8_t w, uint8_t plain);
extern uint8_t tint_is_plain(uint16_t offset);
extern void tint_restore(const tint_area *t);
extern void tint_apply(const tint_area *t, uint8_t attr);
extern void tint_place(tint_area *t, uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t attr);
extern void tint_rect_off(tint_area *t);
extern void tint_cell(uint16_t offset, uint8_t attr);
extern void tint_uncell(uint16_t offset);
#endif
