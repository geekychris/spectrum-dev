/*
 * render.c - Block Breaker 3D's picture on the Spectrum Next (the original's
 * render.c).
 *
 *   The tunnel is a 320 x 256 Layer 2 picture made on the host from the
 *   original's camera and palette (tools/gen_gfx.py), so it costs nothing
 *   to show. Its walls are cut into depth bands with their own palette
 *   entries: the green ring that shows how far away the ball is, a set of
 *   quads in the original, is the ball's band turned green.
 *
 *   The bricks are drawn into Layer 2 by l2wpoly.h, with the original's
 *   rules for which faces show (the front, and the sides and tops facing
 *   the camera with no brick against them) and its draw order (far layers
 *   first, a layer's sides before its fronts). Only where a brick changed is
 *   redrawn: its box on the screen goes back to the clean tunnel (a copy in
 *   banks 25-29) and every brick face there is drawn again. Each pixel also
 *   gets its face's depth (the original's sort key) in a depth plane.
 *
 *   The ball, its shadow, the capsules, the bits of broken bricks and the
 *   paddle are hardware sprites with 4-bit patterns, one per size by depth
 *   (nextsprl.h). Where one is deeper than a brick face drawn over it, its
 *   pattern is masked against the depth plane, so the ball goes behind
 *   bricks as it did in the original's painter's algorithm.
 *
 *   This file and its tables are in bank 41, mapped at 0x0000 while drawing
 *   (memmap.h); Layer 2 and sprite patterns are reached through MMU slot 2,
 *   the depth plane and the clean tunnel through slot 3.
 *
 *   port: the camera stays at (0, 0, -320); the original leans it with the
 *   paddle (cam = paddle / 5), which would mean redrawing the whole tunnel.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "l2wpoly.h"
#include "nextsprl.h"
#include "fixmath.h"
#include "palfx.h"
#include "breaker.h"
#include "gfx_data.h"
#include "memmap.h"

#define L2_PAGE    (L2_BANK * 2)        /* 40: Layer 2, the tunnel and the bricks */
#define CLEAN_BANK 25                   /* the tunnel without bricks, banks 25-29 */
#define CLEAN_PAGE (CLEAN_BANK * 2)
#define Z_PAGE     60                   /* the depth plane, banks 30-34 */
#define SPR_PAGE   70                   /* the sprite patterns (bank 35) */

/* palette (render.c's) */
#define PEN_BRICK  11
#define PEN_TOUGH  23
#define PEN_STEEL  25
#define PEN_CAP    29
#define PEN_RING   30
#define PEN_FLOOR  5
#define PEN_CEIL   7
#define PEN_SIDE   9

#define KEY(z)     ((z) - 600)          /* a depth key byte: the original's sort depth - 600 */
#define S0         3456                 /* 270 * 4096 / 320: the paddle's plane, z = 0 */

uint16_t scene_lines;

int brick_pen(int type, int hits)
{
    if (type >= BK_RED && type <= BK_BLUE) return PEN_BRICK + 2 * (type - BK_RED);
    if (type == BK_TOUGH) return hits > 1 ? PEN_TOUGH : PEN_TOUGH + 1;
    if (type == BK_STEEL) return PEN_STEEL;
    return PEN_CAP;
}

static uint8_t brick_shade(int type, int hits)
{
    if (type == BK_TOUGH && hits <= 1) return 3;
    if (type == BK_BONUS) return PEN_BRICK + 11;
    return (uint8_t)(brick_pen(type, hits) + 1);
}

/* ------------------------------------------------------------ the bricks */

typedef struct { int16_t x0, y0, x1, y1; } Box;
static Box cell_box[BB_CELLS];          /* each cell's box on the screen */
static uint8_t shown[BB_CELLS];         /* the pen each cell was drawn with (0: empty) */
static int16_t dx0 = 32767, dy0, dx1, dy1;   /* the dirty rectangle (dx0 > dx1: none) */
static const BBGame *cur;
static int16_t zlo, zhi, zlo_y, zhi_y;  /* the brick block's box on the screen */

/* layer l's depths in bz_tab: the front, its face (1 nearer), the back */
#define ZI_FACE(l)  ((l) * 2)
#define ZI_FRONT(l) ((l) * 2 + 1)
#define ZI_BACK(l)  ((l) == 2 ? 6 : (l) * 2 + 3)

