/* view.c: Lander 3D's picture (the original's render.c scene()) */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "ld.h"

/* the camera (engine units, 16 to the metre), made by view_camera() while
   the rules' bank is in (it reads the ground), drawn from by view_draw() */
typedef struct {
    int32_t x, y, z;
    uint16_t pitch;
    int16_t cp, sp;                      /* cos, sin of the pitch, Q12 */
    int32_t shadow_y;                    /* the ground under the craft (units) */
    fix up[3];                           /* the craft's up (the exhaust) */
    uint8_t world;
} ViewCam;

/* what the HUD and the view's compass show, worked out while the rules'
   bank is in (hud_values) */
typedef struct {
    fix agl, vs, hs, tilt, lim_vs, lim_hs, lim_tilt;
    int32_t pad_dist;
    uint16_t pad_dir, wind_dir;          /* 16-bit angles from +z towards +x */
    uint8_t fuel, thr, pad, wind;        /* fuel and throttle in %, the nearest pad, Mars's wind */
} HudVals;
extern HudVals hv;                       /* (game.c) */
extern uint8_t view_compass;             /* 1: the arrow to the nearest pad (not behind the title) */

extern ViewCam vcam;
extern uint16_t view_polys;              /* polygons drawn last frame (the perf line) */
void view_reset(void);                   /* the camera's distance back to the start's */
void view_tick(const Level *L);          /* the camera's easing, a logic tick (the original: a frame) */
void view_camera(const Level *L);        /* resident: needs the rules' bank (ld_height) */
void view_init(void);                    /* the lander's model */
void view_draw(const Level *L, uint8_t frame);   /* bank 21: the picture into the back buffer */

#endif
