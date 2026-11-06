/*
 * The room on screen: the original's scene.c for the Next's layers.
 *
 *   Layer 2 (320 x 256)  the floor, back walls, blocks, spikes, gates, the
 *                        throne and the front arches, drawn once when you
 *                        enter a room from its pieces in the original's
 *                        order (tools/gen_gfx.py, l2w_blit_rle). The room's
 *                        palette is 1..127, and 129..255 the same at half
 *                        brightness: a shadow is "OR 0x80" on the picture.
 *   sprites              everything that moves (characters, crates, lifts,
 *                        pickups) as groups of 16 x 16 patterns streamed
 *                        each frame by DMA from the frames in banked memory.
 *
 * port: the original composes every frame in software, drawing each item
 * after the items behind it. Here the picture stays put and the sprites are
 * over it, so a sprite is cut where scenery is in front of it: for each
 * moving item, every static item whose screen rectangle meets it and that
 * the original's order puts in front (iso_order: a separating axis, else the
 * depth key) masks its silhouette (the piece's own RLE runs) out of the
 * sprite's patterns (sprg_mask_rle). The moving items among themselves are
 * sorted the same way (iso_sort) and take sprite numbers in that order, so
 * the ones in front are drawn last. Shadows are masked the same way.
 *
 * Work is split in two: scene_prepare() (after the game's logic) works out
 * the sprites, masks and shadows for the next frame, scene_commit() (right
 * after the frame interrupt, at the bottom of the picture) uploads patterns
 * and attributes and changes the shadows, so the picture never shows half
 * an update.
 *
 * MMU: this file is code bank 40 at 0x0000 (main.c maps it).
 * Slot 3 (0x6000) gets the room's blob, slot 2 (0x4000) is the window for
 * frames, Layer 2 (l2w_slot) and the stage, the page where masked sprites
 * are put together. The big tables are initialised data, so they live in
 * bank 40 too and not in the main memory.
 */
#include <string.h>
#include <arch/zxn.h>
#include "keep.h"
#include "scene.h"
#include "gfx_data.h"
#include "nextgfx.h"
#include "l2rle.h"
#include "nextspr.h"
#include "isosort.h"
#include "palfx.h"
#include "fixmath.h"

#define BLOB       ((const uint8_t *)0x6000)
#define STAGE_PAGE 83                  /* bank 41, upper half */
#define WIN_PAGE   10                  /* what slot 2 holds when not drawing (bank 5) */
#define EPS        2                   /* the original's order() eps (4, Q12) */

#define MAXOCC   50                    /* the busiest room has 48 */
#define MAXITEM  ISO_MAX
#define MAXJOB   40
#define MAXSHOW  40
#define MAXSPAN  110

typedef struct {
    IsoBox b;
    const uint8_t *piece;
    int16_t x, y;                      /* the piece's top-left on the screen */
    uint8_t arch;                      /* always in front */
} Occ;

typedef struct {
    uint8_t nfr;
    uint8_t fr[2];                     /* frames (a lift: piston, then the lift) */
    int16_t x, y;                      /* the model's origin on the screen */
    uint8_t shadow;
    int16_t shx, shy;
} Item;

typedef struct { uint8_t slot, n, page; const uint8_t *src; } Job;      /* page 0: the stage */
typedef struct { int16_t x, y; uint8_t cols, rows, slot; } Show;
typedef struct { int16_t x; uint8_t y, len; } Span;

/* the big tables: in bank 40's RAM above its code (SCENE_MEM, checked by
   the Makefile against the bank's end), not in the main memory */
