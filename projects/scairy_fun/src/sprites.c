// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sprites.c - vehicles, animals, people, debris and cones as hardware sprites.
 *
 * port: the original draws these as polygon models with the road (render.c
 * add_model). Here each is a hardware sprite made from the same models
 * pre-drawn on the host (tools/gen_sprites.py) at 16 sizes; the nearest size
 * is chosen, the biggest ones scaled by the sprite hardware. The patterns are
 * streamed from banks 40..44 into one half of the sprite pattern memory
 * while the other half is on screen (spr_frame_end), and the sprite
 * attributes are sent after the flip (spr_show_frame), so nothing tears.
 * Sprites are over Layer 2, so an object behind a hill crest (its foot below
 * the top of the road nearer to the eye) is hidden; one only partly behind
 * shows whole.
 */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>

#include "sc.h"
#include "render.h"
#include "sprites.h"
#include "spr_views.h"
#include "persp.h"
#include "nextgfx.h"

#define SEGF       ((int32_t)SEG_LEN * (1 << FBITS))
#define MAX_SPR    24                   /* objects a frame */
#define HALF       64                   /* 4-bit patterns in each half of the pattern memory */

typedef struct {
    uint8_t view, size, pal, flags;     /* flags: SPR_MIRROR_X */
    int16_t x, y;                       /* screen position of the foot */
    int16_t zr;                         /* depth (for the order) */
    int16_t seg;                        /* its segment, for the crests */
} Obj2;

static Obj2 list[MAX_SPR];
static uint8_t nlist;
static int16_t seg_top[48];             /* top of the road drawn at each segment (from s0) */
static int16_t cover;                   /* the highest road row nearer than the segment drawn */
static int s0_frame;
static uint8_t half;                    /* pattern half being filled: 0 or 1 */
static uint8_t attrs[128 * 5];          /* the next frame's sprite attributes */
static uint8_t nattr, nattr_shown;
static uint16_t s_thresh[SPR_NSIZES];   /* scale between size k - 1 and k */

void spr_setup(void)
{
    uint8_t k;
    /* size k is F0 * 0.8^k pixels per unit: the persp scale (x 4096) halfway
       between sizes in log terms is F0 * 4096 * 0.8^(k - 0.5) */
    uint32_t s = (uint32_t)SPR_F0_X1000 * 4096UL / 1000;
    for (k = 0; k < SPR_NSIZES; k++) {
        s_thresh[k] = (uint16_t)((s * 894) / 1000);         /* 0.8^0.5 = 0.894 */
        s = (s * 4) / 5;
    }
    ZXN_WRITE_REG(0x4B, 0x00);          /* sprite colour 0 is transparent (4-bit: pen 0) */
    nattr_shown = 0;
}

static uint8_t size_of(uint16_t s)
{
    uint8_t k = 0;
    while (k < SPR_NSIZES - 1 && s < s_thresh[k + 1]) k++;
    return k;
}

void spr_frame_begin(const World *w, int s0, int s1)
{
    (void)w; (void)s1;
    nlist = 0;
    s0_frame = s0;
    cover = 0x7FFF;
    memset(seg_top, 0x7F, sizeof(seg_top));
}

/* an object at fine (z, d), lifted by y units, view v, palette offset pal */
static void add(const World *w, uint8_t view, int32_t z, int16_t d, int16_t y, uint8_t pal, uint8_t flags)
{
    Obj2 *o;
    int16_t zr;
    int32_t bx, zu = z >> FBITS;
    uint16_t s;
    if (nlist >= MAX_SPR) return;
    zr = (int16_t)(zu - cam.z);
    if (zr < 24 || zr > DRAW_DIST_UNITS) return;
    bx = road_cx(w, z) + (d >> FBITS) - cam.x - (((int32_t)zr * cam.tan) >> 8);
    if (bx > 8000 || bx < -8000) return;
    s = persp_scale(zr);
    o = &list[nlist];
    o->x = persp_x1((int16_t)bx, 0, s, VIEW_CX);
    if (o->x < -64 || o->x > VIEW_W + 64) return;
    o->y = persp_y1(road_y(w, z) + y, -cam.y, s, cam.horizon * 2);     /* (on screen: full height) */
    o->view = view;
    o->size = size_of(s);
    o->pal = pal;
    o->flags = flags;
    o->zr = zr;
    o->seg = seg_of(z);
    nlist++;
}

void spr_add_world(const World *w, uint8_t kind, int32_t z, int16_t d, int16_t y, uint8_t colour)
{
    (void)kind;                         /* cones only, from render.c */
    add(w, SV_CONE, z, d, y, colour, 0);
}

