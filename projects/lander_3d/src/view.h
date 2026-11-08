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

extern ViewCam vcam;
extern uint16_t view_polys;              /* polygons drawn last frame (the perf line) */
void view_reset(void);                   /* the camera's distance back to the start's */
void view_tick(const Level *L);          /* the camera's easing, a logic tick (the original: a frame) */
void view_camera(const Level *L);        /* resident: needs the rules' bank (ld_height) */
void view_init(void);                    /* the lander's model */
void view_draw(const Level *L, uint8_t frame);   /* bank 21: the picture into the back buffer */

#endif
