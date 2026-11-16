/* gridmove.c - maze-chase movement (see gridmove.h) */
#include "gridmove.h"

const int8_t grid_dx[4] = { 1, 0, -1, 0 };
const int8_t grid_dy[4] = { 0, 1, 0, -1 };

void grid_place(grid_body *b, int16_t tx, int16_t ty)
{
    b->tile_x = tx;
    b->tile_y = ty;
    b->pixel_x = tx * GRID_TILE;
    b->pixel_y = ty * GRID_TILE;
    b->sub_x = 0;
    b->sub_y = 0;
}

/* p / 8 rounded toward zero, as C's division does for the original */
#define TO_TILE(p) ((p) >= 0 ? (p) >> 3 : -((-(p)) >> 3))

/* the reference for gridmove_s.asm (which is what a Spectrum build links) */
#ifndef __SDCC
void grid_move(grid_body *b, int16_t dir, int16_t speed, int16_t wrap_w)
{
    int16_t *pos, *sub, v, t;
    if (dir < 0) return;
    /* one axis: the same as adding dx * speed and dy * speed; the original's
       carry loops are one arithmetic shift (floor) and a mask */
    if (dir & 1) { pos = &b->pixel_y; sub = &b->sub_y; }
    else         { pos = &b->pixel_x; sub = &b->sub_x; }
    v = *sub + (dir < 2 ? speed : -speed);
    t = *pos + (v >> 8);
    *sub = v & 255;
    /* never step over a tile centre: stop on it (exactly, sub 0). The
       original tested "centred" after steps of 1-3 pixels and missed about
       one centre in five, so actors missed turns and walked on into walls. */
    if (t > *pos) {
        int16_t c = (*pos | 7) + 1;              /* the next centre to the right/down */
        if (c < t) { t = c; *sub = 0; }
    } else if (t < *pos) {
        int16_t c = (*pos - 1) & ~7;             /* the next centre to the left/up */
        if (c > t) { t = c; *sub = 0; }
    }
    *pos = t;
    t = b->pixel_x + GRID_TILE / 2;
    b->tile_x = TO_TILE(t);
    t = b->pixel_y + GRID_TILE / 2;
    b->tile_y = TO_TILE(t);
    if (wrap_w) {
        if (b->pixel_x < -GRID_TILE) {
            b->pixel_x = wrap_w;
            b->sub_x = 0;
        } else if (b->pixel_x >= wrap_w) {
            b->pixel_x = -GRID_TILE;
            b->sub_x = 0;
        }
    }
}
#endif

static uint8_t sq_abs(int16_t d)       /* |d| (<= 127) */
{
    return (uint8_t)(d < 0 ? -d : d);
}

uint16_t grid_dist2(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    uint8_t dx = sq_abs(x2 - x1), dy = sq_abs(y2 - y1);
    return (uint16_t)dx * dx + (uint16_t)dy * dy;
}

int8_t grid_choose(int16_t tx, int16_t ty, int8_t dir,
                   int16_t target_x, int16_t target_y, grid_open_fn open)
{
    static const int8_t priority[4] = { GRID_UP, GRID_LEFT, GRID_DOWN, GRID_RIGHT };
    int8_t best_dir = dir, opp = GRID_OPPOSITE(dir);
    uint16_t best = 0xFFFF;
    uint8_t i;

    for (i = 0; i < 4; i++) {
        int8_t d = priority[i];
        int16_t nx, ny;
        uint16_t dd;
        if (d == opp) continue;
        nx = tx + grid_dx[d];
        ny = ty + grid_dy[d];
        if (!open(nx, ny)) continue;
        dd = grid_dist2(nx, ny, target_x, target_y);
        if (dd < best) {
            best = dd;
            best_dir = d;
        }
    }
    return best_dir;
}
