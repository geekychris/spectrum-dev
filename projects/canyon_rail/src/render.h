/* render.h - Canyon Rail's picture on the Next (render.c) */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>
#include "cr.h"

/* port: the original's 320 x 256 view drawn on Layer 2's 256 x 192: the
   sim keeps the original's view coordinates (the sight, hits); the picture
   scales them by 4/5 across and 3/4 down */
#define VIEW_W 256
#define VIEW_H 192
#define VIEW_ROWS 96                    /* port: drawn half height, each row shown twice (main.c's copper) */
#define SX(x) ((int16_t)(((x) * 4) / 5))
#define SY(y) ((int16_t)(((y) * 3) / 4))

/* pens (the original's rail_palette order) */
#define PEN_BLACK  0
#define PEN_WHITE  1
#define PEN_PANEL  2
#define PEN_GREY   3
#define PEN_SKY    4
#define PEN_BANK   5
#define PEN_RIVER  7
#define PEN_WALL   8
#define PEN_SPIRE  12
#define PEN_TURRET 13
#define PEN_DRONE  15
#define PEN_POD    17
#define PEN_BOLT   18
#define PEN_GLOW   19
#define PEN_BOSS   20
#define PEN_NODE   22
#define PEN_FIRE   23
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29

extern const uint16_t rail_palette[32];
extern uint16_t render_polys;
void render_init(void);
void render_scene(const CRGame *g);     /* the sky and the canyon into the back buffer */

#endif
