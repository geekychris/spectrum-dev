/* Platformer movement against tmap (see plat.h). Builds on the host too. */
#include <stdint.h>
#include "plat.h"
#include "tmap.h"

uint8_t plat_shift = 4;
uint8_t plat_bw = 16, plat_bh = 16;
const uint8_t *plat_solid;
uint8_t plat_outside;

uint8_t plat_kind_at(int16_t px, int16_t py)
{
    uint8_t c, r;
    if (px < 0 || py < 0) return plat_outside;
    c = (uint8_t)(px >> plat_shift);
    r = (uint8_t)(py >> plat_shift);
    if ((px >> plat_shift) >= tmap_w || (py >> plat_shift) >= tmap_h) return plat_outside;
    return tmap_row[r][c];
}

uint8_t plat_solid_at(int16_t px, int16_t py)
{
    return plat_solid[plat_kind_at(px, py)];
}

void plat_move_x(plat_body *b)
{
    int16_t x = b->x, nx = x + b->vx;
    if (b->vx > 0) {
        int16_t e = nx + plat_bw - 1;
        if (plat_solid_at(e, b->y + 1) || plat_solid_at(e, b->y + plat_bh - 1))
            nx = (((x >> plat_shift) + 1) << plat_shift) - plat_bw;
    } else if (b->vx < 0) {
        if (plat_solid_at(nx, b->y + 1) || plat_solid_at(nx, b->y + plat_bh - 1))
            nx = (x >> plat_shift) << plat_shift;
    }
    b->x = nx;
}

void plat_move_y(plat_body *b, int8_t gravity, int8_t max_fall)
{
    int16_t y = b->y, ny;
    b->vy += gravity;
    if (b->vy > max_fall) b->vy = max_fall;
    ny = y + b->vy;
    b->on_ground = 0;
    if (b->vy > 0) {
        int16_t f = ny + plat_bh - 1;
        if (plat_solid_at(b->x + 1, f) || plat_solid_at(b->x + plat_bw - 2, f)) {
            ny = (((y >> plat_shift) + 1) << plat_shift) - plat_bh;
            b->vy = 0;
            b->on_ground = 1;
        }
    } else if (b->vy < 0) {
        if (plat_solid_at(b->x + 1, ny) || plat_solid_at(b->x + plat_bw - 2, ny)) {
            ny = (y >> plat_shift) << plat_shift;
            b->vy = 1;
        }
    }
    b->y = ny;
}
