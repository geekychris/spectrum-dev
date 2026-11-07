/*
 * The two views (the original's pitch.c): the top pane is P2's (the CPU's)
 * camera, the bottom pane P1's, each looking down the pitch from its
 * skimmer.
 *
 * port: the original redraws both panes every frame (sky, a chequered floor
 * computed per pixel, goal posts, the other skimmer and the ball). Here:
 *   - the floor is a fixed Layer 2 picture (tools/gen_gfx.py) whose pixels
 *     hold their sideways phase; the camera's z picks which of a pane's
 *     120 palette entries are light (a rotation of the run, loaded by DMA
 *     each frame) and its x picks, row by row, whether the row is inverted:
 *     the copper switches those rows to the second Layer 2 palette. The
 *     CPU draws no pixels at all.
 *   - the posts, the other skimmer and the ball are hardware sprites with
 *     images made on the host by the original's drawing code, one image a
 *     size (zoomed by the hardware for big skimmers and long posts); the
 *     copper gives each pane its own sprite clip window, so nothing spills
 *     into the other pane (the original clips each shape to its pane).
 *   - the floor's perspective uses exact row distances; the original's are
 *     integers (360 / dy) and its camera position whole units, so its
 *     stripes step where these slide. A skimmer too close to fit the pane
 *     is cut off at the pane's top edge (the original squashes it).
 *
 * The projection is the original's, in its 16.16 arithmetic.
 *
 * view_prepare() works out the next picture during the frame;
 * view_commit() shows it right after the frame interrupt (line 248), before
 * the top pane's floor starts (row 43, line 11): sprite patterns,
 * attributes, the two palettes and the copper list.
 */
#include <stdint.h>
#include <z80.h>
#include <arch/zxn.h>
#include "roto.h"
#include "view.h"
#include "gfx_data.h"
#include "nextgfx.h"
#include "nextspr.h"
#include "nextsplit.h"
#include "palfx.h"
#include "fixmath.h"

#define SCR_W     320
#define PANE_H    128
#define HORIZON_Y (PANE_H / 3)        /* 42 */
#define FOCAL     180L
#define NEAR_CLIP (ONE / 2)
#define FLOOR_ROWS(p) ((p) ? 85 : 84)  /* the top pane's last row is the divider */

/* sprites and pattern slots of each pane */
#define SPR_POSTS  0                   /* 3 */
#define SPR_SKIM   3                   /* 9 */
#define SPR_BALL   12                  /* 16 */
#define SPR_PANE   32
static const uint8_t slot_ball[2] = {0, 25}, slot_skim[2] = {16, 41};
#define SLOT_POSTS 56                  /* 8: lines 2, 4 .. 16 long */

typedef struct {
    int16_t x, y;                      /* top-left on the 320 x 256 plane */
    uint8_t pat, zoom;                 /* zoom 0..3 (vertical only) */
} Post;

typedef struct {
    uint8_t ball_r;                    /* 0: not seen */
    int16_t ball_x, ball_y;
    uint8_t skim_s;                    /* 0: not seen */
    int16_t skim_x, skim_y;
    uint8_t skim_pal;
    uint8_t nposts;
    Post post[3];
    uint8_t post_pal;
    uint8_t k;                         /* the floor palette's rotation */
    uint8_t shown_r, shown_s;          /* images in the pattern slots */
} Pane;

static Pane pane[2];
uint16_t view_copper_n;                /* instructions in the last list */

/* the floor run: entry j light (A) for j mod N < N/2, else dark (B), as
   0x44 byte pairs; a pane's first palette is entries k..k+N-1, its
   second (rows inverted) k+N/2.. */
static uint8_t floor_run[FLOOR_N * 3 * 2];

/* 1/distance: rcp[zi] = FOCAL * 128 / zi for a whole-unit distance zi
   (1..255), as the original divides by the integer part of the distance */
static int16_t rcp[256];
static uint8_t ball_r_of[256], skim_s_of[256];

/* project a world point (wy8: height above the hover plane, 1/256 units)
   for the camera `cam` facing fwd (+1: +X); the original's project() with
   cos = fwd, sin = 0.
   port: one MUL by a table reciprocal instead of 32-bit divisions (the
   original's divide by the whole-unit distance is kept; positions can
   differ from it by a pixel) */
static uint8_t zi;                     /* the distance of the last projection */
static uint8_t project(int32_t wx, int16_t wy8, int32_t wz, const Skimmer *cam, int8_t fwd,
                       uint8_t y0, int16_t *sx, int16_t *sy)
{
    int32_t d = wx - cam->x;
    int16_t xc8, sxp, syp, r;
    if (fwd < 0) d = -d;
    if (d < NEAR_CLIP) return 0;
    zi = d >= 256L * ONE ? 255 : (uint8_t)(d >> FP);
    if (zi == 0) zi = 1;               /* port: the original divides by 0 here (zc < 1 unit) */
    xc8 = (int16_t)((wz - cam->z) >> 8);
    if (fwd > 0) xc8 = -xc8;
    r = rcp[zi];
    sxp = SCR_W / 2 + (int16_t)((mul16(xc8, r) << 1) >> 16);
    syp = y0 + HORIZON_Y - (int16_t)((mul16(wy8, r) << 1) >> 16);
    if (sxp < 0) sxp = 0;
    if (sxp > SCR_W - 1) sxp = SCR_W - 1;
    if (syp < y0) syp = y0;
    if (syp > y0 + PANE_H - 1) syp = y0 + PANE_H - 1;
    *sx = sxp;
    *sy = syp;
    return 1;
}

