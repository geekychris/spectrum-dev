/*
 * Uranus Lander - the Spectrum front end's drawing (port: replaces draw.c).
 *
 * The game's world is the original's 320x256; it is drawn scaled to 256x192
 * (x 0.8, y 0.75). The terrain of a level is drawn once (sdk hmap48); what
 * moves is XORed on and off: the ship (drawn with sdk bitline into a small
 * buffer, then XOR-blitted, only when it moved or turned), the stars and the
 * explosion (sdk dots48), so the scenery never needs redrawing. Colour is by
 * attributes: the ship and the sparks tint the open-sky cells they are in
 * (sdk attrtint), cells with terrain keep the terrain's colour. Rows 0 and
 * 23 are the HUD (fuel gauge: sdk gauge48); nothing that moves goes there.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "game.h"
#include "view.h"
#include "zxgame.h"
#include "zxtext.h"
#include "fixmath.h"
#include "bitline.h"
#include "dots48.h"
#include "hmap48.h"
#include "gauge48.h"
#include "attrtint.h"

extern GameState gs;

/* ------------------------------------------------------------ colours */

#define A_SKY      (INK_WHITE | BRIGHT)         /* open sky: stars; ship tints it */
#define A_SURFACE  (INK_WHITE)                  /* cells the surface line crosses */
#define A_GROUND   (INK_RED)                    /* the brown body (dither) */
#define A_PAD      (INK_YELLOW | BRIGHT)
#define A_LABEL    (INK_MAGENTA | BRIGHT)
#define A_SHIP     (INK_CYAN | BRIGHT)
#define A_HUD      (INK_WHITE)
#define A_SCORE    (INK_YELLOW | BRIGHT)
#define A_GREEN    (INK_GREEN | BRIGHT)
#define A_YELLOW   (INK_YELLOW | BRIGHT)
#define A_RED      (INK_RED | BRIGHT)

#define PLAY_Y0  8                  /* moving things stay in rows 1..22 */
#define PLAY_Y1  183

/* ------------------------------------------------------------ world -> screen */

uint8_t wx2sx[TERRAIN_W];           /* x * 0.8, rounded (stars.asm reads it) */
static uint8_t hmap[256];           /* the level's surface, screen y per column */

/* world y (-40..296) -> screen y (x 0.75, rounded) */
static int16_t wy2sy(int16_t wy)
{
    return (int16_t)(((uint16_t)(wy + 40) * 3 + 2) >> 2) - 30;
}

#define BUF_W 4                     /* the ship buffer: 32 x 24 pixels */
#define BUF_H 24
#define BUF_OY 12                   /* the ship's centre line in it */
static uint8_t *ship_rows[BUF_H];

void view_init(void)
{
    uint16_t x;
    for (x = 0; x < TERRAIN_W; x++) wx2sx[x] = (uint8_t)((x * 4 + 2) / 5);
    tint_plain = A_SKY;
}

/* ------------------------------------------------------------ the ship */

/* the original's lander (9 points), antenna (1) and flame (draw.c: small
   (-2,3) (0,9) (2,3), big tip (0,12)), pointing up at angle 0 */
#define NPTS 14
static const int8_t ship_pts[NPTS * 2] = {
    0, -7,  4, -3,  4, 1,  7, 6,  3, 3,  -3, 3,  -7, 6,  -4, 1,  -4, -3,
    0, -8,
    -2, 3,  0, 9,  2, 3,  0, 12,
};

/* The ship is drawn (sdk bitline) into an unshifted 32 x 24 image per flame
   state (none, small, big) when its angle changes, and shifted into place
   (sdk spr32_shift) when it moves: no lines redrawn for a move. */
#define BUF_OX 12                   /* the ship's centre in the images */
static uint8_t ship_img[3][BUF_W * BUF_H];
static int16_t img_angle[3] = {-1, -1, -1};
static uint8_t ship_disp[2][BUF_W * BUF_H];     /* shifted: shown, next */
static uint8_t disp_cur;

