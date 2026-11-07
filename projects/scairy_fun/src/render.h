/*
 * render.h - ScAIry Fun's renderer on the Next (render.c): the road drawn as
 * segments far to near on Layer 2, like the original's render.c.
 */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include "sc.h"

#define VIEW_W   256
#define VIEW_H   160                    /* 3D area on screen; the HUD below (port: 212 of 256 rows) */
#define VIEW_ROWS 80                    /* port: drawn at half height, each row shown twice (copper) */
#define VIEW_CX  128
#define VIEW_CY  75
#define FOV      144                    /* port: 180 at 320 wide */
#define DRAW_DIST_UNITS 4400            /* sprites further than this are not drawn */

/* the pens of the original's 32-colour palette (models.h), Layer 2 indices */
#define PEN_BLACK     0
#define PEN_WHITE     1
#define PEN_PANEL     2
#define PEN_GREY      3
#define PEN_SKY       4
#define PEN_HAZE      5
#define PEN_WATER     6     /* 6, 7 */
#define PEN_ROAD      8     /* 8, 9 */
#define PEN_PAVE      10
#define PEN_GRASS     11    /* 11, 12 */
#define PEN_DIRT      13    /* 13, 14 */
#define PEN_ROCK      15
#define PEN_RED       16    /* 16, 17 */
#define PEN_YELLOW    18
#define PEN_ORANGE    19
#define PEN_AI        20    /* cyan: the autopilot's path and boxes */
#define PEN_AI2       21    /* green */
#define PEN_BLUE      22    /* 22, 23 */
#define PEN_CARW      24    /* 24, 25 */
#define PEN_KHAKI     26
#define PEN_TREE      27
#define PEN_GLASS     28
#define PEN_BROWN     29    /* 29 dark, 30 light */
#define PEN_SHADOW    31
#define PEN_FOG       32    /* port: 32..63, 64..95, 96..127: the pens fading into the haze */

typedef struct {
    int32_t x, z;                       /* world units */
    int16_t y;
    int yaw;                            /* 16-bit angle */
    int16_t tan;                        /* tan(yaw) * 256: the road-aligned shear */
    int16_t horizon;                    /* row of Layer 2 (VIEW_ROWS); on screen: twice that */
    int car_dir;                        /* road_dir at the car (for its sprite's angle) */
} Cam;

extern Cam cam;
extern const uint16_t route_palette[NUM_ROUTES][32];
extern const uint16_t route_sky[NUM_ROUTES][2];
extern uint16_t render_polys;           /* polygons drawn, last frame */

void render_init(void);
void camera_follow(const World *w);
void render_world(const World *w);
/* the skyline: a panorama of the whole horizon (port: drawn once per route,
   copied into the view by DMA each frame) */
#define SKY_PH   16                     /* rows above the horizon */
#define SKY_W    905                    /* 65536 of yaw at FOV / 10430 a pixel */
#define SKY_PAGE 44                     /* pages 44-45 */
void sky_copy(uint16_t p0, int16_t ytop);   /* main.c (resident: it maps the panorama over the renderer) */
int16_t render_horizon(int view);
void render_copper(int route, int view);/* the line doubling and the sky's gradient */

#endif