/* forward distance of a point from the camera, 16.16 */
static int32_t depth(int32_t wx, const Skimmer *cam, int8_t fwd)
{
    int32_t d = wx - cam->x;
    return fwd < 0 ? -d : d;
}

static void prepare_pane(uint8_t p)
{
    Pane *v = &pane[p];
    const Skimmer *cam = &G.p[p ? 0 : 1], *other = &G.p[p ? 1 : 0];
    int8_t fwd = cam->angle == 0 ? 1 : -1;
    uint8_t y0 = p ? PANE_H : 0;
    int32_t zc, gx, z;
    int16_t sx, sy, sxt, syt;
    uint8_t g;

    /* goal posts: three at each end (the original's loop -40, 0, 40) */
    v->nposts = 0;
    for (g = 0; g < 2; g++) {
        gx = g ? PITCH_LENGTH : -PITCH_LENGTH;
        if (depth(gx, cam, fwd) < NEAR_CLIP) continue;
        v->post_pal = g;                /* +X is P2's goal (amber) */
        for (z = -PITCH_WIDTH; z <= PITCH_WIDTH; z += PITCH_WIDTH) {
            if (project(gx, -512, z, cam, fwd, y0, &sx, &sy) &&
                project(gx, 2048, z, cam, fwd, y0, &sxt, &syt)) {
                Post *q = &v->post[v->nposts++];
                uint16_t len = sy - syt + 1;
                uint8_t zm = len <= 16 ? 0 : len <= 32 ? 1 : len <= 64 ? 2 : 3;
                uint8_t n = (uint8_t)((len + (1 << zm) - 1) >> zm);
                n = (n + 1) & ~1;
                if (n > 16) n = 16;
                q->pat = SLOT_POSTS + n / 2 - 1;
                q->zoom = zm;
                q->x = sx;
                q->y = sy + 1 - (16 << zm);
            }
        }
    }

    /* the other skimmer */
    v->skim_s = 0;
    zc = depth(other->x, cam, fwd);
    if (zc > NEAR_CLIP && project(other->x, -512, other->z, cam, fwd, y0, &sx, &sy)) {
        uint8_t s = skim_s_of[zi];      /* (2 ONE FOCAL / zi) >> FP, 6..40 */
        v->skim_s = s;
        v->skim_x = sx - (s + 1);
        v->skim_y = sy - 3 * s;
        v->skim_pal = p;                /* the bottom pane sees P2 (amber) */
    }

    /* the ball, over everything */
    v->ball_r = 0;
    zc = depth(G.ball.x, cam, fwd);
    if (zc > 0) {
        int16_t r;
        if (zc < NEAR_CLIP) {
            r = 31;                     /* "I've got the ball": the pane's centre, big */
            sx = SCR_W / 2;
            sy = y0 + HORIZON_Y;
        } else if (project(G.ball.x, 0, G.ball.z, cam, fwd, y0, &sx, &sy)) {
            r = ball_r_of[zi];          /* (BALL_RADIUS FOCAL / zi) >> FP, 3..31 */
        } else {
            r = 0;
        }
        if (r > 31) r = 31;             /* port: 31 for the original's 32 (four 16 x 16 patterns) */
        v->ball_r = (uint8_t)r;
        v->ball_x = sx - r;
        v->ball_y = sy - r;
    }

    /* the floor: sideways phase (cell units of 1/256: a period is 512) */
    {
        int16_t cz = (int16_t)(cam->z >> 8) / 5;
        v->k = (uint8_t)(((uint16_t)(cz & 511) * FLOOR_N) >> 9);
    }
}

/* the copper list: each pane's sprite clip window, and the rows whose
   depth parity is odd shown with the second palette */
extern uint16_t floor_rows(uint8_t *out, const int16_t *zc, int16_t cx, uint16_t n, uint16_t neg,
                           uint16_t line);

static void prepare_copper(void)
{
    uint8_t p;
    split_begin();
    for (p = 0; p < 2; p++) {
        const Skimmer *cam = &G.p[p ? 0 : 1];
        uint8_t row = (p ? PANE_H : 0) + HORIZON_Y + 1;
        if (p) split_pane(PANE_H + 1, 255, SPLIT_SPRITES);
        else split_pane(0, PANE_H - 2, SPLIT_SPRITES);
        /* the rows' moves (floor_rows.asm): a change of cell along the
           pitch at most every row of the first ten, then every few rows:
           under 30 a pane, well inside the copper buffer */
        copper_n += floor_rows(copper_buf + copper_n * 2, floor_zc + 1, (int16_t)(cam->x >> 8) / 5,
                               FLOOR_ROWS(p), cam->angle != 0, L2Y_TO_LINE(row) - 1) >> 1;
    }
    view_copper_n = copper_n;
}

