/*
 * The view: the course picture scrolled under the camera, and everything
 * that moves as hardware sprites.
 *
 * port: the original projects every face each frame (render.c) at any yaw,
 * pitch and zoom. Here the camera keeps the original's default view (yaw 0,
 * pitch 35 degrees, 11.4 pixels a unit), so a course is one big picture
 * drawn on the host from the original's own geometry, lighting and fog
 * (tools/gen_course.py). Layer 2 (320 x 256) holds the part around the
 * camera as a ring: the hardware scroll shows it, and a move fills only the
 * columns and rows coming into view (l2view.asm). The camera's focus is
 * game.c's, so it follows, leads and shakes as in the original.
 */
#include <string.h>
#include <arch/zxn.h>
#include <z80.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nextsprl.h"
#include "zxlog.h"
#include "game.h"
#include "view.h"
#include "data.h"
#include "sprite_data.h"
#include "palfx.h"
#include "hud.h"

/* l2view.asm */
extern void lv_setup(uint8_t page);
extern void lv_col(uint16_t c, int16_t x, int16_t y0);
extern void lv_row(uint16_t c0, int16_t x0, int16_t y);
extern int16_t lv_w, lv_h, lv_ox, lv_oy;
extern uint8_t lv_void, lv_page;

int16_t view_x, view_y;                  /* the picture's pixel at the screen's top left */
static uint8_t shown = 0xFF;             /* course whose picture is up */
uint16_t view_cols, view_rows;           /* filled since the start (perf) */

/* port: the projection of render.c's proj() at the default view, in
   picture pixels: x = p.x * 2925 >> 20, y = -(p.y * 3355 + p.z * 2349) * 2925 >> 32
   (the camera basis and scale of cam_basis(); 32-bit safe, within a pixel
   of the host's) */
void view_project(const V3 *p, int16_t *x, int16_t *y)
{
    long t, a;
    a = (p->x >> 4) * 2925L;
    *x = (int16_t)(a >> 16) - lv_ox;
    t = (p->y >> 2) * 3355L;
    a = (p->z >> 2) * 2349L;
    t = (t + a) >> 10;                   /* the view's up, Q12 */
    a = (t >> 4) * 2925L;
    *y = -(int16_t)(a >> 16) - lv_oy;
}

static uint16_t ring_col(int16_t x)      /* Layer 2 column of picture column x */
{
    int16_t c = x % 320;
    return (uint16_t)(c < 0 ? c + 320 : c);
}

static void scroll_regs(void)
{
    uint16_t c = ring_col(view_x);
    ZXN_WRITE_REG(0x16, c & 255);
    ZXN_WRITE_REG(0x71, c >> 8);
    ZXN_WRITE_REG(0x17, (uint8_t)view_y);
}

static void redraw(void)
{
    int16_t x;
    for (x = view_x; x < view_x + 320; x++) lv_col(ring_col(x), x, view_y);
    view_cols += 320;
}

void view_course(uint8_t level)
{
    lv_setup(pic_page[level]);
    shown = level;
}

/* the camera at picture pixel (cx, cy): the screen's centre */
void view_goto(int16_t cx, int16_t cy)
{
    int16_t nx = cx - 160, ny = cy - 128, x;
    int16_t dx = nx - view_x, dy = ny - view_y;
    if (dx > 64 || dx < -64 || dy > 64 || dy < -64) {
        view_x = nx;
        view_y = ny;
        redraw();
        scroll_regs();
        return;
    }
    /* new columns first (at the old rows), then new rows across the new columns */
    if (dx > 0) {
        for (x = view_x + 320; x < nx + 320; x++) lv_col(ring_col(x), x, view_y);
    } else if (dx < 0) {
        for (x = nx; x < view_x; x++) lv_col(ring_col(x), x, view_y);
    }
    view_cols += dx < 0 ? -dx : dx;
    view_x = nx;
    if (dy > 0) {
        for (x = view_y + 256; x < ny + 256; x++) lv_row(ring_col(nx), nx, x);
    } else if (dy < 0) {
        for (x = ny; x < view_y; x++) lv_row(ring_col(nx), nx, x);
    }
    view_rows += dy < 0 ? -dy : dy;
    view_y = ny;
    scroll_regs();
}

void view_init(void)
{
    /* Layer 2 320 x 256 from bank 9 (pages 18-27) */
    ZXN_WRITE_REG(0x12, 9);
    ZXN_WRITE_REG(0x70, 0x10);           /* 320 x 256, 8 bpp */
    ZXN_WRITE_REG(0x1C, 1);              /* reset the Layer 2 clip index */
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 159);
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 255);
    z80_outp(0x123B, 0x02);              /* Layer 2 visible */
    view_x = -10000;
}