static int16_t rot_angle = -1;
static int8_t rot_pts[NPTS * 2];        /* rotated and scaled to the screen */
#define body (rot_pts)
#define antenna (rot_pts + 18)
static int8_t flame_s[6], flame_b[6];
static int8_t bx0, bx1, by0, by1;        /* the body's box */
static int8_t fx0, fx1, fy0, fy1;        /* with the flame */

static void box(const int8_t *p, uint8_t n, int8_t *x0, int8_t *x1, int8_t *y0, int8_t *y1)
{
    for (; n; n--, p += 2) {
        if (p[0] < *x0) *x0 = p[0];
        if (p[0] > *x1) *x1 = p[0];
        if (p[1] < *y0) *y0 = p[1];
        if (p[1] > *y1) *y1 = p[1];
    }
}

/* rotate (sdk rot2d) and scale to the screen: x 0.8 (205/256), y 0.75 (192/256) */
static void rotate_ship(uint8_t angle)
{
    rot2d_set(angle, 205, 192);
    rot2d(ship_pts, rot_pts, NPTS);
    memcpy(flame_s, rot_pts + 20, 6);
    memcpy(flame_b, rot_pts + 20, 6);
    flame_b[2] = rot_pts[26];
    flame_b[3] = rot_pts[27];
    bx0 = by0 = 127;
    bx1 = by1 = -127;
    box(body, 9, &bx0, &bx1, &by0, &by1);
    fx0 = bx0; fx1 = bx1; fy0 = by0; fy1 = by1;
    box(flame_b, 3, &fx0, &fx1, &fy0, &fy1);
    rot_angle = angle;
}

/* what is drawn and where */
typedef struct {
    uint8_t on;
    int16_t angle;
    uint8_t shift, flame;
    int16_t sx, sy;
    /* the blit, kept for the erase */
    const uint8_t *src;
    uint8_t col, y, n, lines;
} ship_view;
static ship_view shown_ship;
static tint_area ship_tint;

extern const uint8_t *spr_src;
extern uint8_t spr_col, spr_y, spr_n, spr_lines, spr_stride, spr_mode;
extern void spr_blit(void);

static void ship_blit(const ship_view *v)
{
    spr_src = v->src;
    spr_col = v->col;
    spr_y = v->y;
    spr_n = v->n;
    spr_lines = v->lines;
    spr_stride = BUF_W;
    spr_mode = 0;
    spr_blit();
}

/* the unshifted image of flame state f at the angle */
static const uint8_t *ship_image(uint8_t angle, uint8_t f)
{
    uint8_t *img = ship_img[f], i;
    if (img_angle[f] == angle) return img;
    if (rot_angle != angle) rotate_ship(angle);
    memset(img, 0, BUF_W * BUF_H);
    for (i = 0; i < BUF_H; i++) ship_rows[i] = img + i * BUF_W;
    bl_rows = ship_rows;
    bl_cx0 = 0;
    bl_cx1 = BUF_W * 8 - 1;
    bl_cy0 = 0;
    bl_cy1 = BUF_H - 1;
    bl_mode = BL_OR;
    bl_poly(body, 9, BUF_OX, BUF_OY, 1);
    bl_plot(BUF_OX + antenna[0], BUF_OY + antenna[1]);
    if (f) bl_poly(f == 1 ? flame_s : flame_b, 3, BUF_OX, BUF_OY, 1);
    bl_screen();
    bl_mode = BL_XOR;
    img_angle[f] = angle;
    return img;
}

