/* Planet Chomp's 3D view on Layer 2 (render.c): the original's render_cel.c
 * camera and draw order, at 256 x 192 */
#ifndef RENDER_H
#define RENDER_H
#include "pc.h"

#define RD_W   256
#define RD_H   192
#define RD_CX  128
#define RD_CY  96
#define RD_ROWS 96                  /* port: Layer 2 rows drawn; the copper shows each twice */

/* Layer 2 palette indices (gen_gfx.py makes the palette) */
enum {
    C_SPACE = 0, C_STAR1, C_STAR2, C_STAR3, C_STAR4,
    C_PLANET, C_NEST, C_CRUMB, C_WALL_TOP, C_WALL_SIDE,   /* 8 side shades: 9..16 */
    C_COUNT = C_WALL_SIDE + 8
};

#define RD_WALL_FLASH 1

/* the walls of the current maze: edges that are closed (rd_walls_build, from
   maze.c's mz_open, in the original's mz_build_walls order) */
#define MAX_WALLS 490                /* a spanning tree leaves at most 487 closed */
extern int16_t rd_wall[MAX_WALLS];
extern int16_t rd_nwalls;
void rd_walls_build(void);          /* maze.c (needs the grid: logic phase) */

void rd_frame(void);
void rd_bg_disc(void);              /* the planet's disc into the background (l2_back), when rd_bg_want != rd_bg_h */
extern int16_t rd_bg_h, rd_bg_want;
                /* the view of g.cam into the Layer 2 back buffer */
/* world Q8 -> screen pixels (256 x 192 frame) and depth (Q8); 0 if behind
   the near plane. Uses the camera of the last rd_frame. */
uint8_t rd_project(const V3 *p, int16_t *sx, int16_t *sy, int16_t *z);
/* pixels a world size (Q8) is at depth z (Q8): size * focal / z, in 1/16 px */
int16_t rd_size16(int16_t size, int16_t z);
/* the dot product of a direction (Q14) with the camera's position, the
   original's horizon test (>> 14, world Q8) */
int16_t rd_horizon(const V3 *dir);
extern V3 rd_campos;
extern uint8_t rd_dbg;
extern int16_t rd_cull;
extern uint8_t rd_thin;
extern int16_t rd_stats_walls, rd_stats_crumbs;

/* drawing (render.c draws through these: l2poly on the Next, a frame
   buffer on the host) */
void dr_begin(void);
void dr_poly(const int16_t *xy, uint8_t n, uint8_t colour);
void dr_rect(int16_t y0, int16_t y1, int16_t x0, int16_t x1, uint8_t colour);
void dr_end(void);
#endif
