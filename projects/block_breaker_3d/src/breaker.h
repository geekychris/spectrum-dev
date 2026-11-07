/*
 * breaker.h - Block Breaker 3D's game shell, shared by main.c, render.c and
 * hud.c (the original's breaker.h, for the Spectrum Next).
 */
#ifndef BREAKER_H
#define BREAKER_H

#include <stdint.h>
#include "bb.h"

/* port: BS_ENTRY, the high-score initials, is new */
enum { BS_TITLE, BS_PLAY, BS_OVER, BS_ENTRY };

#define MAX_PARTS 48
/* a bit of a broken brick, world units * 16; port: 16-bit, and the brick's
   type and lit/shaded instead of a pen (sprite palettes) */
typedef struct { int16_t x, y, z, vx, vy, vz; int8_t life; uint8_t type, shade; } Part;

typedef struct {
    uint8_t state, state_timer;
    uint8_t start_level, autoplay, paused, help;
    char flash[32];
    uint8_t flash_timer;
    int32_t best;
    Part part[MAX_PARTS];
} Breaker;

/* what moves, as it was before the last tick: drawn halfway between that and
   now on the frame between two ticks (port: the logic runs at the original's
   25 Hz, the picture at 50) */
typedef struct {
    int32_t bx, by, bz, px, py;
    int32_t cx[BB_MAX_CAPS], cy[BB_MAX_CAPS], cz[BB_MAX_CAPS];
} Before;

extern Breaker br;
extern BBGame game;
extern Before before;

/* render.c */
void scene_init(void);
void scene_level(void);                 /* every brick redrawn (a new level) */
void scene_dirty(int16_t x0, int16_t y0, int16_t x1, int16_t y1);   /* Layer 2 back to the scene there */
void scene_draw(const BBGame *g, uint8_t half);
void scene_sprites_off(void);
void scene_blank(void);                 /* Layer 2 black (help pages) */
void scene_box(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t edge);  /* a box in Layer 2 (render.c's g_box) */
int  brick_pen(int type, int hits);
extern uint16_t scene_lines;            /* raster lines the last scene_draw took */

/* hud.c */
void hud_init(void);
void hud_frame(const BBGame *g);
void hud_clear(void);
extern uint8_t hud_title_page;

/* sound.c */
void snd_init(void);
void snd_music(uint8_t on);
void snd_play(uint8_t sfx);
void snd_tick(void);
uint8_t snd_reg(uint8_t chip, uint8_t reg);

#endif