/* the ship as it should look now (v->on = 0: not shown) */
static void ship_wanted(ship_view *v)
{
    Ship *s = &gs.ship;
    v->on = 0;
    if (!s->alive) return;
    if (gs.state == STATE_PLAYING && gs.state_timer > 0 && (gs.frame & 4)) return;  /* blink */
    if (gs.state != STATE_PLAYING && gs.state != STATE_LANDED) return;
    {
        int16_t wx = FIX_ROUND(s->x), wy = FIX_ROUND(s->y);
        if (wx < 0) wx = 0;
        if (wx >= TERRAIN_W) wx = TERRAIN_W - 1;
        if (wy < -40 || wy > SCREEN_H + 10) return;
        v->sx = wx2sx[wx];
        v->sy = wy2sy(wy);
    }
    v->on = 1;
    v->angle = (s->angle + 2) & 0xFC;   /* port: drawn in 64 directions (the logic keeps 256) */
    v->shift = (uint8_t)(v->sx - BUF_OX) & 7;
    v->flame = 0;
    if (s->thrusting && s->fuel > 0) v->flame = (gs.frame & 2) ? 2 : 1;
}

/* the blit for v (buffer column/line clipping) */
static void ship_place(ship_view *v)
{
    int16_t top = v->sy - BUF_OY, y0 = top, y1 = top + BUF_H - 1;
    int16_t col = ((v->sx - BUF_OX - v->shift + 64) >> 3) - 8;
    const uint8_t *src = ship_disp[disp_cur];
    uint8_t n = BUF_W;
    if (y0 < PLAY_Y0) y0 = PLAY_Y0;
    if (y1 > PLAY_Y1) y1 = PLAY_Y1;
    if (y1 < y0) { v->lines = 0; return; }
    src += (y0 - top) * BUF_W;
    if (col < 0) { src -= col; n += col; col = 0; }
    if (col + n > 32) n = 32 - col;
    v->src = src;
    v->col = col;
    v->y = y0;
    v->n = n;
    v->lines = y1 - y0 + 1;
}

/* the cells to tint: the body's box (and the flame's) */
static void ship_tint_place(const ship_view *v)
{
    int16_t x0, x1, y0, y1;
    if (!v->on || !v->lines) return;
    if (v->flame) {
        x0 = v->sx + fx0;
        x1 = v->sx + fx1;
        y0 = v->sy + fy0;
        y1 = v->sy + fy1;
    }
    else {
        x0 = v->sx + bx0;
        x1 = v->sx + bx1;
        y0 = v->sy + by0;
        y1 = v->sy + by1;
    }
    if (x0 < 0) x0 = 0;
    if (x1 > 255) x1 = 255;
    if (y0 < PLAY_Y0) y0 = PLAY_Y0;
    if (y1 > PLAY_Y1) y1 = PLAY_Y1;
    if (y1 < y0) return;
    tint_place(&ship_tint, x0 >> 3, y0 >> 3, (x1 >> 3) - (x0 >> 3) + 1, (y1 >> 3) - (y0 >> 3) + 1, A_SHIP);
}

/* ------------------------------------------------------------ stars */

/* stars.asm draws them (see there): a record per star */
typedef struct { uint8_t sx, on; uint8_t *row; uint8_t tmap, spare; } star_view;
star_view star_rec[MAX_STARS];
extern uint8_t st_force, st_alloff, st_half;
extern void stars_scan(const Star *stars);
static uint8_t stars_unknown;

static void stars_reset(void)
{
    uint8_t i;
    star_view *r = star_rec;
    for (i = 0; i < MAX_STARS; i++, r++) {
        uint8_t sy = (uint8_t)wy2sy(gs.stars[i].y);
        r->on = 0;
        r->row = zx_row[sy];
        r->tmap = (sy >= PLAY_Y0 && sy <= PLAY_Y1) ? (sy >> 3) * 4 : 0xFF;
    }
    stars_unknown = 1;
}

/* move the stars on the screen (all_off: take them all off) */
static void stars_plan(uint8_t all_off)
{
    st_alloff = all_off;
    st_force = all_off | stars_unknown;
    /* half of them each frame (they move at most a pixel in 1.3 frames) */
    st_half = st_force ? 0 : (st_half == 1 ? 2 : 1);
    stars_scan(gs.stars);
    stars_unknown = all_off;            /* after all off: look at every star again */
}

