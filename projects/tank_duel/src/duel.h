/* duel.h - Tank Duel's game shell, shared by main.c, front.c and render.c
   (the original's duel.h) */
#ifndef DUEL_H
#define DUEL_H

#include <stdint.h>
#include "td.h"

enum { DS_TITLE, DS_PLAY, DS_OVER };

#define MAX_BLASTS 12
typedef struct { int8_t t; uint8_t size, pen; int16_t x, y, z; } Blast;   /* an explosion: world units */

typedef struct {
    uint8_t state, state_timer;
    uint8_t start_round, autoplay, paused, help;
    char flash[32];
    uint8_t flash_timer, flash_pen;
    uint8_t hurt_timer;                 /* the screen's red edge after a hit */
    int32_t best;
    Blast blast[MAX_BLASTS];
} Duel;

extern Duel du;
extern TDGame game;

/* the original's 32 pens (render.c) */
#define PEN_SKY    4
#define PEN_FLOOR  5
#define PEN_GRID   6
#define PEN_WALL   7
#define PEN_SHIELD 22
#define PEN_SHELL  23
#define PEN_ESHELL 24
#define PEN_RING   25
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29
#define PEN_FIRE   30
#define PEN_RADAR  31

/* render.c (bank 18) */
void render_init(void);
void render_scene(void);                /* the 3D view and the gun into Layer 2's back buffer */
extern uint8_t render_polys;            /* polygons drawn by the last render_scene */

#endif
