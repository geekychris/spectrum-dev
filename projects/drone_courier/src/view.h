/* view.c: Drone Courier's 3D view on Layer 2 */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "dc.h"

/* profiling (main.c): raster lines since clk_mark, up to 5 seconds */
typedef struct { uint8_t f; uint16_t r; } clk_t;
void clk_mark(clk_t *c);
uint16_t clk_lines(const clk_t *c);

#define VIEW_ROWS 96                    /* rows drawn; the copper shows each twice */

/* in bank 21 (at 0x0000): call through main.c's view_draw / view_clear */
void view_init(void);                   /* palette, Layer 2, the copper's line doubling */
void view_scene(const DCGame *g, uint16_t cam_yaw);   /* the original's scene(), into the back buffer */
void view_blank(void);                  /* the back buffer black (behind the help pages) */
/* resident (main.c): bank 21 mapped for them, then the rules' bank 20 again */
void view_draw(const DCGame *g, uint16_t cam_yaw);
void view_clear(void);
/* the drone's sprites for the picture just drawn (hud_drone shows them) */
typedef struct { int16_t x, y; uint8_t frame, mirror, fragile, show; } DroneSpr;
extern DroneSpr view_drone;
extern uint8_t view_towers, view_polys;
extern uint16_t view_t_build, view_t_fill;   /* raster lines, summed (game.c's perf log) */ /* the last scene's towers and polygons drawn */
extern uint16_t view_cam_yaw;
#endif