/* the course's picture palette (kept here: the flash and the HUD's shade
   are made from it) into Layer 2's first palette */
uint16_t view_pal[64];
uint8_t view_npal, view_palgen;          /* palgen: counts palette loads */

void view_palette(void)
{
    ZXN_WRITE_MMU2(lv_page);
    view_npal = *(const uint8_t *)0x4009;
    if (view_npal > 64) view_npal = 64;
    memcpy(view_pal, (const void *)0x400A, view_npal * 2);
    ZXN_WRITE_MMU2(10);
    palfx_load(PAL_L2, 0, view_npal, view_pal);
    view_palgen++;
}

static void objects_load(uint8_t level);

/* the frame: follow game.c's camera */
void view_frame(void)
{
    const Player *p = &G.p[0];
    int16_t x, y;
    if (shown != G.level) {
        view_course(G.level);
        view_palette();
        objects_load(G.level);
        view_x = -10000;
    }
    view_project(&p->cam.focus, &x, &y);
    if (p->shake > 0) {
        /* port: the original's shake moves the focus by up to shake each way */
        int16_t s = (int16_t)((p->shake * 2925L) >> 24) + 1;
        x += (int16_t)(rnd16() % (2 * s + 1)) - s;
        y += (int16_t)(rnd16() % (2 * s + 1)) - s;
    }
    view_goto(x, y);
}

/* ---- sprites: what moves, far to near ---- */

typedef struct { int16_t x, y, z; uint8_t pat, pal; } Spr;
#define MAXSPR 64
static Spr list[MAXSPR];
static uint8_t order[MAXSPR];
static uint8_t nspr;

static int16_t depth_of(const V3 *p)    /* along the view (Q12 >> 6), the far end first */
{
    long a = (p->z >> 6) * 3355L, b = (p->y >> 6) * 2349L;
    return (int16_t)((a - b) >> 12);
}

static void put(const V3 *p, int16_t z, uint8_t pat, uint8_t pal)
{
    int16_t x, y;
    Spr *s;
    if (nspr >= MAXSPR) return;
    view_project(p, &x, &y);
    x -= view_x + 8;
    y -= view_y + 8;
    if (x < -16 || x >= 320 || y < -16 || y >= 256) return;
    s = &list[nspr++];
    s->x = x; s->y = y; s->z = z; s->pat = pat; s->pal = pal;
}

/* port: the shadow's deck: under a rolling ball, its own contact (r below
   it); in the air the original's deck search (phys_ground_below, ~300,000
   T-states), every 4th frame for each ball */
static fix shadow_y[MAXENEMY + 1];
static uint8_t shadow_ok[MAXENEMY + 1], shadow_age;

static void ball(const Ball *b, uint8_t pat, uint8_t pal, uint8_t shadow_pat, uint8_t k)
{
    V3 g;
    int16_t z = depth_of(&b->p);
    if (shadow_pat != 0xFF) {
        if (b->grounded) {
            shadow_y[k] = b->p.y - b->r;
            shadow_ok[k] = 1;
        } else if (((shadow_age + k) & 3) == 0)
            shadow_ok[k] = phys_ground_below(&b->p, &shadow_y[k]);
        if (shadow_ok[k] && b->p.y - shadow_y[k] <= 14 * FX) {
            g.x = b->p.x; g.y = shadow_y[k] + 41; g.z = b->p.z;
            put(&g, depth_of(&g) + 3, shadow_pat, PAL_SHADOW);   /* port: just behind the ball */
        }
    }
    put(&b->p, z, pat, pal);
}


/* ---- the moving scenery: crushers, sweeper arms, crumbling tiles ----
   port: the original draws them as lit boxes and course faces each frame;
   here they are sprites made on the host (gen_course.objects: the same
   boxes and lighting, the tiles hidden where the course is in front of
   them): a crusher moves up and down, an arm shows one of 16 turns, a tile
   shivers and drops away */
#define PAL_OBJ   4
#define PAT_OBJ   16                     /* patterns from here: the objects' */
#define OBJ_MAX   10
#define CELLS_MAX 112
typedef struct { uint8_t kind, first, n, frames; V3 ref; uint8_t fpage; uint16_t faddr; } Obj;
static Obj obj[OBJ_MAX];
static uint8_t nobj, ncell;
static int8_t cell_dx[CELLS_MAX], cell_dy[CELLS_MAX];
static uint8_t sweep_shown = 0xFF, sweep_pat;
static uint8_t rd_page;
static const uint8_t *rd_p;