typedef struct {
    Occ occ[MAXOCC];
    uint16_t room_pal[128];            /* the room's colours as loaded (for flashes) */
    IsoBox ibox[MAXITEM];
    Item item[MAXITEM];
    Job job[MAXJOB];
    Show show[MAXSHOW];
    Span span_new[MAXSPAN], span_old[MAXSPAN];
    uint8_t cache_fr[64], cache_slot[64];
    uint8_t front[MAXOCC];
    uint8_t shadow_px[256];
} SceneMem;
#define SCENE_MEM 0x2C00
#define SM ((SceneMem *)SCENE_MEM)
typedef char scene_mem_fits[(SCENE_MEM + sizeof(SceneMem) <= 0x4000) ? 1 : -1];
#define occ (SM->occ)
#define room_pal (SM->room_pal)
#define ibox (SM->ibox)
#define item (SM->item)
#define job (SM->job)
#define show (SM->show)
#define span_new (SM->span_new)
#define span_old (SM->span_old)
#define cache_fr (SM->cache_fr)
#define cache_slot (SM->cache_slot)
#define front (SM->front)

static uint8_t nocc;
static int16_t ox, oy;
static uint8_t blob_page;

static uint8_t nitem, order[MAXITEM];

static uint8_t njob;
static uint8_t nshow;
static uint8_t nspan_new, nspan_old;
static uint16_t stage_used;            /* bytes of the stage page used this frame */
static uint8_t ncache;
uint8_t scene_overflow;                /* items dropped for lack of patterns (tests) */

/* ---- projection (Q11 world -> 320 x 256 screen) ---- */

/* mul16: the Z80N's MUL (fixmath.h); the same numbers as the original's
   ((x - z) * 14) >> 12 in Q12 */
static int16_t sx_of(fix x, fix z) { return ox + (int16_t)(mul16(x - z, ISO_X) >> FXS); }
static int16_t sy_of(fix x, fix y, fix z)
{
    return oy - (int16_t)((mul16(y, ISO_Y) + mul16(x, ISO_Z) + mul16(z, ISO_Z)) >> FXS);
}

static void set_box(IsoBox *b, fix x0, fix y0, fix z0, fix x1, fix y1, fix z1)
{
    iso_set(b, x0, y0, z0, x1, y1, z1, sx_of(x0, z1) - 2, sy_of(x1, y1, z1) - 2, sx_of(x1, z0) + 2,
            sy_of(x0, y0, z0) + 2);
}

/* world.c's body_box (world.c isn't mapped while the scene draws) */
static void box_of(const Body *b, Box *o)
{
    o->x0 = b->px - (b->sx >> 1); o->x1 = o->x0 + b->sx;
    o->y0 = b->py;                o->y1 = b->py + b->sy;
    o->z0 = b->pz - (b->sz >> 1); o->z1 = o->z0 + b->sz;
}

/* ---- palettes ---- */

static uint16_t blob16(uint16_t off) { return BLOB[off] | (uint16_t)BLOB[off + 1] << 8; }

#define BACKDROP RGB9(0, 0, 1)         /* the camera's (0.07, 0.075, 0.1) */

static void load_palettes(uint8_t flash_ink)
{
    static const uint16_t inks[8] = {   /* the original's pens 1..7, a third of each for a flash */
        RGB9(0, 0, 0), RGB9(0, 0, 1), RGB9(1, 0, 0), RGB9(1, 0, 1),
        RGB9(0, 1, 0), RGB9(0, 1, 1), RGB9(1, 1, 0), RGB9(1, 1, 1)
    };
    uint8_t mul = flash_ink ? 7 : 8;
    uint16_t add = flash_ink ? inks[flash_ink & 7] : 0;
    palfx_load_fx(PAL_L2, 0, 128, room_pal, mul, add);
    palfx_load_fx(PAL_L2, 128, 128, room_pal, mul >> 1, add);          /* shadows: half */
    palfx_load_fx(PAL_L2 | PAL_SECOND, 0, 128, room_pal, mul * 3 >> 3, add);   /* the panel: 3/8 */
    palfx_load_fx(PAL_L2 | PAL_SECOND, 128, 128, room_pal, mul * 3 >> 4, add);
    palfx_load_fx(PAL_SPR, 0, 256, spr_pal, mul, add);
    palfx_load_fx(PAL_SPR | PAL_SECOND, 0, 256, spr_pal, mul * 3 >> 3, add);
}