/* ------------------------------------------------------------ explosion */

static uint8_t part_xy[2][MAX_PARTICLES * 2];
static uint8_t part_n[2], part_cur;
static uint16_t part_cell[MAX_PARTICLES];
static uint8_t part_cells;

static const uint8_t spark_attr[3] = {A_RED, INK_RED | BRIGHT, A_YELLOW};

extern uint8_t particles_project(const Particle *p, uint8_t *xy);   /* particles.asm */

static void particles_plan(void)
{
    uint8_t next = part_cur ^ 1;
    part_n[next] = gs.particles_live ? particles_project(gs.particles, part_xy[next]) : 0;
}

static void particles_tint(void)
{
    uint8_t i, k = 0, *xy = part_xy[part_cur];
    for (i = 0; i < part_cells; i++) tint_uncell(part_cell[i]);
    part_cells = 0;
    for (i = 0; i < part_n[part_cur]; i++, xy += 2) {
        uint16_t c = ((uint16_t)(xy[1] & 0xF8) << 2) + (xy[0] >> 3);
        tint_cell(c, spark_attr[k]);
        if (++k == 3) k = 0;
        part_cell[part_cells++] = c;
    }
}

/* ------------------------------------------------------------ HUD */

uint8_t view_autopilot;             /* main.c: show AUTO */

/* a HUD number: a signed value left-aligned in `width` cells; only the
   characters that changed are printed (a ROM-font character is ~2,400
   T-states), and the digits come from subtraction, not division */
typedef struct { uint8_t col, row, width; char shown[6]; } hud_num;

static const uint16_t pow10[4] = {10000, 1000, 100, 10};

static void hud_num_set(hud_num *h, int16_t v)
{
    char buf[7], *p = buf, one[2];
    uint16_t u = v < 0 ? -v : v;
    uint8_t i, started = 0;
    if (v < 0) *p++ = '-';
    for (i = 0; i < 4; i++) {
        char d = '0';
        while (u >= pow10[i]) { u -= pow10[i]; d++; }
        if (d != '0' || started) { *p++ = d; started = 1; }
    }
    *p++ = '0' + (char)u;
    while (p < buf + h->width) *p++ = ' ';
    one[1] = 0;
    for (i = 0; i < h->width; i++)
        if (buf[i] != h->shown[i]) {
            one[0] = h->shown[i] = buf[i];
            zx_print_at(h->col + i, h->row, one);
        }
}

static void hud_num_init(hud_num *h, uint8_t col, uint8_t row, uint8_t width)
{
    h->col = col;
    h->row = row;
    h->width = width;
    memset(h->shown, 0, sizeof(h->shown));
}

static gauge fuel_gauge;
static LONG hud_score;
static WORD hud_level, hud_lives, hud_fuel;
static hud_num num_level, num_alt, num_vx, num_vy;
static uint8_t hud_vx_attr, hud_vy_attr, hud_auto;

static uint8_t speed_attr(Fixed v, LONG safe)
{
    if (v < 0) v = -v;
    if (v < safe) return A_GREEN;
    if (v < safe * 3 / 2) return A_YELLOW;
    return A_RED;
}

static void hud_reset(void)
{
    hud_score = -1;
    hud_level = hud_lives = hud_fuel = -32768;
    hud_num_init(&num_level, 30, 0, 2);
    hud_num_init(&num_alt, 15, 23, 4);
    hud_num_init(&num_vx, 22, 23, 4);
    hud_num_init(&num_vy, 28, 23, 4);
    hud_vx_attr = hud_vy_attr = 0;
    hud_auto = 0xFF;
}

/* the instruments change every frame, so one of them (fuel, altitude,
   H, V) is brought up to date each frame, in turn */
static uint8_t hud_turn;

