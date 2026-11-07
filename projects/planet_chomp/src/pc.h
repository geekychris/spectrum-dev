/*
 * Planet Chomp for the Spectrum Next: the original's pc.h with 16-bit
 * vectors.
 *
 * port: the original keeps Q14 directions and Q8 positions in 32-bit longs.
 * Every one of them fits 16 bits, so here they are vec3 (sdk vec3.h), whose
 * products and shifts are the original's, bit for bit; the few sums that can
 * pass 16 bits are kept in 32 (marked). The sphere's static tables are made
 * on the host by the original's maze.c (tools/gen_data.c -> maze_tab.c),
 * flattened to [cell * 4 + exit].
 */
#ifndef PC_H
#define PC_H
#include <stdint.h>
#include "vec3.h"
#include "fixmath.h"
#include "maze_tab.h"

typedef vec3 V3;

#define ONE14   16384
#define Q8(v)   ((int16_t)((v) * 256))

#define GRID_N      9
#define CELLS       486
#define PLANET_R    Q8(12)

/* the grid (maze_tab.c) */
extern const uint16_t mz_nb[CELLS * 4];
extern const uint8_t mz_rev[CELLS * 4];
extern const uint8_t mz_len8[CELLS * 4];
extern const vec3 mz_dir[CELLS];
extern const vec3 mz_tan[CELLS * 4];
/* the renderer's (maze_tab.c) */
extern const int8_t rd_cell8[CELLS * 3];
extern const int8_t rd_corner8[CORNERS * 3];
extern const uint16_t ed_ck[EDGES];
extern const uint16_t ed_of[CELLS * 4];
extern const uint16_t ed_corner[EDGES * 2];
extern const int8_t ed_side8[EDGES * 3];
extern const int8_t ed_mid8[EDGES * 3];

#define CK(c, k)    (((uint16_t)(c) << 2) + (k))
#define NB(c, k)    ((int16_t)mz_nb[CK(c, k)])
#define REV(c, k)   (mz_rev[CK(c, k)])
#define LEN(c, k)   ((int16_t)mz_len8[CK(c, k)] + MZ_LEN_BASE)
#define TAN(c, k)   (&mz_tan[CK(c, k)])
#define DIR(c)      (&mz_dir[c])

/* the maze (maze.c): bit k of mz_open[c] = exit k is a passage */
extern uint8_t mz_open[CELLS];
extern const uint8_t bit4[4];
#define OPEN(c, k)  (mz_open[c] & bit4[k])
extern const int16_t mz_keys[4];
#define mz_start    MZ_START
#define mz_nest     MZ_NEST

void mz_build(uint32_t seed);
void mz_bfs(int16_t src, int16_t *dist);
uint8_t mz_degree(int16_t c);

/* the camera (PlanetCamera.cs) */
typedef struct {
    V3 focus;        /* camera focus direction, Q14 */
    V3 up;           /* screen-up tangent, Q14 */
    int16_t height;  /* Q8 */
} Camera;

/* the log: zx_log on the Next, the host sim's own on the host */
#ifdef __SDCC
#include "zxlog.h"
#define PC_LOG zx_log
#else
void pc_log(const char *fmt, ...);
#define PC_LOG pc_log
#endif

#endif