void scene_flash(uint8_t ink) { load_palettes(ink); }

/* ---- a new room ---- */

void scene_room(void)
{
    const RoomGfx *g = &room_gfx[R.level->first_gfx + R.ridx];
    uint16_t pal_off, lut_off, ops_off, nops, i;
    uint8_t np;
    const uint8_t *op;
    blob_page = g->page;
    ZXN_WRITE_REG(0x53, blob_page);
    ox = g->ox;
    oy = g->oy;
    pal_off = blob16(0);
    lut_off = blob16(2);
    ops_off = blob16(4);
    nops = blob16(6);
    np = BLOB[8];
    room_pal[0] = BACKDROP;
    for (i = 0; i < 127; i++) room_pal[1 + i] = blob16(pal_off + 2 * i);
    (void)np;
    load_palettes(0);

    sprg_hide_from(0);
    nshow = 0;
    nspan_old = nspan_new = 0;
    l2w_slot = 2;
    l2w_restore = WIN_PAGE;
    l2w_clear(0);
    nocc = 0;
    for (i = 0, op = BLOB + ops_off; i < nops; i++, op += 10) {
        uint8_t kind = op[2], cx = op[7], cy = op[8], cz = op[9];
        int16_t x = (int16_t)(op[3] | op[4] << 8), y = (int16_t)(op[5] | op[6] << 8);
        const uint8_t *piece = BLOB + blob16(9 + 2 * op[0]);
        fix X = (fix)cx << FXS, Y = (fix)cy << FXS, Z = (fix)cz << FXS;
        Occ *o;
        if (kind == OK_GATE && R.ngate == 0) continue;         /* opened */
        l2w_blit_rle(x, y, piece, op[1] ? BLOB + lut_off + (uint16_t)(op[1] - 1) * 128 : 0);
        if (kind == OK_FLOOR || nocc >= MAXOCC) continue;
        o = &occ[nocc++];
        o->piece = piece;
        o->x = x;
        o->y = y;
        o->arch = kind == OK_ARCH;
        switch (kind) {
        case OK_SPIKES: iso_set(&o->b, X, Y, Z, X + FX, Y + 922, Z + FX, 0, 0, 0, 0); break;   /* 0.45 (1843) */
        case OK_GATE:   iso_set(&o->b, X, Y, Z, X + FX, Y + 2 * FX, Z + FX, 0, 0, 0, 0); break;
        case OK_THRONE: iso_set(&o->b, X + 205, Y, Z + 307, X + 1843, Y + 3400, Z + 1782, 0, 0, 0, 0); break;
        default:        iso_set(&o->b, X, Y, Z, X + FX, Y + FX, Z + FX, 0, 0, 0, 0); break;
        }
        o->b.sx0 = x;                  /* the piece's own rectangle on the screen */
        o->b.sy0 = y;
        o->b.sx1 = x + piece[0] - 1;
        o->b.sy1 = y + piece[1] - 1;
    }
}

/* ---- each frame ---- */

static const uint8_t walk_frame[4] = { 0, 1, 0, 2 };

static Item *add_item(void)
{
    if (nitem >= MAXITEM) { scene_overflow++; return 0; }
    item[nitem].nfr = 0;
    item[nitem].shadow = 0;
    return &item[nitem++];
}

static uint8_t bob_t, spin_t;