static uint8_t rd8(void)
{
    uint8_t v;
    ZXN_WRITE_MMU2(rd_page);
    v = *rd_p++;
    if (rd_p == (const uint8_t *)0x6000) { rd_p = (const uint8_t *)0x4000; rd_page++; }
    return v;
}

/* n bytes; a block copy unless they cross the page */
static void rd_n(uint8_t *d, uint16_t n)
{
    if (rd_p + n <= (const uint8_t *)0x6000) {
        ZXN_WRITE_MMU2(rd_page);
        memcpy(d, rd_p, n);
        rd_p += n;
        if (rd_p == (const uint8_t *)0x6000) { rd_p = (const uint8_t *)0x4000; rd_page++; }
        return;
    }
    while (n--) *d++ = rd8();
}

static uint8_t sweep_fpage[16];
static const uint8_t *sweep_faddr[16];   /* where each of the arm's frames starts */

/* one 16 x 16 cell's dx, dy and pattern from the reader into pattern slot pat */
static void rd_cell(uint8_t k, uint8_t pat)
{
    static uint8_t buf[130];
    rd_n(buf, 130);
    ZXN_WRITE_MMU2(10);
    cell_dx[k] = (int8_t)buf[0];
    cell_dy[k] = (int8_t)buf[1];
    spl_patterns(pat, buf + 2, 1);
}

static void objects_load(uint8_t level)
{
    uint8_t i, f, c, npal;
    uint16_t pal[15];
    rd_page = obj_page[level];
    rd_p = (const uint8_t *)obj_addr[level];
    nobj = rd8();
    npal = rd8();
    rd_n((uint8_t *)pal, 30);
    if (nobj > OBJ_MAX) nobj = OBJ_MAX;
    ncell = 0;
    for (i = 0; i < nobj; i++) {
        Obj *o = &obj[i];
        o->kind = rd8();
        rd_n((uint8_t *)&o->ref, 12);
        o->frames = rd8();
        o->first = ncell;
        if (o->frames > 1) {
            /* an arm: its frames stay in their pages, one is uploaded at a time */
            o->fpage = rd_page;
            o->faddr = (uint16_t)rd_p;
            o->n = 0;
            for (f = 0; f < o->frames; f++) {
                uint8_t n;
                if (f < 16) { sweep_fpage[f] = rd_page; sweep_faddr[f] = rd_p; }
                n = rd8();
                if (n > o->n) o->n = n;
                for (c = 0; c < n; c++) {                    /* past its cells */
                    uint16_t left = 130;
                    while (left) {
                        uint16_t room = (uint16_t)((const uint8_t *)0x6000 - rd_p), k = left < room ? left : room;
                        rd_p += k;
                        left -= k;
                        if (rd_p == (const uint8_t *)0x6000) { rd_p = (const uint8_t *)0x4000; rd_page++; }
                    }
                }
            }
            sweep_pat = PAT_OBJ + ncell;
            ncell += o->n;
            sweep_shown = 0xFF;
            continue;
        }
        o->n = rd8();
        for (c = 0; c < o->n && ncell < CELLS_MAX; c++, ncell++) rd_cell(ncell, PAT_OBJ + ncell);
    }
    ZXN_WRITE_MMU2(10);
    ZXN_WRITE_REG(0x43, 0x20);           /* sprite first palette: the objects' 16 */
    ZXN_WRITE_REG(0x40, PAL_OBJ * 16 + 1);
    for (i = 0; i < npal; i++) {
        ZXN_WRITE_REG(0x44, (uint8_t)(pal[i] >> 1));
        ZXN_WRITE_REG(0x44, (uint8_t)(pal[i] & 1));
    }
    ZXN_WRITE_REG(0x43, 0x10);
}

/* the arm's frame f into its cells (the patterns by DMA) */
static void sweep_frame(Obj *o, uint8_t f)
{
    uint8_t n, c;
    rd_page = sweep_fpage[f];
    rd_p = sweep_faddr[f];
    n = rd8();
    for (c = 0; c < n; c++) rd_cell(o->first + c, sweep_pat + c);
    for (; c < o->n; c++) cell_dx[o->first + c] = -128;      /* unused this frame */
    ZXN_WRITE_MMU2(10);
}