static uint8_t empty_at(int8_t c, int8_t r, int8_t l)
{
    if (c < 0 || c >= BB_COLS || r < 0 || r >= BB_ROWS || l < 0 || l >= BB_LAYERS) return 1;
    return cur->cell[bb_index(c, r, l)] == BK_EMPTY;
}

static int16_t xy[8];

static void quad(uint8_t za, uint8_t zb, uint8_t xa, uint8_t xb, uint8_t ya, uint8_t yb, uint8_t vertical, uint8_t pen)
{
    if (vertical) {                     /* x = bx_tab[xa], y from ya to yb, z from za to zb */
        xy[0] = sx_tab[za][xa]; xy[1] = sy_tab[za][ya];
        xy[2] = sx_tab[za][xa]; xy[3] = sy_tab[za][yb];
        xy[4] = sx_tab[zb][xa]; xy[5] = sy_tab[zb][yb];
        xy[6] = sx_tab[zb][xa]; xy[7] = sy_tab[zb][ya];
    } else {                            /* y = by_tab[ya], x from xa to xb */
        xy[0] = sx_tab[za][xa]; xy[1] = sy_tab[za][ya];
        xy[2] = sx_tab[za][xb]; xy[3] = sy_tab[za][ya];
        xy[4] = sx_tab[zb][xb]; xy[5] = sy_tab[zb][ya];
        xy[6] = sx_tab[zb][xa]; xy[7] = sy_tab[zb][ya];
    }
    l2wp_poly(xy, 4, pen);
}

/* cell i's sides (pass 0) or front (pass 1), as render.c's bricks() */
static void cell_faces(uint8_t i, uint8_t pass)
{
    uint8_t t = cur->cell[i], h = cur->hits[i], lit, sh;
    int8_t c = i % BB_COLS, r = (i / BB_COLS) % BB_ROWS, l = i / (BB_COLS * BB_ROWS);
    uint8_t zf = ZI_FRONT(l), zb = ZI_BACK(l);
    lit = (uint8_t)brick_pen(t, h);
    sh = brick_shade(t, h);
    if (pass) {
        if (!empty_at(c, r, l - 1)) return;
        l2wp_key = KEY(bz_tab[zf]);
        l2wp_rect(sx_tab[zf][c], sy_tab[zf][r + 1], sx_tab[zf][c + 1], sy_tab[zf][r], sh);    /* the rim */
        l2wp_key--;
        zf = ZI_FACE(l);
        l2wp_rect(sx_tab[zf][7 + c], sy_tab[zf][9 + r], sx_tab[zf][13 + c], sy_tab[zf][5 + r], lit);   /* the face */
        return;
    }
    l2wp_key = KEY(bz_tab[zf] + BB_CD / 2);
    if (c >= 4 && empty_at(c - 1, r, l)) quad(zf, zb, c, 0, r, r + 1, 1, sh);
    if (c <= 1 && empty_at(c + 1, r, l)) quad(zf, zb, c + 1, 0, r, r + 1, 1, sh);
    if (r == 0 && empty_at(c, r + 1, l)) quad(zf, zb, c, c + 1, r + 1, 0, 0, lit);
    if (r == 3 && empty_at(c, r - 1, l)) quad(zf, zb, c, c + 1, r, 0, 0, sh);
}

/* the scene back in a rectangle of Layer 2: the clean tunnel, then every
   brick face there, far to near */
static void redraw(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int8_t l;
    uint8_t pass, i;
    l2wp_copy(L2_PAGE, CLEAN_PAGE, x0, y0, x1, y1);
    /* the depth plane only holds bricks: clear it where they can be */
    l2wp_fill(Z_PAGE, x0 > zlo ? x0 : zlo, y0 > zlo_y ? y0 : zlo_y, x1 < zhi ? x1 : zhi, y1 < zhi_y ? y1 : zhi_y, 0xFF);
    l2wp_target(L2_PAGE, Z_PAGE);
    l2wp_clip(x0, y0, x1, y1);
    for (l = BB_LAYERS - 1; l >= 0; l--)
        for (pass = 0; pass < 2; pass++)
            for (i = l * BB_COLS * BB_ROWS; i < (l + 1) * BB_COLS * BB_ROWS; i++) {
                const Box *b = &cell_box[i];
                if (cur->cell[i] == BK_EMPTY || b->x0 >= x1 || b->x1 <= x0 || b->y0 >= y1 || b->y1 <= y0) continue;
                cell_faces(i, pass);
            }
    l2wp_clip(0, 0, 320, 256);
}