static void gather(uint8_t show_player)
{
    uint8_t i;
    Box b;
    Item *it;
    nitem = 0;
    for (i = 0; i < R.nlift; i++) {
        const Lift *l = &R.lift[i];
        const Body *bd = l->body;
        int16_t k = (bd->py - l->base + FX / 4 - 1) / (FX / 4);
        if (!(it = add_item())) return;
        box_of(bd, &b);
        set_box(&ibox[nitem - 1], b.x0, l->base, b.z0, b.x1, b.y1, b.z1);
        if (k > 8) k = 8;
        if (k > 0) it->fr[it->nfr++] = FR_PISTON + (uint8_t)k;
        it->fr[it->nfr++] = FR_LIFT;
        it->x = sx_of(bd->px, bd->pz);
        it->y = sy_of(bd->px, bd->py, bd->pz);
    }
    /* Pickup.Animate: bob sin(3t + phase) * 0.08, spin 140 t + 30 phase degrees;
       the clock's part once a frame, the rest is small numbers */
    if (R.npk) {
        bob_t = (uint8_t)(R.clock * 2444 / 1000);
        spin_t = (uint8_t)(R.clock * 1244 / 10000);
    }
    for (i = 0; i < R.npk; i++) {
        const Pickup *p = &R.pk[i];
        fix x = ((fix)p->x << FXS) + FX / 2, y = (fix)p->y << FXS, z = ((fix)p->z << FXS) + FX / 2;
        uint8_t ph = (uint8_t)((int16_t)(693 * p->x) / 10 + (int16_t)(407 * p->z) / 10 + 55);
        int16_t bob = (int16_t)(mul16(isin256((uint8_t)(bob_t + ph)), 22) >> 14);
        uint8_t f = (uint8_t)(spin_t + (int16_t)(227 * p->x) / 100 + (int16_t)(133 * p->z) / 100 + 2) & 15;
        if (!(it = add_item())) return;
        set_box(&ibox[nitem - 1], p->box.x0, p->box.y0, p->box.z0, p->box.x1, p->box.y1, p->box.z1);
        it->fr[it->nfr++] = p->kind == 'R' ? FR_RELIC + f : p->kind == 'K' ? FR_KEY + f : FR_POTION;
        it->x = sx_of(x, z);
        it->y = sy_of(x, y, z) - bob / 16;
    }
    for (i = 0; i < R.nact; i++) {
        const Actor *a = &R.act[i];
        const Body *bd = a->body;
        uint8_t fr = walk_frame[((a->phase >> 8) + 32) >> 6 & 3], f;
        if (a->kind == K_PLAYER && (!show_player || !a->visible)) continue;
        switch (a->kind) {
        case K_PLAYER:  f = FR_WALKER(0, a->face, fr); break;
        case K_GUARD:   f = FR_WALKER(1, a->face, fr); break;
        case K_HOUND:   f = FR_WALKER(2, a->face, fr); break;
        case K_SAGE:    f = FR_SAGE + a->face; break;
        case K_GHOST:   f = FR_GHOST + a->face; break;
        case K_BOUNCER: f = FR_BOUNCER + (a->roll & 7); break;
        default:        f = FR_CRATE; break;
        }
        if (!(it = add_item())) return;
        box_of(bd, &b);
        set_box(&ibox[nitem - 1], b.x0, b.y0, b.z0, b.x1, b.y1, b.z1);
        it->fr[it->nfr++] = f;
        it->x = sx_of(bd->px, bd->pz);
        it->y = sy_of(bd->px, bd->py, bd->pz);
        if (a->kind != K_CRATE) {
            it->shadow = 1;                          /* room_grounds() found the ground */
            it->shx = it->x;
            it->shy = sy_of(bd->px, a->ground, bd->pz);
        }
    }
}

/* the static items in front of item i, and whether any are */
static uint8_t nfront;

static void find_front(uint8_t i)
{
    uint8_t k;
    nfront = 0;
    const IsoBox *b = &ibox[i];
    int16_t x0 = b->sx0, x1 = b->sx1, y0 = b->sy0, y1 = b->sy1;
    const Occ *o = occ;
    for (k = 0; k < nocc; k++, o++)
        if (o->b.sx0 <= x1 && x0 <= o->b.sx1 && o->b.sy0 <= y1 && y0 <= o->b.sy1 &&
            (o->arch || iso_order(b, &o->b, EPS) > 0))
            front[nfront++] = k;
}