static void hud(void)
{
    Ship *s = &gs.ship;
    if (hud_score != gs.score) {
        text_num(0, 0, (uint32_t)gs.score, 7);
        hud_score = gs.score;
    }
    if (hud_level != gs.level) {
        hud_num_set(&num_level, gs.level);
        hud_level = gs.level;
    }
    if (hud_lives != gs.lives) {
        uint8_t i;
        static const uint8_t icon[8] = {0x00, 0x38, 0x7C, 0x7C, 0x28, 0x44, 0x82, 0x00};   /* a lander */
        static const uint8_t blank[8] = {0};
        for (i = 0; i < 5; i++) tile_draw(i, 23, i < gs.lives ? icon : blank);
        hud_lives = gs.lives;
    }
    if (hud_auto != view_autopilot) {
        zx_print_at(6, 23, view_autopilot ? "AUTO" : "    ");
        hud_auto = view_autopilot;
    }
    switch (hud_turn++ & 3) {
    case 0:
        if (hud_fuel != s->fuel) {  /* fuel: the original's percentage, its colours */
            WORD pct = s->fuel > 0 ? (WORD)(((uint16_t)s->fuel * 100u) / (uint16_t)g_tune.fuel_max) : 0;
            uint8_t a = pct > 50 ? A_GREEN : pct > 25 ? A_YELLOW : A_RED;
            if (pct > 100) pct = 100;
            gauge_set(&fuel_gauge, gauge_len(&fuel_gauge, pct, 100), a);
            hud_fuel = s->fuel;
        }
        break;
    case 1: {   /* altitude above the terrain under the ship, as the original */
        WORD sx = FIX_ROUND(s->x), sy = FIX_ROUND(s->y), alt = 0;
        if (sx >= 0 && sx < TERRAIN_W) alt = gs.terrain_y[sx] - sy - 6;
        if (alt < 0) alt = 0;
        hud_num_set(&num_alt, alt);
        break;
    }
    case 2: {
        uint8_t a = speed_attr(s->vx, g_tune.safe_vx);
        hud_num_set(&num_vx, (WORD)(s->vx >> 10));
        if (a != hud_vx_attr) { attr_fill(20, 23, 6, 1, a); hud_vx_attr = a; }
        break;
    }
    default: {
        uint8_t a = speed_attr(s->vy, g_tune.safe_vy);
        hud_num_set(&num_vy, (WORD)(s->vy >> 10));
        if (a != hud_vy_attr) { attr_fill(26, 23, 6, 1, a); hud_vy_attr = a; }
        break;
    }
    }
}

/* ------------------------------------------------------------ overlays */

/* text over the play field (LANDED!, DESTROYED!, GAME OVER): its cells leave
   the open sky, so stars stay off them; the frame it changes, everything
   that moves is taken off first */
static uint8_t overlay_rows[24];    /* rows with overlay text */
static uint8_t refresh;             /* this frame: take everything off and put it back */
typedef struct { uint8_t row, big, attr; char text[20]; } overlay_line;
static overlay_line pending[4];
static uint8_t n_pending, clear_pending;

static void overlay_put(uint8_t row, const char *s, uint8_t attr, uint8_t big)
{
    overlay_line *o;
    if (n_pending >= 4) return;
    o = &pending[n_pending++];
    o->row = row;
    o->big = big;
    o->attr = attr;
    strncpy(o->text, s, 19);
    o->text[19] = 0;
    refresh = 1;
}

void view_overlay_clear(void)
{
    clear_pending = 1;
    n_pending = 0;
    refresh = 1;
}

