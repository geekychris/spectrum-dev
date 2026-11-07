/* Terra Strike - the 3D view on the Next (render.c) */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include "ts.h"

#define R_W      256                    /* the view: 256 x 170 (the original's 320 x 212 at 0.8) */
#define R_H      170
#define R_ROWS   (R_H / 2)              /* drawn rows (the copper shows each twice) */
#define R_CX     128
#define R_CY     77                     /* in full rows */
#define R_FOCAL  136                    /* the original's FOV 170 at 0.8 */

#define DRAW_ARENA      0x4000          /* the draw list: an 8K page in MMU slot 2 */
#define DRAW_ARENA_SIZE 8192

typedef struct {
    uint16_t tiles, models, polys, dropped;
} RenderStats;

typedef struct {
    uint8_t x, y;                       /* view pixel, drawn row */
    uint8_t col, size;                  /* pen, 1..5 */
} RPoint;

#define RENDER_MAX_POINTS (MAX_SHOTS + MAX_SHELLS + MAX_PARTS)

extern Cam cam;
extern RenderStats render_stats;
extern RPoint render_points[RENDER_MAX_POINTS];
extern uint8_t render_npoints;

void render_init(void);
void camera_follow(Cam *c, const World *w);
void render_build(const World *w);      /* the draw list (the heights' page mapped) */
void render_draw(void);                 /* fill it into the Layer 2 back buffer */

#endif