/* patterns for frame f at group top-left (gx, gy): cached when unmasked */
static int16_t patterns(uint8_t f, int16_t gx, int16_t gy, uint8_t *slots_used)
{
    const Frame *fr = &frames[f];
    uint8_t n = fr->cols * fr->rows, k, slot, masked = 0;
    for (k = 0; k < nfront; k++) {
        const Occ *o = &occ[front[k]];
        if (o->b.sx1 >= gx && o->b.sx0 < gx + fr->cols * 16 && o->b.sy1 >= gy && o->b.sy0 < gy + fr->rows * 16) {
            masked = 1;
            break;
        }
    }
    if (!masked)
        for (k = 0; k < ncache; k++)
            if (cache_fr[k] == f) return cache_slot[k];
    if (*slots_used + n > 64 || njob >= MAXJOB) return -1;
    slot = *slots_used;
    *slots_used += n;
    if (!masked) {
        job[njob].page = fr->page;
        job[njob].src = (const uint8_t *)fr->addr;
        if (ncache < 64) { cache_fr[ncache] = f; cache_slot[ncache++] = slot; }
    } else {
        uint16_t off = stage_used;
        if (stage_used + ((uint16_t)n << 8) > 8192) return -1;
        stage_used += (uint16_t)n << 8;
        /* the frame (slot 2) to the stage (slot 3 for a moment) */
        ZXN_WRITE_REG(0x52, fr->page);
        ZXN_WRITE_REG(0x53, STAGE_PAGE);
        dma_copy((uint8_t *)0x6000 + off, (const void *)fr->addr, (uint16_t)n << 8);
        /* then the stage in slot 2, masked by the pieces in the blob (slot 3) */
        ZXN_WRITE_REG(0x52, STAGE_PAGE);
        ZXN_WRITE_REG(0x53, blob_page);
        for (k = 0; k < nfront; k++) {
            const Occ *o = &occ[front[k]];
            sprg_mask_rle((uint8_t *)0x4000 + off, fr->cols, fr->rows, o->x - gx, o->y - gy, o->piece);
        }
        ZXN_WRITE_REG(0x52, WIN_PAGE);
        job[njob].page = STAGE_PAGE;
        job[njob].src = (const uint8_t *)0x4000 + off;
    }
    job[njob].slot = slot;
    job[njob].n = n;
    njob++;
    return slot;
}

/* the shadow under item i: its rows less what's in front, as spans */
static void shadow(uint8_t i)
{
    uint8_t *px = SM->shadow_px;
    const Item *it = &item[i];
    int16_t gx = it->shx - SHADOW_AX, gy = it->shy - SHADOW_AY;
    uint8_t r, k, x, covered = 0;
    for (k = 0; k < nfront && !covered; k++) {
        const Occ *o = &occ[front[k]];
        covered = o->b.sx0 < gx + SHADOW_W && gx <= o->b.sx1 && o->b.sy0 < gy + SHADOW_H && gy <= o->b.sy1;
    }
    if (!covered) {                          /* the plain ellipse: a span a row */
        for (r = 0; r < SHADOW_H && nspan_new < MAXSPAN; r++)
            if (shadow_rows[r][1]) {
                Span *sp = &span_new[nspan_new++];
                sp->x = gx + shadow_rows[r][0];
                sp->y = (uint8_t)(gy + r);
                sp->len = shadow_rows[r][1];
            }
        return;
    }
    memset(px, SPRG_TRANSPARENT, 256);
    for (r = 0; r < SHADOW_H; r++) memset(px + r * 16 + shadow_rows[r][0], 1, shadow_rows[r][1]);
    for (k = 0; k < nfront; k++) {
        const Occ *o = &occ[front[k]];
        sprg_mask_rle(px, 1, 1, o->x - gx, o->y - gy, o->piece);
    }
    for (r = 0; r < SHADOW_H; r++) {
        const uint8_t *row = px + r * 16;
        for (x = 0; x < 16;) {
            uint8_t s;
            if (row[x] != 1) { x++; continue; }
            for (s = x; x < 16 && row[x] == 1; x++) {}
            if (nspan_new < MAXSPAN) {
                Span *sp = &span_new[nspan_new++];
                sp->x = gx + s;
                sp->y = (uint8_t)(gy + r);
                sp->len = x - s;
            }
        }
    }
}