static void overlay_apply(void)
{
    uint8_t i, r;
    if (clear_pending) {
        for (r = 1; r < 23; r++)
            if (overlay_rows[r]) {
                /* overlays that go away are in the sky */
                gfx_clear_rect(0, r * 8, 32, 8);
                tint_mark(0, r, 32, 1);
                attr_fill(0, r, 32, 1, A_SKY);
                overlay_rows[r] = 0;
            }
        clear_pending = 0;
    }
    for (i = 0; i < n_pending; i++) {
        overlay_line *o = &pending[i];
        uint8_t n = strlen(o->text), w = o->big ? n * 2 : n, h = o->big ? 2 : 1;
        uint8_t col = (32 - w) / 2;
        gfx_clear_rect(col, o->row * 8, w, h * 8);
        if (o->big) text_big(col, o->row, o->text);
        else zx_print_at(col, o->row, o->text);
        attr_fill(col, o->row, w, h, o->attr);
        for (r = 0; r < h; r++) {
            tint_mark(col, o->row + r, w, 0);
            overlay_rows[o->row + r] = 1;
        }
    }
    n_pending = 0;
}

/* ------------------------------------------------------------ the frame */

static uint16_t draws;

void view_frame(void)
{
    ship_view want;
    uint8_t ship_changed;

    /* plan (no screen writes) */
    ship_wanted(&want);
    ship_changed = refresh || want.on != shown_ship.on ||
        (want.on && (want.angle != shown_ship.angle || want.shift != shown_ship.shift ||
                     want.flame != shown_ship.flame || want.sx != shown_ship.sx ||
                     want.sy != shown_ship.sy));
    stars_plan(refresh);
    particles_plan();

    /* take off */
    dots_xor(part_xy[part_cur], part_n[part_cur]);
    if (ship_changed && shown_ship.on && shown_ship.lines) ship_blit(&shown_ship);

    /* scenery changes */
    if (refresh) {
        overlay_apply();
        refresh = 0;
        stars_plan(0);                  /* the stars allowed now */
    }
    hud();

    /* put on */
    part_cur ^= 1;
    dots_xor(part_xy[part_cur], part_n[part_cur]);
    if (ship_changed) {
        if (want.on) {
            if (!shown_ship.on || want.angle != shown_ship.angle || want.shift != shown_ship.shift ||
                want.flame != shown_ship.flame) {
                disp_cur ^= 1;              /* the shown copy stays intact for the erase */
                spr32_shift(ship_disp[disp_cur], ship_image(want.angle, want.flame), BUF_H, want.shift);
            }
            ship_place(&want);
            if (want.lines) ship_blit(&want);
        }
        shown_ship = want;
    }

    /* colour */
    tint_restore(&ship_tint);
    ship_tint.w = 0;
    particles_tint();
    ship_tint_place(&shown_ship);
    draws++;
}

uint16_t view_draws(void) { return draws; }

/* ------------------------------------------------------------ screens */

static void xor_state_reset(void)
{
    shown_ship.on = 0;
    ship_tint.w = ship_tint.h = 0;
    part_n[0] = part_n[1] = 0;
    part_cells = 0;
    memset(overlay_rows, 0, sizeof(overlay_rows));
    n_pending = clear_pending = refresh = 0;
    stars_reset();
}

static const uint8_t dither[8] = {0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00};

