/*
 * Orbital Patrol on the Next: everything that is drawn (the original's
 * draw.c). The display is the original's 320 x 256, 1:1:
 *
 *   Layer 2 320 x 256     the scanner (rows 0-19) and the HUD (236-255)
 *                         stand still; the playfield rows scroll with the
 *                         world (the copper sets the scroll row by row), so
 *                         only the terrain columns that scroll in are drawn
 *   tilemap (behind)      the stars, scrolled at a quarter of the speed
 *   sprites               everything that moves, and the wave messages
 *
 * view_commit() in the vertical blank shows what view_prepare() worked out.
 */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "game.h"

void view_init(void);                   /* layers, palettes, patterns, the stars */
void view_play_screen(GameState *gs);   /* a new playfield: scanner, terrain, HUD */
void view_prepare(GameState *gs);       /* the frame: scroll, terrain columns, scanner, HUD, sprites */
void view_commit(void);                 /* in the vertical blank */
void view_message(WORD level, const char *msg);   /* "WAVE n" + msg as sprite text; NULL hides */
/* static screens (no scroll, no sprites): cleared to the sky (stars show) */
void view_static_screen(GameState *gs);
void view_text(int16_t sx, uint8_t y, const char *s, uint8_t colour);   /* on the screen as seen */
void view_text_center(uint8_t y, const char *s, uint8_t colour);
void view_fill(int16_t sx, uint8_t y, uint16_t w, uint8_t h, uint8_t colour);
extern uint16_t view_cols_drawn;        /* terrain columns drawn last frame (profiling) */
extern uint8_t view_sprites_used;

/* colours: RRRGGGBB (the Next's default palette), the original's */
#define C_BLACK     0x00
#define C_WHITE     0xFF
#define C_STAR_DIM  0x25
#define C_TERR_DK   0x24
#define C_TERRAIN   0x4C
#define C_TERR_LT   0x54
#define C_YELLOW    0xFC
#define C_ORANGE    0xF0
#define C_GREEN     0x1C
#define C_MAGENTA   0xE2
#define C_RED       0xE0
#define C_CYAN      0x1B
#define C_SKY       0xE3        /* transparent: the stars behind show */
#endif