void scene_dirty(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (dx0 > dx1) { dx0 = x0; dy0 = y0; dx1 = x1; dy1 = y1; return; }
    if (x0 < dx0) dx0 = x0;
    if (y0 < dy0) dy0 = y0;
    if (x1 > dx1) dx1 = x1;
    if (y1 > dy1) dy1 = y1;
}

void scene_level(void)
{
    memset(shown, 0xFF, sizeof(shown));                 /* every cell differs */
}

/* cells whose look changed since they were drawn: their boxes are dirty */
static void bricks_changed(void)
{
    uint8_t i, p;
    for (i = 0; i < BB_CELLS; i++) {
        p = cur->cell[i] == BK_EMPTY ? 0 : (uint8_t)brick_pen(cur->cell[i], cur->hits[i]);
        if (p != shown[i]) {
            shown[i] = p;
            scene_dirty(cell_box[i].x0, cell_box[i].y0, cell_box[i].x1, cell_box[i].y1);
        }
    }
}

/* ------------------------------------------------------------ the ring */

static uint8_t ring_band = 0xFF;

static void l2pal(uint8_t idx, uint16_t c)
{
    ZXN_WRITE_REG(0x43, 0x10);                          /* Layer 2's first palette */
    ZXN_WRITE_REG(0x40, idx);
    ZXN_WRITE_REG(0x44, (uint8_t)(c >> 1));
    ZXN_WRITE_REG(0x44, c & 1);
}

static void band_colours(uint8_t b, uint8_t lit)
{
    uint8_t s = band_par[b], k, pen = FIRST_BAND_PEN + 3 * b;
    static const uint8_t base[3] = { PEN_FLOOR, PEN_CEIL, PEN_SIDE };
    for (k = 0; k < 3; k++) l2pal(pen + k, lit ? l2_pal[PEN_RING] : l2_pal[base[k] + s]);
}

/* the ring round the walls at depth z (render.c's four PEN_RING quads);
   0x7FFF: none */
static void ring(int16_t z)
{
    uint8_t b = z == 0x7FFF ? 0xFF : zt_band[(uint16_t)(z - ZT_MIN) >> 1];
    if (b == ring_band) return;
    if (ring_band != 0xFF) band_colours(ring_band, 0);
    if (b != 0xFF) band_colours(b, 1);
    ring_band = b;
}

/* ------------------------------------------------------------ sprites */

static uint8_t dyn;                     /* masked patterns made this frame */
static uint8_t mbuf[128];

static int16_t clampz(int32_t z)
{
    int16_t v = (int16_t)(z / BB_FX);
    return v < ZT_MIN ? ZT_MIN : v > ZT_MAX ? ZT_MAX : v;
}

static int16_t scale_of(int16_t z) { return (int16_t)zt_scale[(uint16_t)(z - ZT_MIN) >> 1]; }

/* x (world units) at scale s: screen offset from the centre */
static int16_t proj(int16_t v, int16_t s) { return (int16_t)(mul16(v, s) >> 12); }

/* one 16 x 16 sprite at (x, y) whose depth key is `key`: masked against the
   bricks drawn nearer than it when it can be behind one */
static void put(int16_t x, int16_t y, uint8_t pat, uint8_t pal, uint8_t flags, int16_t key)
{
    if (key > 19 && x + 16 > zlo && x < zhi && y + 16 > zlo_y && y < zhi_y && dyn < 128 - SPR_DYN) {
        uint8_t slot = SPR_DYN + dyn;
        uint16_t left;
        ZXN_WRITE_MMU2(SPR_PAGE + (pat >> 6));
        left = spl_zmask(mbuf, (const uint8_t *)0x4000 + ((uint16_t)(pat & 63) << 7), x, y,
                         key > 255 ? 255 : (uint8_t)key, Z_PAGE);
        ZXN_WRITE_MMU2(TILE_PAGE);
        if (!left) return;                              /* wholly behind bricks */
        spl_patterns(slot, mbuf, 1);
        dyn++;
        pat = slot;
    }
    spl_add(x, y, pat, pal, flags);
}

static uint8_t depth_at(int16_t x, int16_t y)
{
    uint8_t v;
    if (x < zlo || x >= zhi || y < zlo_y || y >= zhi_y) return 0xFF;
    ZXN_WRITE_MMU3(Z_PAGE + (x >> 5));
    v = *(uint8_t *)(0x6000 + ((uint16_t)(x & 31) << 8) + (uint8_t)y);
    ZXN_WRITE_MMU3(HUD_PAGE);
    return v;
}

static int32_t mid(int32_t a, int32_t b, uint8_t half) { return half ? (a + b) >> 1 : b; }