/* the play screen for the level in gs: terrain, pads, HUD */
void view_play_screen(void)
{
    uint8_t x, i;
    screen_clear(A_SKY, INK_BLACK);
    for (x = 0;; x++) {             /* the surface under each screen column */
        uint16_t wx = ((uint16_t)x * 5 + 2) >> 2;
        hmap[x] = (uint8_t)wy2sy(gs.terrain_y[wx]);
        if (x == 255) break;
    }
    hmap_line(hmap, 0);
    hmap_fill(hmap, 3, PLAY_Y1, dither);
    for (x = 0; x < 254; x += 2)    /* the original's highlight line, dotted */
        gfx_plot(x, hmap[x] + 2 > PLAY_Y1 ? PLAY_Y1 : hmap[x] + 2);
    hmap_colour(hmap, 1, 22, A_SKY, A_SURFACE, A_GROUND, (uint8_t *)0x5800);
    for (i = 0; i < gs.num_pads; i++) {
        LandingPad *p = &gs.pads[i];
        uint8_t x0 = wx2sx[p->x], x1 = wx2sx[p->x + p->width - 1];
        uint8_t y = (uint8_t)wy2sy(p->y), row = (y + 10) >> 3;   /* below the legs */
        char lbl[3];
        gfx_hline(x0, x1, y - 1);
        gfx_hline(x0, x1, y);
        gfx_plot(x0, y + 1); gfx_plot(x0, y + 2);
        gfx_plot(x1, y + 1); gfx_plot(x1, y + 2);
        attr_fill(x0 >> 3, (y - 1) >> 3, (x1 >> 3) - (x0 >> 3) + 1, 1, A_PAD);
        if (((y + 2) >> 3) != ((y - 1) >> 3))
            attr_fill(x0 >> 3, (y + 2) >> 3, (x1 >> 3) - (x0 >> 3) + 1, 1, A_PAD);
        /* the multiplier under it */
        lbl[0] = 'x';
        lbl[1] = '0' + (char)p->multiplier;
        lbl[2] = 0;
        if (row > 22) row = 22;
        {
            uint8_t col = (uint8_t)((((uint16_t)x0 + x1) >> 1) - 8) >> 3;
            if (col > 30) col = 30;
            zx_print_at(col, row, lbl);
            attr_fill(col, row, 2, 1, A_LABEL);
        }
    }
    /* HUD */
    attr_fill(0, 0, 32, 1, A_HUD);
    attr_fill(0, 23, 32, 1, A_HUD);
    attr_fill(0, 0, 7, 1, A_SCORE);
    zx_print_at(8, 0, "FUEL");
    gauge_init(&fuel_gauge, 13, 0, 12, A_GREEN);
    zx_print_at(26, 0, "LVL");
    attr_fill(0, 23, 5, 1, A_SHIP);
    attr_fill(6, 23, 4, 1, INK_MAGENTA | BRIGHT);
    zx_print_at(11, 23, "ALT");
    zx_print_at(20, 23, "H");
    zx_print_at(26, 23, "V");
    hud_reset();
    tint_capture();
    xor_state_reset();
}

void view_landed(void)
{
    char buf[20], *p;
    LONG b = gs.land_bonus;
    uint8_t n = 0;
    char digits[8];
    overlay_put(4, "LANDED!", A_GREEN, 0);
    /* "x2 BONUS: 535" */
    p = buf;
    *p++ = 'x';
    *p++ = '0' + (char)gs.pads[gs.landed_pad].multiplier;
    memcpy(p, " BONUS: ", 8);
    p += 8;
    do { digits[n++] = '0' + (char)(b % 10); b /= 10; } while (b && n < 7);
    while (n) *p++ = digits[--n];
    *p = 0;
    overlay_put(6, buf, A_SCORE, 0);
}

void view_crash(void)
{
    overlay_put(4, "DESTROYED!", A_RED, 0);
}

void view_gameover(void)
{
    char buf[20], digits[8], *p = buf;
    LONG v = gs.score;
    uint8_t n = 0;
    view_overlay_clear();
    overlay_put(8, "GAME OVER", A_RED, 1);
    memcpy(p, "SCORE: ", 7);
    p += 7;
    do { digits[n++] = '0' + (char)(v % 10); v /= 10; } while (v && n < 7);
    while (n) *p++ = digits[--n];
    *p = 0;
    overlay_put(11, buf, INK_WHITE | BRIGHT, 0);
    overlay_put(13, "PRESS FIRE", A_YELLOW, 0);
}

/* blink a row (attributes only: the text stays) */
void view_blink(uint8_t row, uint8_t col, uint8_t w, uint8_t attr)
{
    attr_fill(col, row, w, 1, (frame_count & 32) ? attr : INK_BLACK);
}

/* ------------------------------------------------------------ title */