static void put_obj(const Obj *o, const V3 *p, int16_t jx)
{
    int16_t x, y, z = depth_of(p);
    uint8_t c;
    view_project(p, &x, &y);
    x -= view_x - jx;
    y -= view_y;
    if (x < -96 || x > 416 || y < -96 || y > 352) return;
    for (c = 0; c < o->n; c++) {
        Spr *s;
        if (cell_dx[o->first + c] == -128 || nspr >= MAXSPR) continue;
        s = &list[nspr++];
        s->x = x + cell_dx[o->first + c];
        s->y = y + cell_dy[o->first + c];
        s->z = z;
        s->pat = PAT_OBJ + o->first + c;
        s->pal = PAL_OBJ;
    }
}

static void objects_draw(void)
{
    uint8_t i, nc = 0, ns = 0, ng = 0;
    for (i = 0; i < nobj; i++) {
        const Obj *o = &obj[i];
        V3 p;
        p.x = o->ref.x; p.y = o->ref.y; p.z = o->ref.z;
        if (o->kind == 1) {                  /* crusher: lifted by crush_h */
            p.y += (phy_crush[nc].lift * Wd.crush_h[nc]) >> 12;
            nc++;
            put_obj(o, &p, 0);
        } else if (o->kind == 2) {           /* arm: the turn render.c draws, of 16 */
            long a = phy_sweep[ns].yaw * 2 / 45 + (Wd.sweep_angle[ns] >> 8);
            uint8_t f = (uint8_t)(((uint16_t)a + 1024) >> 11) & 15;
            ns++;
            if (f != sweep_shown) {
                sweep_frame((Obj *)o, f);
                sweep_shown = f;
            }
            put_obj(o, &p, 0);
        } else {                             /* a tile: shivers, then drops away */
            const Crumble *cr = &Wd.crumble[crumble_spec[ng].group - 1];
            ng++;
            if (cr->state == 2) {
                if (cr->fall > 10 * FX) continue;
                p.y -= cr->fall;
            }
            put_obj(o, &p, cr->state == 1 ? ((Wd.clock & 1) ? 1 : -1) : 0);
        }
    }
}

void view_sprites(void)
{
    uint8_t i, j, k;
    const Player *p = &G.p[0];
    nspr = 0;
    shadow_age++;
    for (i = 0; i < (uint8_t)Wd.nenemy; i++) {
        const Enemy *e = &Wd.enemy[i];
        if (e->kind) ball(&e->b, PAT_BLOB, PAL_BLOB, 0xFF, i + 1);
        else ball(&e->b, PAT_STEEL, PAL_STEEL, PAT_SHADOW + 1, i + 1);
    }
    if (G.state != ST_TITLE && !p->dying && p->m.live)
        ball(&p->m, PAT_MARBLE, PAL_MARBLE, PAT_SHADOW, 0);
    objects_draw();
    for (i = 0; i < MAXBITS; i++) {
        const Bit *b = &bits[i];
        long k12, r;
        if (!b->life) continue;
        if (b->look == SP_SPARK) { put(&b->p, depth_of(&b->p), PAT_SPARK, PAL_SPARK); continue; }
        k12 = FX - (b->age << 12) / b->life;             /* the original's Fader: shrink away */
        r = (b->size * k12) >> 12;
        r = (r * 2925L) >> 20;                           /* pixels */
        if (r < 1) continue;
        put(&b->p, depth_of(&b->p), PAT_MARBLE + (r > 4 ? 5 : (uint8_t)r), PAL_MARBLE);
    }
    /* far to near: an insertion sort of the indices */
    for (i = 0; i < nspr; i++) {
        k = i;
        for (j = i; j > 0 && list[order[j - 1]].z < list[k].z; j--) order[j] = order[j - 1];
        order[j] = k;
    }
    spl_begin();
    for (i = 0; i < nspr; i++) {
        const Spr *s = &list[order[i]];
        spl_add(s->x, s->y, s->pat, s->pal, 0);
    }
    spl_end();
}

void view_sprite_init(void)
{
    uint8_t i;
    spl_init();
    spl_patterns(0, spr_patterns, SPR_NPAT);
    ZXN_WRITE_REG(0x43, 0x20);           /* sprite first palette */
    ZXN_WRITE_REG(0x40, 0);
    for (i = 0; i < SPR_NPAL * 16; i++) {
        uint16_t c = spr_palettes[i];
        ZXN_WRITE_REG(0x44, (uint8_t)(c >> 1));
        ZXN_WRITE_REG(0x44, (uint8_t)(c & 1));
    }
}
