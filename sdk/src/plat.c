/* Platformer movement against tmap (see plat.h). Builds on the host too. */
#include <stdint.h>
#include <string.h>
#include "plat.h"
#include "tmap.h"

uint8_t plat_shift = 4;
uint8_t plat_bw = 16, plat_bh = 16;
const uint8_t *plat_solid;
uint8_t plat_outside;

#ifndef __SDCC
/* the Spectrum's is plat_kind.asm */
uint8_t plat_kind_at(int16_t px, int16_t py)
{
    if (px < 0 || py < 0) return plat_outside;
    px >>= plat_shift;
    py >>= plat_shift;
    if (px >= tmap_w || py >= tmap_h) return plat_outside;
    return tmap_row[py][px];
}
#endif

uint8_t plat_solid_at(int16_t px, int16_t py)
{
    return plat_solid[plat_kind_at(px, py)];
}

/* zsdcc compiles a static's fields to direct loads and a pointer's to far
   more code, so the moves work on a copy */
static plat_body pb;

void plat_move_x(plat_body *b)
{
    int16_t nx;
    memcpy(&pb, b, sizeof(pb));
    nx = pb.x + pb.vx;
    if (pb.vx > 0) {
        int16_t e = nx + plat_bw - 1;
        if (plat_solid_at(e, pb.y + 1) || plat_solid_at(e, pb.y + plat_bh - 1))
            nx = (((pb.x >> plat_shift) + 1) << plat_shift) - plat_bw;
    } else if (pb.vx < 0) {
        if (plat_solid_at(nx, pb.y + 1) || plat_solid_at(nx, pb.y + plat_bh - 1))
            nx = (pb.x >> plat_shift) << plat_shift;
    }
    b->x = nx;
}

void plat_move_y(plat_body *b, int8_t gravity, int8_t max_fall)
{
    int16_t ny;
    memcpy(&pb, b, sizeof(pb));
    pb.vy += gravity;
    if (pb.vy > max_fall) pb.vy = max_fall;
    ny = pb.y + pb.vy;
    pb.on_ground = 0;
    if (pb.vy > 0) {
        int16_t f = ny + plat_bh - 1;
        if (plat_solid_at(pb.x + 1, f) || plat_solid_at(pb.x + plat_bw - 2, f)) {
            ny = (((pb.y >> plat_shift) + 1) << plat_shift) - plat_bh;
            pb.vy = 0;
            pb.on_ground = 1;
        }
    } else if (pb.vy < 0) {
        if (plat_solid_at(pb.x + 1, ny) || plat_solid_at(pb.x + plat_bw - 2, ny)) {
            ny = (pb.y >> plat_shift) << plat_shift;
            pb.vy = 1;
        }
    }
    pb.y = ny;
    memcpy(b, &pb, sizeof(pb));
}