/* the world's objects and the car (the original's draw_object) */
static void add_objects(const World *w)
{
    uint8_t i;
    const Obj *o = w->obj;
    for (i = 0; i < MAX_OBJS; i++, o++) {
        uint8_t v, pal = o->colour & 3, fl = 0;
        int16_t y = (o->flags & OF_KNOCKED) ? -4 : 0;
        int crossing = o->v == 0 && o->vd != 0;
        switch (o->type) {
        case OBJ_NONE: continue;
        case OBJ_CAR: v = o->v < 0 ? SV_CAR_FRONT : SV_CAR; break;
        case OBJ_TRUCK: v = o->v < 0 ? SV_TRUCK_FRONT : SV_TRUCK; break;
        case OBJ_TRACTOR: v = SV_TRACTOR; break;
        case OBJ_CYCLIST: v = SV_CYCLIST; break;
        case OBJ_DEER: v = crossing ? SV_DEER_SIDE : SV_DEER; break;
        case OBJ_GOAT: v = crossing ? SV_GOAT_SIDE : SV_GOAT; break;
        case OBJ_PED: v = crossing ? SV_PED_SIDE : SV_PED; break;
        case OBJ_CONE: v = SV_CONE; break;
        case OBJ_DEBRIS: v = w->route == 2 ? SV_ROCK : SV_TYRE; break;
        case OBJ_AMBULANCE: v = SV_AMBULANCE; pal = 6 + ((w->frame >> 2) & 1); break;
        default: continue;
        }
        if (crossing && o->vd > 0) fl = SPR_MIRROR_X;          /* crossing to the right */
        add(w, v, o->z, o->d, y, pal, fl);
    }
    if (w->view == CAM_CHASE) {
        const Car *c = &w->car;
        int16_t y = 0, yaw;
        int32_t v = c->v > 8 ? c->v : 8;
        if (c->state == CS_SPLASH || c->state == CS_ROLL)
            y = -(50 - c->timer) * (c->state == CS_SPLASH ? 3 : 6);
        yaw = sc_atan_ratio(c->lat, v) - (int16_t)(cam.yaw - road_dir(w, c->z));
        add(w, yaw > 900 || yaw < -900 ? SV_PLAYER_L : SV_PLAYER, c->z, c->d, y, 4,
            yaw > 900 ? SPR_MIRROR_X : 0);
    }
}

void spr_segment_done(int seg, int16_t road_top)
{
    int k = seg - s0_frame;
    if (k >= 0 && k < 48) seg_top[k] = road_top * 2;      /* (a Layer 2 row: twice on screen) */
}

/* send n patterns from pattern `first` of the banked data into pattern memory
   from 4-bit pattern `slot` (DMA through MMU slot 3) */
static void upload(uint8_t slot, uint16_t first, uint8_t n)
{
    uint32_t off = (uint32_t)first * 128;
    uint16_t len = (uint16_t)n * 128;
    z80_outp(0x303B, (slot >> 1) | (slot & 1) << 7);
    while (len) {
        uint16_t in_page = 8192 - ((uint16_t)off & 8191), part = len < in_page ? len : in_page;
        ZXN_WRITE_REG(0x53, SPR_BANK * 2 + (uint8_t)(off >> 13));
        dma_to_port(0x5B, (const void *)(0x6000 + ((uint16_t)off & 8191)), part);
        off += part;
        len -= part;
    }
}

/* the order (far first: higher sprite numbers are drawn over lower), the
   crests, the patterns into the free half, the attributes for after the flip */
void spr_frame_end(const World *w)
{
    uint8_t order[MAX_SPR], n = 0, i, j, slot, mmu3;
    int16_t lowest;
    int k;

    add_objects(w);
    /* a running minimum of the road's top from the eye outwards */
    lowest = 0x7FFF;
    for (k = 0; k < 48; k++) {
        int16_t t = seg_top[k];
        seg_top[k] = lowest;            /* what is nearer than segment k covers below this */
        if (t < lowest) lowest = t;
    }
    for (i = 0; i < nlist; i++) {
        Obj2 *o = &list[i];
        k = o->seg - s0_frame;
        if (k > 0 && k < 48 && o->y > seg_top[k] + 2) continue;      /* behind a crest */
        /* insert, far first */
        for (j = n; j > 0 && list[order[j - 1]].zr < o->zr; j--) order[j] = order[j - 1];
        order[j] = i;
        n++;
    }
    mmu3 = ZXN_READ_REG(0x53);
    slot = half ? HALF : 0;
    nattr = 0;
    for (i = 0; i < n; i++) {
        const Obj2 *o = &list[order[i]];
        const SprSize *sz = &spr_tab[o->view * SPR_NSIZES + o->size];
        uint8_t cols = SPR_COLS(sz), rows = SPR_ROWS(sz), scale = SPR_SCALE(sz);
        uint8_t np = cols * rows, c, r, sc = 1 << scale, p;
        int16_t x0, y0;
        if (slot + np > (half ? 2 * HALF : HALF) || nattr + np > 128) break;
        upload(slot, sz->first, np);
        x0 = o->x - (o->flags & SPR_MIRROR_X ? (int16_t)(cols * 16 - sz->ax) : sz->ax) * sc + 32;
        y0 = o->y - (int16_t)sz->ay * sc + 32;
        p = slot;
        for (r = 0; r < rows; r++)
            for (c = 0; c < cols; c++, p++) {
                int16_t cx = o->flags & SPR_MIRROR_X ? cols - 1 - c : c;
                int16_t x = x0 + cx * 16 * sc, y = y0 + r * 16 * sc;
                uint8_t *a = attrs + nattr * 5;
                if (x < -16 * sc || x >= 320 || y < -16 * sc || y >= 32 + VIEW_H) continue;
                a[0] = (uint8_t)x;
                a[1] = (uint8_t)y;
                a[2] = o->pal << 4 | (o->flags & SPR_MIRROR_X) | ((x >> 8) & 1);
                a[3] = 0xC0 | (p >> 1);
                a[4] = 0x80 | (p & 1) << 6 | scale << 3 | scale << 1 | ((y >> 8) & 1);
                nattr++;
            }
        slot += np;
    }
    ZXN_WRITE_REG(0x53, mmu3);
    half ^= 1;
}

/* after the flip: the new attributes (and hide the ones not used any more) */
void spr_show_frame(void)
{
    uint8_t i;
    z80_outp(0x303B, 0);
    if (nattr) dma_to_port(0x57, attrs, (uint16_t)nattr * 5);
    for (i = nattr; i < nattr_shown; i++) {
        z80_outp(0x303B, i);
        z80_outp(0x57, 0);
        z80_outp(0x57, 0);
        z80_outp(0x57, 0);
        z80_outp(0x57, 0);
    }
    nattr_shown = nattr;
}

void spr_hide_all(void)
{
    nattr = 0;
    spr_show_frame();
}
