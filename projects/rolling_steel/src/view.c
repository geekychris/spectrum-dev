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

/* the course's picture palette into Layer 2's first palette */
void view_palette(void)
{
    uint8_t n, i;
    const uint16_t *pal;
    ZXN_WRITE_MMU2(lv_page);
    n = *(const uint8_t *)0x4009;
    pal = (const uint16_t *)0x400A;
    ZXN_WRITE_REG(0x43, 0x10);           /* Layer 2 first palette, auto-increment */
    ZXN_WRITE_REG(0x40, 0);
    for (i = 0; i < n; i++) {
        uint16_t c = pal[i];
        ZXN_WRITE_REG(0x44, (uint8_t)(c >> 1));
        ZXN_WRITE_REG(0x44, (uint8_t)(c & 1));
    }
    ZXN_WRITE_MMU2(10);
}

/* the frame: follow game.c's camera */
void view_frame(void)
{
    const Player *p = &G.p[0];
    int16_t x, y;
    if (shown != G.level) {
        view_course(G.level);
        view_palette();
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

#include "sprite_data.h"

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
