/*
 * sprites.c - turrets, drones, pods, bolts, explosions, the guardian and the
 * sight as hardware sprites.
 *
 * port: the original draws these as polygon models and camera-facing shapes
 * with the canyon (render.c targets, guardian, burst). Here each is drawn on
 * the host from the same models (tools/gen_sprites.py) at a ladder of sizes;
 * the nearest size is chosen, the biggest scaled by the sprite hardware. The
 * patterns are streamed from banks 40.. into one half of the sprite pattern
 * memory while the other half is on screen (spr_end) and the attributes are
 * sent after the flip (spr_show), so nothing tears. Sprites are over Layer
 * 2: one behind a wall at a bend shows (the original's shots ignore the
 * walls too: what is seen can be hit). Places through the canyon's camera
 * (view.asm's xform16), so they sit where the canyon is drawn.
 */
#include <stdint.h>
#include <z80.h>
#include <arch/zxn.h>
#include "cr.h"
#include "render.h"
#include "sprites.h"
#include "persp.h"
#include "nextgfx.h"
#include "palfx.h"
#include "front.h"

#define MAX_OBJ 32
#define HALF    64                      /* 4-bit patterns in each half of the pattern memory */

typedef struct { int16_t x, y, z; } P3;
extern void xform16(int16_t dx, int16_t dy, int16_t dz, P3 *o);     /* view.asm */
extern A3Cam render_cam;                /* render.c: this frame's camera */

typedef struct { uint8_t view, frame, k; int16_t x, y, z; } Obj;
static Obj obj[MAX_OBJ];
static uint8_t nobj;
static uint8_t half;
static uint8_t attrs[128 * 5];
static uint8_t nattr, nattr_shown;
static uint32_t thresh[SPR_NSIZES], bthresh[SPR_NSIZES];   /* scale between size k - 1 and k */

void spr_setup(void)
{
    static uint16_t pal[16];
    uint32_t s = (uint32_t)SPR_F0_X1000 * 4096UL / 1000, b = (uint32_t)SPR_BURST_F0_X1000 * 4096UL / 1000;
    uint8_t k;
    for (k = 0; k < SPR_NSIZES; k++) {
        /* size k is F0 * 0.8^k pixels a unit: halfway (in log terms) to the
           next is 0.8^0.5 = 0.894 of it */
        thresh[k] = (s * 894) / 1000;
        bthresh[k] = (b * 894) / 1000;
        s = (s * 4) / 5;
        b = (b * 4) / 5;
    }
    for (k = 0; k < 16; k++) {
        uint16_t c = spr_pal12[k];
        pal[k] = RGB9((((c >> 8) & 15) * 7 + 7) / 15, (((c >> 4) & 15) * 7 + 7) / 15, ((c & 15) * 7 + 7) / 15);
    }
    palfx_load(PAL_SPR, 0, 16, pal);
    ZXN_WRITE_REG(0x4B, 0x00);          /* 4-bit sprites: pen 0 is transparent */
    nattr_shown = 0;
}

void spr_begin(void)
{
    nobj = 0;
}

static void add(uint8_t view, uint8_t frame, uint8_t k, int16_t x, int16_t y, int16_t z)
{
    const SprView *v = &spr_view[view];
    Obj *o;
    if (nobj >= MAX_OBJ) return;
    if (k < v->k0) k = v->k0;
    if (k >= v->k0 + v->nk) k = v->k0 + v->nk - 1;
    o = &obj[nobj++];
    o->view = view;
    o->frame = frame;
    o->k = k - v->k0;
    o->x = x;
    o->y = y;
    o->z = z;
}

void spr_world(uint8_t view, uint8_t frame, int32_t wx, int32_t wy, int32_t wz, uint8_t rmul)
{
    P3 p;
    uint16_t s;
    uint32_t f;
    uint8_t k = 0;
    int32_t dx = wx - render_cam.x, dy = wy - render_cam.y, dz = wz - render_cam.z;
    if (dx < -8000 || dx > 8000 || dz < -100 || dz > 8000) return;
    xform16((int16_t)dx, (int16_t)dy, (int16_t)dz, &p);
    if (p.z < 16) return;
    s = persp_scale((uint16_t)p.z);
    if (s > 32767) s = 32767;
    if (rmul > 1) {
        f = (uint32_t)s * rmul;
        while (k < SPR_NSIZES - 1 && f < bthresh[k + 1]) k++;
    } else
        while (k < SPR_NSIZES - 1 && s < thresh[k + 1]) k++;
    add(view, frame, k, VIEW_W / 2 + persp_mul(p.x, s), VIEW_H / 2 - persp_mul(p.y, s - (s >> 2)), p.z);
}

