/* the renderer's tables (rtab*.c: made by tools/gen_data.c), its corner
   records and the memory pages they're in on the Next (main.c's memory map) */
#ifndef RTAB_H
#define RTAB_H
#include "pc.h"

/* a lattice corner as xform.asm leaves it: screen points at the ground (b)
   and the walls' top (t), the depth of the ground point (Q8), the horizon
   dot (Q14), shown or not (16 bytes: records are addressed by shifting) */
typedef struct { int16_t bx, by, tx, ty, z, dot; uint8_t vis, pad[3]; } Corner;
/* an edge's wall: its corners' records, its side normal (Q7) */
typedef struct { uint16_t a, b; int8_t side[3]; uint8_t pad; } Edge;   /* 8 bytes */

#ifdef __SDCC
#define CN_BASE 0x4000                          /* page 38 */
#define CREC(i) (CN_BASE + 16 * (i))            /* tables hold records' addresses */
#define CN(r)   ((Corner *)(r))
#else
extern Corner cn_host[];
#define CREC(i) (i)
#define CN(r)   (&cn_host[r])
#endif

#define RECIP_N 1024
extern const int16_t rd_recip[RECIP_N];          /* focal * 16384 / (16 i + 8): z = 16 i .. 16 i + 15 (Q8) */
extern const uint8_t rd_tsize[128];              /* 0.13 at depth (i + 0.5) / 2 units, in 1/16 px */
extern const V3 rd_nest_rim[8];                  /* the nest disc's rim, world Q8 */
extern const uint8_t rd_corner8[CORNERS * 3];    /* rtab_a.c: corner directions, Q7 + 128 */
extern const uint16_t rd_cell_corner[CELLS * 4]; /* rtab_a.c: each cell's four corners' records */
extern const Edge ed_rec[EDGES];                 /* rtab_b.c */

/* the camera for xform.asm, set by render.c */
extern vec3 xf_m[3], xf_c;
extern int8_t xf_chat[3], xf_fwd8[3];
extern int16_t xf_thr, xf_thr_wall, xf_thr_crumb;
void xf_corners(void);

#ifdef __SDCC
void rd_map_corners(void);                       /* page 39 into slot 3 */
void rd_map_walls(void);                         /* page 40 into slot 3 */
#else
#define rd_map_corners()
#define rd_map_walls()
#endif
#endif