static void draw_capsule(const BBGame *g, uint8_t k, uint8_t half)
{
    const Capsule *c = &g->cap[k];
    int16_t z = clampz(mid(before.cz[k], c->z, half)), s = scale_of(z), x, y;
    const CapCls *cc = &cap_cls[zt_cap[(uint16_t)(z - ZT_MIN) >> 2]];
    uint8_t t;
    x = 160 + proj((int16_t)(mid(before.cx[k], c->x, half) / BB_FX), s) - cc->half;
    y = 128 - proj((int16_t)(mid(before.cy[k], c->y, half) / BB_FX), s) - cc->half;
    for (t = 0; t < cc->n; t++)
        put(x + (t & 1) * 16, y + (t >> 1) * 16, cc->pat + t, SPAL_CAP + c->type, 0, KEY(z));
}

void scene_draw(const BBGame *g, uint8_t half)
{
    uint8_t k, order[BB_MAX_CAPS + 1], n = 0, ball_shown, wide;
    int16_t bz = 0, bxs = 0, bys = 0, s, px, py;
    uint16_t t0 = ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);

    cur = g;
    bricks_changed();
    if (dx0 <= dx1) {
        redraw(dx0, dy0, dx1, dy1);
        dx0 = 32767;
        dx1 = -32767;
    }

    ball_shown = !(g->between && g->stuck);
    if (ball_shown) {
        bz = clampz(mid(before.bz, g->bz, half));
        s = scale_of(bz);
        bxs = 160 + proj((int16_t)(mid(before.bx, g->bx, half) / BB_FX), s);
        bys = 128 - proj((int16_t)(mid(before.by, g->by, half) / BB_FX), s);
    }
    ring(ball_shown ? bz : 0x7FFF);

    dyn = 0;
    spl_begin();
    /* the shadow on the floor (render.c: x +-9, z +-9 at y = -BB_H + 2) */
    if (ball_shown)
        put(bxs - 8, 128 + proj(BB_H - 2, scale_of(bz)) - 8, SPR_SHADOW + zt_shadow[(uint16_t)(bz - ZT_MIN) >> 2], 0, 0,
            KEY(bz + 30));
    /* the bits of broken bricks: hidden when their middle is behind a brick */
    for (k = 0; k < MAX_PARTS; k++) {
        const Part *p = &br.part[k];
        int16_t z, x, y;
        if (p->life <= 0) continue;
        z = p->z >> 4;
        if (z < ZT_MIN) z = ZT_MIN;
        s = scale_of(z);
        x = 160 + proj(p->x >> 4, s);
        y = 128 - proj(p->y >> 4, s);
        if (KEY(z - 2) > 19 && depth_at(x, y) < KEY(z - 2)) continue;
        spl_add(x - 8, y - 8, SPR_PART + 2 * zt_part[(uint16_t)(z - ZT_MIN) >> 2] + p->shade, SPAL_BRICK + p->type - 1, 0);
    }
    /* the capsules and the ball, far to near (the original sorts them) */
    for (k = 0; k < BB_MAX_CAPS; k++) if (g->cap[k].active) order[n++] = k;
    if (ball_shown && bz >= -8) order[n++] = BB_MAX_CAPS;
    for (k = 1; k < n; k++) {                           /* insertion sort, deepest first */
        uint8_t j = k, v = order[k];
        int32_t zv = v == BB_MAX_CAPS ? g->bz : g->cap[v].z;
        while (j > 0 && (order[j - 1] == BB_MAX_CAPS ? g->bz : g->cap[order[j - 1]].z) < zv) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
    for (k = 0; k < n; k++) {
        if (order[k] == BB_MAX_CAPS)
            put(bxs - 8, bys - 8, SPR_BALL + zt_ball[(uint16_t)(bz - ZT_MIN) >> 2], g->plough_t ? SPAL_PLOUGH : 0, 0, KEY(bz - 2));
        else draw_capsule(g, order[k], half);
    }
    /* the paddle's frame at z = 0, nearer than everything but a ball that
       got past it */
    px = 160 + proj((int16_t)(mid(before.px, g->px, half) / BB_FX), S0);
    py = 128 - proj((int16_t)(mid(before.py, g->py, half) / BB_FX), S0);
    wide = g->wide_t != 0;
    {
        const PadTile *t = wide ? wide_tiles : pad_tiles;
        uint8_t nt = wide ? WIDE_TILES : PADDLE_TILES, pal = g->plough_t && (g->tick & 2) ? SPAL_PLOUGH : 0;
        for (k = 0; k < nt; k++, t++) spl_add(px + t->dx, py + t->dy, t->pat, pal, t->flags);
    }
    if (ball_shown && bz < -8)
        spl_add(bxs - 8, bys - 8, SPR_BALL + zt_ball[(uint16_t)(bz - ZT_MIN) >> 2], g->plough_t ? SPAL_PLOUGH : 0, 0);
    /* the capsules' letters, over everything (render.c's capsule_letters) */
    for (k = 0; k < BB_MAX_CAPS; k++) {
        const Capsule *c = &g->cap[k];
        int16_t z;
        if (!c->active) continue;
        z = clampz(mid(before.cz[k], c->z, half));
        s = scale_of(z);
        spl_add(160 + proj((int16_t)(mid(before.cx[k], c->x, half) / BB_FX), s) - 8,
                128 - proj((int16_t)(mid(before.cy[k], c->y, half) / BB_FX), s) - 8, SPR_LETTER + c->type, 0, 0);
    }
    spl_end();
    {
        uint16_t t1 = ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);
        scene_lines = t1 >= t0 ? t1 - t0 : t1 + 312 - t0;
    }
}