void spr_screen(uint8_t view, int16_t x, int16_t y)
{
    add(view, 0, 0, x, y, 0);
}

/* n patterns from pattern `first` of the banked data into pattern memory from
   4-bit pattern `slot` (DMA through MMU slot 3) */
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

void spr_end(void)
{
    uint8_t order[MAX_OBJ], n = 0, i, j, slot, mmu3;
    for (i = 0; i < nobj; i++) {        /* far first (later sprites are over earlier ones); the sight, z 0, last */
        for (j = n; j > 0 && obj[order[j - 1]].z < obj[i].z; j--) order[j] = order[j - 1];
        order[j] = i;
        n++;
    }
    mmu3 = ZXN_READ_REG(0x53);
    slot = half ? HALF : 0;
    nattr = 0;
    for (i = 0; i < n; i++) {
        const Obj *o = &obj[order[i]];
        const SprView *v = &spr_view[o->view];
        const SprSize *sz = &spr_tab[v->at + (uint16_t)o->frame * v->nk + o->k];
        uint8_t cols = SPR_COLS(sz), rows = SPR_ROWS(sz), scale = SPR_SCALE(sz);
        uint8_t np = cols * rows, c, r, sc = 1 << scale, p;
        int16_t x0, y0;
        if (slot + np > (half ? 2 * HALF : HALF) || nattr + np > 128) break;
        upload(slot, sz->first, np);
        x0 = o->x - (int16_t)sz->ax * sc + 32;
        y0 = o->y - (int16_t)sz->ay * sc + 32;
        p = slot;
        for (r = 0; r < rows; r++)
            for (c = 0; c < cols; c++, p++) {
                int16_t x = x0 + c * 16 * sc, y = y0 + r * 16 * sc;
                uint8_t *a = attrs + nattr * 5;
                if (x < -16 * sc || x >= 320 || y < -16 * sc || y >= 256) continue;
                a[0] = (uint8_t)x;
                a[1] = (uint8_t)y;
                a[2] = (uint8_t)((x >> 8) & 1);
                a[3] = 0xC0 | (p >> 1);
                a[4] = 0x80 | (p & 1) << 6 | scale << 3 | scale << 1 | ((y >> 8) & 1);
                nattr++;
            }
        slot += np;
    }
    ZXN_WRITE_REG(0x53, mmu3);
    half ^= 1;
}

void spr_present(void)
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
    spr_present();
}

/* ---------- the objects: everything but the canyon, as sprites ---------- */
void spr_objects(const CRGame *g)
{
    int k;
    int32_t x, y, z;
    spr_begin();
    if (rl.state == RS_PLAY || rl.state == RS_OVER) spr_screen(SV_SIGHT, SX(g->sx), SY(g->sy));
    for (k = 0; k < MAX_BURSTS; k++) {
        const Burst *b = &rl.burst[k];
        if (b->t > 0) spr_world(SV_BURST, (uint8_t)(b->t - 1), b->x, b->y, b->z, (uint8_t)(b->size * (4 + b->t) / 14));
    }
    for (k = 0; k < CR_MAX_BOLTS; k++) {
        if (!g->bolt[k].alive) continue;
        cr_bolt_pos(g, k, &x, &y, &z);
        spr_world(SV_BOLT, 0, x, y, z, 1);
    }
    for (k = 0; k < g->ntg; k++) {
        const Target *t = &g->tg[k];
        int32_t ahead = t->s - g->cam_s;
        if (!t->alive || ahead < 20 || ahead > 1300) continue;
        cr_target_pos(g, k, &x, &y, &z);
        spr_world(t->type == TG_TURRET ? SV_TURRET : t->type == TG_DRONE ? SV_DRONE : SV_POD, 0, x, y, z, 1);
    }
    if (g->cam_s + 1300 > CR_LEN) {
        int32_t bs = CR_LEN + 450;
        for (k = 0; k < CR_NODES; k++) {
            if (g->node_hp[k] <= 0) continue;
            cr_node_pos(g, k, &x, &y, &z);
            spr_world(SV_NODE, (uint8_t)((g->boss_angle >> 13) & 7), x, y, z - 30, 1);
        }
        spr_world(SV_CORE, (uint8_t)((g->boss_angle / 3 >> 13) & 7), cr_path_x(bs), cr_path_y(bs) + 90, bs, 1);
    }
    spr_end();
}