void scene_prepare(uint8_t show_player)
{
    uint8_t i, j, f, slots = 0;
    njob = nshow = nspan_new = ncache = 0;
    stage_used = 0;
    gather(show_player);
    iso_sort(ibox, nitem, order, EPS);
    ZXN_WRITE_REG(0x53, blob_page);                  /* the pieces, for masking */
    for (j = 0; j < nitem; j++) {
        Item *it;
        i = order[j];
        it = &item[i];
        find_front(i);
        if (it->shadow) shadow(i);
        for (f = 0; f < it->nfr; f++) {
            const Frame *fr = &frames[it->fr[f]];
            int16_t gx = it->x - fr->ax, gy = it->y - fr->ay, slot;
            if (!fr->cols || nshow >= MAXSHOW) continue;
            slot = patterns(it->fr[f], gx, gy, &slots);
            if (slot < 0) { scene_overflow++; continue; }
            show[nshow].x = gx;
            show[nshow].y = gy;
            show[nshow].cols = fr->cols;
            show[nshow].rows = fr->rows;
            show[nshow].slot = (uint8_t)slot;
            nshow++;
        }
    }
}

static uint8_t same_span(const Span *a, const Span *b) { return a->x == b->x && a->y == b->y && a->len == b->len; }

/* shadows: take off last frame's spans that changed, put on this frame's
   that changed, and any unchanged one that an old one overlapped */
static void shadows(void)
{
    uint8_t i, k, nundone = 0;
    static uint8_t undone[MAXSPAN];
    for (i = 0; i < nspan_old; i++)
        if (i >= nspan_new || !same_span(&span_old[i], &span_new[i])) {
            l2w_span(span_old[i].x, span_old[i].y, span_old[i].len, 0x7F, 0);
            undone[nundone++] = i;
        }
    for (i = 0; i < nspan_new; i++) {
        const Span *sp = &span_new[i];
        uint8_t draw = i >= nspan_old || !same_span(&span_old[i], sp);
        for (k = 0; !draw && k < nundone; k++) {
            const Span *o = &span_old[undone[k]];
            draw = o->y == sp->y && o->x < sp->x + sp->len && sp->x < o->x + o->len;
        }
        if (draw) l2w_span(sp->x, sp->y, sp->len, 0xFF, 0x80);
    }
    memcpy(span_old, span_new, nspan_new * sizeof(Span));
    nspan_old = nspan_new;
}

void scene_commit(void)
{
    uint8_t i, n = 0;
    const Job *jb;
    const Show *s;
    for (i = 0, jb = job; i < njob; i++, jb++) {
        ZXN_WRITE_REG(0x52, jb->page);
        sprg_upload(jb->slot, jb->src, jb->n);
    }
    ZXN_WRITE_REG(0x52, WIN_PAGE);
    for (i = 0, s = show; i < nshow; i++, s++) n = sprg_show(n, s->x, s->y, s->cols, s->rows, s->slot, 0);
    sprg_hide_from(n);
    shadows();
    njob = 0;
}

void scene_hide(void)
{
    uint8_t i;
    sprg_hide_from(0);
    for (i = 0; i < nspan_old; i++) l2w_span(span_old[i].x, span_old[i].y, span_old[i].len, 0x7F, 0);
    nspan_old = nspan_new = 0;
    nshow = njob = 0;
}
