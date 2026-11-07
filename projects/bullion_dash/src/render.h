/*
 * Bullion Dash - the Spectrum screen (replaces the original's render.c/gfx.c)
 *
 * The 28 x 16 grid is drawn as character cells 2..29 x 2..17 by the SDK's
 * cellspr48 (tiles and sprites composed per cell, attribute colour per cell);
 * the player and the guards are 8 x 8 sprites placed at the original's pixel
 * positions scaled by 8/10 across and 8/14 down.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#define ROOM_COL 2
#define ROOM_ROW 2

void render_init(void);
/* the play screen: frame, HUD, the whole room (editor = the editor's tile look) */
void render_room(uint8_t editor);
/* a tile changed (level.c's tile_set) */
void render_tile_changed(uint8_t x, uint8_t y);
/* place the sprites from the game state and redraw what changed;
   entities = 0 hides the player and the guards (the death blink) */
void render_frame(uint8_t entities);
/* the score line (only what changed, unless force) */
void render_status(uint8_t force);
/* a message box over the middle of the room (lines may be 0) */
void render_box(const char *l1, const char *l2, const char *l3, uint8_t attr);
/* the editor's cursor (sprite 0) */
void render_cursor(uint8_t x, uint8_t y, uint8_t on);
/* the tile colours (art.h has the tile art) */
extern const uint8_t tile_attr[];

#endif