void view_init(void)
{
    uint16_t j, c;
    uint8_t *b = floor_run;
    for (j = 1; j < 256; j++) {
        rcp[j] = (int16_t)(FOCAL * 128 / j);
        c = 360 / j;
        skim_s_of[j] = c < 6 ? 6 : c > 40 ? 40 : (uint8_t)c;
        c = 180 / j;
        ball_r_of[j] = c < 3 ? 3 : c > 31 ? 31 : (uint8_t)c;
    }
    for (j = 0; j < FLOOR_N * 3; j++) {
        c = (j % FLOOR_N) < FLOOR_N / 2 ? RGB_GRID_A : RGB_GRID_B;
        *b++ = (uint8_t)(c >> 1);
        *b++ = c & 1;
    }
    /* the fixed entries, in both Layer 2 palettes */
    for (j = 0; j < 2; j++) {
        uint8_t sel = j ? PAL_L2 | PAL_SECOND : PAL_L2;
        static const uint16_t fixed[3] = {RGB_BLACK, RGB_SKY_TOP, RGB_SKY_BOT};
        palfx_load(sel, I_BLACK, 3, fixed);
    }
    palfx_load(PAL_SPR, 0, 32, spr_pal);
    pane[0].shown_r = pane[0].shown_s = pane[1].shown_r = pane[1].shown_s = 0;
    /* the posts' patterns, once */
    ZXN_WRITE_MMU2(POST_PAGE);
    sprg_upload(SLOT_POSTS, (const uint8_t *)POST_ADDR, 8);
    ZXN_WRITE_MMU2(10);
    view_prepare();
}

void view_prepare(void)
{
    prepare_pane(0);
    prepare_pane(1);
    prepare_copper();
}

/* this pane's images into its pattern slots, if they changed */
static void upload(uint8_t p)
{
    Pane *v = &pane[p];
    if (v->ball_r && v->ball_r != v->shown_r) {
        const BallImg *im = &ball_img[v->ball_r];
        ZXN_WRITE_MMU2(im->page);
        sprg_upload(slot_ball[p], (const uint8_t *)im->addr, im->cols * im->cols);
        v->shown_r = v->ball_r;
    }
    if (v->skim_s && v->skim_s != v->shown_s) {
        const SkimImg *im = &skim_img[v->skim_s];
        ZXN_WRITE_MMU2(im->page);
        sprg_upload(slot_skim[p], (const uint8_t *)im->addr, im->cols * im->rows);
        v->shown_s = v->skim_s;
    }
    ZXN_WRITE_MMU2(10);
}

static void show(uint8_t p)
{
    Pane *v = &pane[p];
    uint8_t base = p * SPR_PANE, i;
    static uint8_t a[5];
    /* posts: 1 pixel wide, zoomed vertically only */
    z80_outp(0x303B, base + SPR_POSTS);
    for (i = 0; i < 3; i++) {
        if (i < v->nposts) {
            Post *q = &v->post[i];
            a[0] = (uint8_t)q->x;
            a[1] = (uint8_t)q->y;
            a[2] = (v->post_pal << 4) | ((q->x >> 8) & 1);
            a[3] = 0xC0 | q->pat;
            a[4] = (q->zoom << 1) | ((q->y >> 8) & 1);
        } else {
            a[0] = a[1] = a[2] = a[4] = 0;
            a[3] = 0x40;
        }
        z80_otir(a, 0x57, 5);
    }
    if (v->skim_s) {
        const SkimImg *im = &skim_img[v->skim_s];
        i = sprg_show_ex(base + SPR_SKIM, v->skim_x, v->skim_y, im->cols, im->rows, slot_skim[p], 0,
                         v->skim_pal, im->zoom == 4 ? 2 : im->zoom - 1);
    } else {
        i = base + SPR_SKIM;
    }
    sprg_hide(i, base + SPR_BALL - i);
    if (v->ball_r) {
        const BallImg *im = &ball_img[v->ball_r];
        i = sprg_show_ex(base + SPR_BALL, v->ball_x, v->ball_y, im->cols, im->cols, slot_ball[p], 0, 0, 0);
    } else {
        i = base + SPR_BALL;
    }
    sprg_hide(i, base + SPR_PANE - i);
}

static void palette(uint8_t p)
{
    const uint8_t *run = floor_run + 2 * pane[p].k;
    palfx_load_bytes(PAL_L2, p * FLOOR_N, run, FLOOR_N * 2);
    palfx_load_bytes(PAL_L2 | PAL_SECOND, p * FLOOR_N, run + FLOOR_N, FLOOR_N * 2);
}

void view_commit(void)
{
    upload(0);                          /* the top pane's sprites can start at row 0 */
    show(0);
    palette(0);
    palette(1);
    split_end();
    upload(1);
    show(1);
}