void scene_sprites_off(void)
{
    spl_begin();
    spl_end();
    ring(0x7FFF);
}

void scene_blank(void)
{
    scene_sprites_off();
    l2wp_target(L2_PAGE, 0);
    l2wp_rect(0, 0, 320, 256, 0);
    memset(shown, 0xFF, sizeof(shown));
    scene_dirty(0, 0, 320, 256);
}

void scene_box(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t edge)
{
    l2wp_target(L2_PAGE, 0);
    l2wp_rect(x0, y0, x1 + 1, y1 + 1, 2);               /* render.c's g_box: fill 2, edge */
    l2wp_rect(x0, y0, x1 + 1, y0 + 1, edge);
    l2wp_rect(x0, y1, x1 + 1, y1 + 1, edge);
    l2wp_rect(x0, y0, x0 + 1, y1 + 1, edge);
    l2wp_rect(x1, y0, x1 + 1, y1 + 1, edge);
}

/* ------------------------------------------------------------ start */

void scene_init(void)
{
    uint8_t i;
    palfx_load(PAL_L2, 0, 256, l2_pal);
    palfx_load(PAL_SPR, 0, 256, spr_pal);
    l2w_init(L2_BANK);                  /* main() has made the clean copy and cleared the depth plane */
    l2wp_slots(WIN_T, TILE_PAGE, WIN_Z, HUD_PAGE);
    spl_init();
    ZXN_WRITE_MMU2(SPR_PAGE);
    spl_patterns(0, (const uint8_t *)0x4000, 64);
    ZXN_WRITE_MMU2(SPR_PAGE + 1);
    spl_patterns(64, (const uint8_t *)0x4000, SPR_NPAT > 64 ? SPR_NPAT - 64 : 0);
    ZXN_WRITE_MMU2(TILE_PAGE);
    zlo = 32767; zhi = -32767; zlo_y = 32767; zhi_y = -32767;
    for (i = 0; i < BB_CELLS; i++) {
        int8_t c = i % BB_COLS, r = (i / BB_COLS) % BB_ROWS, l = i / (BB_COLS * BB_ROWS);
        uint8_t zf = ZI_FRONT(l), zb = ZI_BACK(l);
        Box *b = &cell_box[i];
        /* the front is nearer, so bigger: its corners bound the box on the
           outside, the back's on the inside */
        b->x0 = sx_tab[zf][c] < sx_tab[zb][c] ? sx_tab[zf][c] : sx_tab[zb][c];
        b->x1 = (sx_tab[zf][c + 1] > sx_tab[zb][c + 1] ? sx_tab[zf][c + 1] : sx_tab[zb][c + 1]) + 1;
        b->y0 = sy_tab[zf][r + 1] < sy_tab[zb][r + 1] ? sy_tab[zf][r + 1] : sy_tab[zb][r + 1];
        b->y1 = (sy_tab[zf][r] > sy_tab[zb][r] ? sy_tab[zf][r] : sy_tab[zb][r]) + 1;
        if (b->x0 < zlo) zlo = b->x0;
        if (b->x1 > zhi) zhi = b->x1;
        if (b->y0 < zlo_y) zlo_y = b->y0;
        if (b->y1 > zhi_y) zhi_y = b->y1;
    }
    scene_level();
}
