/* The C of sim_asm.asm's loops, for the host build (tools/trace.c). */
#ifndef __SDCC
#include "sc.h"
#include "world_int.h"

uint8_t obj_top;

int closed_down(const Seg *seg, int hi, int lo)
{
    int k;
    for (k = hi; k >= lo; k--)
        if (seg[k].closed & 1) return k;
    return -1;
}

const Obj *lead_scan(const Obj *objs, const Obj *o, int32_t *best)
{
    const Obj *p = objs, *found = 0;
    int k;
    for (k = 0; k < obj_top; k++, p++) {
        int32_t dz;
        if (p == o || p->type == OBJ_NONE || p->v < 0 || p->type == OBJ_CYCLIST ||
            p->type == OBJ_DEBRIS) continue;
        if (iabs16(p->d - o->d) > (40 * (1 << FBITS))) continue;
        dz = p->z - o->z;
        if (dz > 0 && dz < *best) { *best = dz; found = p; }
    }
    return found;
}

int lane_scan(const Obj *objs, int lane, int32_t z, int32_t lo, int32_t hi, int skip)
{
    int i;
    for (i = 0; i < obj_top; i++) {
        const Obj *o = &objs[i];
        int32_t dz;
        if (i == skip || o->type == OBJ_NONE || o->lane != lane) continue;
        dz = o->z - z;
        if (dz > lo && dz < hi) return 0;
    }
    return 1;
}
int obj_find(const Obj *objs, int start, uint16_t mask)
{
    int i;
    for (i = start; i < obj_top; i++) if (mask & (1 << objs[i].type)) return i;
    return MAX_OBJS;
}
#endif