static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* Uranus: a dithered sphere lit from the left, with its ring */
static void draw_planet(uint8_t cx, uint8_t cy, uint8_t r)
{
    int16_t dy, dx;
    uint8_t i, lit_x[64], lit_y[64];     /* brightness 15..0: light from the upper left */
    uint16_t r2 = (uint16_t)r * r;
    for (i = 0; i <= 2 * r; i++) {
        lit_x[i] = 15 - (uint8_t)((i * 12) / (2 * r));
        lit_y[i] = (uint8_t)((i * 4) / (2 * r));
    }
    for (dy = -(int16_t)r; dy <= r; dy++) {
        const uint8_t *b = bayer[(cy + dy) & 3];
        int8_t ly = lit_y[dy + r];
        for (dx = -(int16_t)r; dx <= r; dx++) {
            if ((uint16_t)(dx * dx + dy * dy) > r2) continue;
            if ((int8_t)b[(cx + dx) & 3] < (int8_t)lit_x[dx + r] - ly) gfx_plot(cx + dx, cy + dy);
        }
    }
    {   /* the ring: an ellipse */
        int8_t pts[64];
        for (i = 0; i < 32; i++) {
            pts[i * 2] = (int8_t)mulq14(fix_cos14(i * 8), r + 10);
            pts[i * 2 + 1] = (int8_t)mulq14(fix_sin14(i * 8), 4);
        }
        bl_mode = BL_XOR;
        bl_poly(pts, 32, cx, cy, 1);
    }
}

static const uint8_t flicker[16] = {
    INK_CYAN | BRIGHT, INK_WHITE | BRIGHT, INK_CYAN | BRIGHT, INK_CYAN,
    INK_CYAN | BRIGHT, INK_CYAN | BRIGHT, INK_WHITE | BRIGHT, INK_CYAN | BRIGHT,
    INK_CYAN, INK_CYAN | BRIGHT, INK_WHITE | BRIGHT, INK_WHITE | BRIGHT,
    INK_CYAN | BRIGHT, INK_CYAN | BRIGHT, INK_CYAN, INK_CYAN | BRIGHT,
};

void view_title(void)
{
    uint8_t i;
    screen_clear(A_SKY, INK_BLACK);
    text_big(2, 2, "URANUS");
    attr_fill(2, 2, 12, 2, flicker[0]);
    text_big(2, 5, "LANDER");
    attr_fill(2, 5, 12, 2, INK_YELLOW | BRIGHT);
    draw_planet(220, 34, 18);
    attr_fill(24, 1, 8, 8, INK_CYAN | BRIGHT);
    gfx_fill_rect(2, 74, 28, 1, 0xFF);
    gfx_fill_rect(2, 76, 28, 1, 0xFF);
    attr_fill(2, 9, 28, 1, INK_CYAN);
    zx_print_at(11, 11, "TOP SCORES");
    attr_fill(11, 11, 10, 1, INK_WHITE);
    hs_draw(10, 13);
    for (i = 0; i < 5; i++) attr_fill(10, 13 + i, 13, 1, i < 3 ? (INK_YELLOW | BRIGHT) : INK_WHITE);
    zx_print_at(3, 19, "O/P ROTATE    Q/FIRE THRUST");
    zx_print_at(3, 20, "Z AUTOPILOT   T MUSIC (128K)");
    zx_print_at(5, 21, "KEMPSTON / SINCLAIR 1+2");
    attr_fill(0, 19, 32, 3, INK_WHITE);
    zx_print_at(6, 23, "PRESS FIRE TO START");
    attr_fill(0, 23, 32, 1, INK_WHITE);
    tint_capture();
    xor_state_reset();
}

/* the title's animation: stars and the flickering name, every frame */
void view_title_frame(void)
{
    stars_plan(0);
    if ((gs.frame & 3) == 0) attr_fill(2, 2, 12, 2, flicker[(gs.frame >> 2) & 15]);
    view_blink(23, 6, 19, INK_WHITE | BRIGHT);
    draws++;
}
