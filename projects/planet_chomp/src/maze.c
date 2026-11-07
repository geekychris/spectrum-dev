/*
 * The sphere maze (the original's maze.c, from SphereMaze.cs): carved with
 * a recursive backtracker, braided (no dead ends) and opened up with ~7%
 * extra passages, by the same 32-bit random numbers, so each level's maze is
 * the original's. The grid itself (neighbours, directions, tangents) is
 * static and comes from maze_tab.c.
 */
#include <string.h>
#include "pc.h"
#include "game.h"
#include "render.h"

const uint8_t bit4[4] = { 1, 2, 4, 8 };
const int16_t mz_keys[4] = { MZ_KEY0, MZ_KEY1, MZ_KEY2, MZ_KEY3 };

static uint32_t rng;

static int16_t rnd(int16_t n)
{
    rng = rng * 1103515245UL + 12345UL;
    return (int16_t)((uint16_t)(rng >> 16) % (uint16_t)n);
}

void mz_tan(vec3 *r, int16_t c, uint8_t k)
{
    const vec3 *d = DIR(c);
    vec3 t, u;
    v3_sub(&t, DIR(NB(c, k)), d);
    v3_scale(&u, d, v3_dot(&t, d));
    v3_sub(&t, &t, &u);
    v3_norm(r, &t);
}

uint8_t mz_degree(int16_t c)
{
    uint8_t o = mz_open[c];
    return (o & 1) + ((o >> 1) & 1) + ((o >> 2) & 1) + ((o >> 3) & 1);
}

static void set_open(int16_t c, uint8_t k)
{
    mz_open[c] |= bit4[k];
    mz_open[NB(c, k)] |= bit4[REV(c, k)];
}

static void carve(void)
{
    /* port: the work arrays are the autopilot's (pc_work), unused meanwhile */
    int16_t *stack = pc_work.carve.stack, *order = pc_work.carve.order;
    uint8_t *seen = pc_work.carve.seen;
    int16_t sp = 0, c, i, n;
    uint8_t k, choices[4];
    memset(seen, 0, CELLS);
    memset(mz_open, 0, CELLS);
    stack[sp++] = mz_start;
    seen[mz_start] = 1;
    while (sp > 0) {
        c = stack[sp - 1];
        n = 0;
        for (k = 0; k < 4; k++) if (!seen[NB(c, k)]) choices[n++] = k;
        if (!n) { sp--; continue; }
        k = choices[rnd(n)];
        set_open(c, k);
        seen[NB(c, k)] = 1;
        stack[sp++] = NB(c, k);
    }
    /* braid: no dead ends (joining two dead ends fixes both) */
    for (i = 0; i < CELLS; i++) order[i] = i;
    for (i = CELLS - 1; i > 0; i--) {
        int16_t j = rnd(i + 1), t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (i = 0; i < CELLS; i++) {
        int8_t pref = -1;
        c = order[i];
        if (mz_degree(c) != 1) continue;
        n = 0;
        for (k = 0; k < 4; k++) {
            if (OPEN(c, k)) continue;
            choices[n++] = k;
            if (mz_degree(NB(c, k)) == 1) pref = (int8_t)k;
        }
        set_open(c, pref >= 0 ? (uint8_t)pref : choices[rnd(n)]);
    }
    /* a few extra loops */
    for (c = 0; c < CELLS; c++)
        for (k = 0; k < 4; k++)
            if (!OPEN(c, k) && NB(c, k) > c && rnd(100) < 7) set_open(c, k);
    for (k = 0; k < 4; k++) { set_open(mz_start, k); set_open(mz_nest, k); }
}

void mz_bfs(int16_t src, int16_t *dist)
{
    int16_t *q = pc_work.bfs_q;
    int16_t head = 0, tail = 0, c, nb;
    uint8_t k;
    for (c = 0; c < CELLS; c++) dist[c] = 32767;
    dist[src] = 0;
    q[tail++] = src;
    while (head < tail) {
        c = q[head++];
        for (k = 0; k < 4; k++) {
            nb = NB(c, k);
            if (!OPEN(c, k) || dist[nb] != 32767) continue;
            dist[nb] = dist[c] + 1;
            q[tail++] = nb;
        }
    }
}

void mz_build(uint32_t seed)
{
    rng = seed;
    carve();
    rd_walls_build();
}

/* the walls of this maze, for the renderer: the closed edges, numbered as
   the original's mz_build_walls goes through them (cell, then exit, each
   edge once from its lower cell) */
void rd_walls_build(void)
{
    int16_t c, e = 0;
    uint8_t k;
    rd_nwalls = 0;
    for (c = 0; c < CELLS; c++)
        for (k = 0; k < 4; k++) {
            if (NB(c, k) < c) continue;
            if (!OPEN(c, k) && rd_nwalls < MAX_WALLS) rd_wall[rd_nwalls++] = e;
            e++;
        }
}
