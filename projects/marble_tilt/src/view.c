/*
 * view.c - Marble Tilt's picture on the Next: the original's render.c.
 *
 * The board: tools/gen_view.py works out on the host where every corner of
 * the board lands on the screen for each tilt step the original eases
 * through (its camera, its tilt drawn double: geom.py), and each level's
 * polygons in draw order. port: the Next fills those polygons (l2fill)
 * instead of projecting them; a board takes about three frames of the
 * CPU, so
 *   - boards are kept: a cache of Layer 2 screens (four on a 1 MB Next,
 *     25 on a 2 MB one), shown by pointing NextReg 0x12 at them; a tilt
 *     seen before costs nothing;
 *   - a new board is drawn in the time left over in each frame (the
 *     marble, the rules and the text keep their rates), into a screen
 *     that isn't shown, and shown when it is done; one that is done after
 *     the tilt moved on is still shown if it lies on the way.
 *
 * The marble: a hardware sprite placed by interpolating in the shown
 * board's grid (projected properly while it falls). port: the original
 * sorts it with the walls; here its pattern is made each time it moves
 * from the Layer 2 pixels under it: a block from a nearer board row hides
 * it (the blocks' colours encode their row), as does anything but the void
 * once it has dropped below the board. Its shadow (the original's flat
 * dark octagon) takes the colour under it from a darker sprite palette.
 */
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "l2fill.h"
#include "zxlog.h"
#include "fix16.h"
#include "view.h"
#include "view_data.h"
#include "marble_data.h"

#define SLOT2_PAGE 10                    /* what slot 2 holds between uses (bank 5) */

uint16_t view_draws, view_hits;
uint8_t view_slots;
static uint8_t vt[2048 + 256];           /* the tilt being drawn: grid int16 x, y; bumpers uint8 */
static int16_t xy[16];

/* the board cache: each slot is a 256 x 192 Layer 2 screen (three 16K
   banks) holding the board for one key (level, gates, tilt steps); the
   one shown is the one NextReg 0x12 points at. 1 MB Next: the two Layer 2
   buffers and two more; 2 MB: 21 more above bank 47 */
#define MAX_SLOTS 25
#define NO_KEY 0xFFFF
static const uint8_t base_banks[] = { 9, 12, 15, 23 };
static uint8_t slot_bank[MAX_SLOTS];
static uint16_t slot_key[MAX_SLOTS], slot_used[MAX_SLOTS], use_clock;
static uint8_t shown = 0xFF, pending = 0xFF, drawing = 0xFF, stage;
static uint16_t want_key = NO_KEY, draw_key;
static const uint8_t *draw_p;
uint8_t view_roll, view_pitch;          /* the tilt steps (0..10) of the board shown */
uint8_t view_trace;                      /* 1: log each board shown (profiling, set through memory) */

static uint16_t key_of(uint8_t level, uint8_t gates, uint8_t tilt)
{
    return (uint16_t)level << 9 | (uint16_t)gates << 8 | tilt;
}

/* banks 48.. exist on a 2 MB Next: write each one's number, read them back
   (on a 1 MB Next, ZEsarUX's default, the pages above 95 are one page) */
static uint8_t big_ram(void)
{
    uint8_t b, ok = 1, saved = ZXN_READ_REG(0x53);
    for (b = 48; b < 112; b += 16) {
        ZXN_WRITE_REG(0x53, b * 2);
        *(volatile uint8_t *)0x6000 = b;
    }
    for (b = 48; b < 112; b += 16) {
        ZXN_WRITE_REG(0x53, b * 2);
        if (*(volatile uint8_t *)0x6000 != b) ok = 0;
    }
    ZXN_WRITE_REG(0x53, saved);
    return ok;
}

void view_init(void)
{
    uint8_t i;
    pal_load(1, view_palette, 128);
    /* sprites: entries 0..127 the board's colours at half brightness (the
       shadow), MARBLE_PAL.. the marble's shades */
    for (i = 0; i < 128; i++) pal_set(2, i, (view_palette[i] >> 1) & 0xDB);    /* each 3-bit channel / 2 */
    for (i = 0; i < MARBLE_SHADES; i++) pal_set(2, MARBLE_PAL + i, marble_shades[i]);
    for (i = 0; i < sizeof(base_banks); i++) slot_bank[view_slots++] = base_banks[i];
    if (big_ram())
        for (i = 48; i + 3 <= 112; i += 3) slot_bank[view_slots++] = i;
    for (i = 0; i < view_slots; i++) slot_key[i] = NO_KEY;
}

/* the nearest step: round(t * 5 / MT_TILT_MAX) (geom.py tilt_step).
   port: zsdcc got this wrong as (t * 5 +- MT_TILT_MAX / 2) / MT_TILT_MAX
   with a ?: (0 came out as 2), so it counts instead */
int8_t view_step(fix t)
{
    int32_t w = t * VIEW_TILTS;
    int8_t k = 0;
    if (w >= 0)
        while (w >= MT_TILT_MAX / 2) { w -= MT_TILT_MAX; k++; }
    else
        while (w <= -(MT_TILT_MAX / 2)) { w += MT_TILT_MAX; k--; }
    return k;
}

static void load_tilt(uint8_t level, uint8_t t)
{
    ZXN_WRITE_MMU2(VIEW_PAGE + (t >> 2));
    memcpy(vt, (uint8_t *)0x4000 + ((uint16_t)(t & 3) << 11), 2048);
    if (view_bump_page[level]) {
        ZXN_WRITE_MMU2(view_bump_page[level] + (t >> 5));
        memcpy(vt + 2048, (uint8_t *)0x4000 + ((uint16_t)(t & 31) << 8), 256);
    }
    ZXN_WRITE_MMU2(SLOT2_PAGE);
}

/* the void: Layer 2 row y is display line y + 32, band (y + 32) / 16 */
static void bands(void)
{
    uint8_t k, saved = ZXN_READ_REG(0x53);
    for (k = 0; k < 12; k++) {
        ZXN_WRITE_REG(0x53, l2_back * 2 + (k >> 1));
        dma_fill((uint8_t *)0x6000 + ((uint16_t)(k & 1) << 12), k + 2, 4096);
    }
    ZXN_WRITE_REG(0x53, saved);
}

/* the least recently shown slot that isn't shown or about to be */
static uint8_t victim(void)
{
    uint8_t i, best = 0xFF;
    for (i = 0; i < view_slots; i++) {
        if (i == shown || i == pending) continue;
        if (best == 0xFF || slot_key[i] == NO_KEY || (slot_key[best] != NO_KEY && slot_used[i] < slot_used[best])) best = i;
        if (slot_key[best] == NO_KEY) break;
    }
    return best;
}

static void start(uint16_t key)
{
    drawing = victim();
    draw_key = key;
    slot_key[drawing] = NO_KEY;
    stage = 0;
}

/* a board between the one shown and the one wanted (same level and
   gates, each tilt step on the way): shown while the next is drawn, so a
   tilt eases over as in the original instead of jumping */
static uint8_t between(uint8_t a, uint8_t b, uint8_t c)
{
    return (a <= b && b <= c) || (c <= b && b <= a);
}

static uint8_t on_the_way(uint16_t key)
{
    uint8_t from, to, t = (uint8_t)key, n = 2 * VIEW_TILTS + 1;
    if (shown == 0xFF || (slot_key[shown] & 0xFF00) != (key & 0xFF00) || (want_key & 0xFF00) != (key & 0xFF00))
        return 0;
    from = (uint8_t)slot_key[shown];
    to = (uint8_t)want_key;
    return between(from / n, t / n, to / n) && between(from % n, t % n, to % n);
}

/* go on drawing until the next frame interrupt (one polygon over at most) */
static uint16_t raster_line(void) { return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F); }
uint16_t view_board_lines;               /* raster lines the last board took (all its slices) */
static uint16_t board_lines;

static void work(void)
{
    uint16_t l0 = raster_line();
    uint8_t t0 = frame_ticks, skip, more, level = (uint8_t)(draw_key >> 9) - 1;
    l2_back = slot_bank[drawing];
    if (stage == 0) {
        load_tilt(level, (uint8_t)draw_key);
        bands();
        draw_p = (const uint8_t *)view_lists[level];
        stage = 1;
        board_lines = 0;
        if (frame_ticks != t0) {
            board_lines += (raster_line() + 312 - l0) % 312;
            return;
        }
    }
    skip = draw_key & 0x100 ? 2 : 1;             /* gates open: skip the shut ones */
    ZXN_WRITE_MMU2(VIEW_LIST_PAGE);
    l2f_begin();
    while (*draw_p && frame_ticks == t0) {
        const uint8_t *p = draw_p;
        uint8_t n = *p & 15, kind = *p >> 4, col = p[1], i;
        int16_t *o = xy;
        p += 2;
        draw_p = p + n * 2;
        if (kind == skip) continue;
        for (i = 0; i < n; i++, p += 2) {
            uint16_t off = *(const uint16_t *)p;
            if (off < 2048) {
                *o++ = *(int16_t *)(vt + off);
                *o++ = *(int16_t *)(vt + off + 2);
            } else {
                *o++ = vt[off];
                *o++ = vt[off + 1];
            }
        }
        l2f_poly(xy, n, col);
    }
    l2f_end();
    more = *draw_p;
    ZXN_WRITE_MMU2(SLOT2_PAGE);
    board_lines += (raster_line() + 312 - l0) % 312;
    if (more) return;
    view_board_lines = board_lines;
    slot_key[drawing] = draw_key;                /* done */
    view_draws++;
    if (draw_key == want_key || on_the_way(draw_key)) pending = drawing;
    drawing = 0xFF;
}

void view_update(const MTGame *g)
{
    uint8_t r = (uint8_t)(view_step(g->roll) + VIEW_TILTS), pt = (uint8_t)(view_step(g->pitch) + VIEW_TILTS), i;
    {
        uint16_t k = key_of((uint8_t)g->level, (uint8_t)g->gates_open, r * (2 * VIEW_TILTS + 1) + pt);
        want_key = k;
    }
    if (shown != 0xFF && slot_key[shown] == want_key) {
        pending = 0xFF;
        return;
    }
    if (pending != 0xFF && slot_key[pending] == want_key) return;
    for (i = 0; i < view_slots; i++)
        if (slot_key[i] == want_key) {           /* drawn before */
            pending = i;
            view_hits++;
            return;
        }
    if (drawing == 0xFF) start(want_key);
}

/* the rest of this frame: the board being drawn, if any (a board in
   progress is finished even if the tilt moved on: it is kept) */
void view_work(void)
{
    if (drawing == 0xFF && want_key != NO_KEY && (shown == 0xFF || slot_key[shown] != want_key) &&
        (pending == 0xFF || slot_key[pending] != want_key))
        start(want_key);
    if (drawing != 0xFF) work();
}

static void load_grid(uint8_t t);

uint8_t view_flip(void)
{
    uint8_t t;
    if (pending == 0xFF) return 0;
    shown = pending;
    pending = 0xFF;
    slot_used[shown] = ++use_clock;
    ZXN_WRITE_REG(0x12, slot_bank[shown]);
    t = (uint8_t)slot_key[shown];
    view_roll = t / (2 * VIEW_TILTS + 1);
    view_pitch = t % (2 * VIEW_TILTS + 1);
    load_grid(t);
    if (view_trace) zx_log("mt: flip %u,%u frame=%u\n", view_roll, view_pitch, frame_count);
    return 1;
}

/* ---- the marble ---- */

/* the original's bpt() + cam_xform + cam_project for one point, at the
   tilt drawn (16.16 cos/sin of twice the tilt) */
static int32_t cr, sr, cp, sp;
static uint8_t trig_roll = 0xFF, trig_pitch;

static void trig(void)
{
    uint16_t a;
    if (trig_roll == view_roll && trig_pitch == view_pitch) return;
    trig_roll = view_roll;
    trig_pitch = view_pitch;
    a = (uint16_t)a3_angle(2 * (((int32_t)(int8_t)(view_roll - VIEW_TILTS) * MT_TILT_MAX) / VIEW_TILTS));
    cr = a3_cos(a);
    sr = a3_sin(a);
    a = (uint16_t)a3_angle(2 * (((int32_t)(int8_t)(view_pitch - VIEW_TILTS) * MT_TILT_MAX) / VIEW_TILTS));
    cp = a3_cos(a);
    sp = a3_sin(a);
}

#define CAM_Y   470
#define CAM_Z   (-430)
#define CCP     (MARBLE_CCP >> 4)           /* the camera's pitch, cos and sin >> 4 */
#define CSP     (MARBLE_CSP >> 4)

/* screen position (Layer 2 pixels, x and y * 16) and depth of a board point */
static int16_t px, py;
static int32_t pz;
static void project(int32_t x, int32_t y, int32_t z)
{
    int32_t x1 = (x * cr + y * sr) >> 16, y1 = (y * cr - x * sr) >> 16;
    int32_t z2 = (z * cp + y1 * sp) >> 16, y2 = (y1 * cp - z * sp) >> 16;
    int32_t dy = y2 - CAM_Y, dz = z2 - CAM_Z;
    int32_t oy = (dy * CCP + dz * CSP) >> 12, oz = (dz * CCP - dy * CSP) >> 12;
    pz = oz;
    px = (int16_t)(MARBLE_CX * 16 + (x1 * MARBLE_FOV * 16) / oz);
    py = (int16_t)(MARBLE_CY * 16 - (oy * MARBLE_FOV * 16) / oz);
}

static uint8_t pattern[256];

/* the shown board's grid at the floor and at the walls' height (int16 x,
   y; 13 x 13 each), copied from its tilt's table when it is shown: the
   marble and its shadow are placed by interpolating in it */
static int16_t grid[2 * 169 * 2];

static void load_grid(uint8_t t)
{
    ZXN_WRITE_MMU2(VIEW_PAGE + (t >> 2));
    memcpy(grid, (uint8_t *)0x4000 + ((uint16_t)(t & 3) << 11), sizeof(grid));
    ZXN_WRITE_MMU2(SLOT2_PAGE);
}

/* where board point (x, z) at height 0 (layer 0) or 22 (layer 1) is on the
   screen, in 1/16 pixels: bilinear in its cell (the cell is small enough
   for perspective not to show) */
static void place(int16_t x, int16_t z, uint8_t layer)
{
    uint16_t u = (uint16_t)(x + 240), v = (uint16_t)(240 - z);
    uint8_t c = (uint8_t)(u / 40), r = (uint8_t)(v / 40), fx, fz;
    const int16_t *p;
    int16_t ax, ay, bx, by;
    if (c > 11) c = 11;
    if (r > 11) r = 11;
    fx = (uint8_t)(((u - c * 40) * 8) / 5);           /* 0..63 */
    fz = (uint8_t)(((v - r * 40) * 8) / 5);
    p = grid + (layer * 169 + r * 13 + c) * 2;
    ax = p[0] * 16 + (int16_t)(((p[2] - p[0]) * 16 * fx) >> 6);
    ay = p[1] * 16 + (int16_t)(((p[3] - p[1]) * 16 * fx) >> 6);
    p += 26;
    bx = p[0] * 16 + (int16_t)(((p[2] - p[0]) * 16 * fx) >> 6);
    by = p[1] * 16 + (int16_t)(((p[3] - p[1]) * 16 * fx) >> 6);
    px = ax + (int16_t)(((int32_t)(bx - ax) * fz) >> 6);
    py = ay + (int16_t)(((int32_t)(by - ay) * fz) >> 6);
    pz = (p[2] - p[0]) * 16;                          /* the cell's width there, 1/16 pixels */
}

/* the Layer 2 pixel shown at (x, y), 0 (the void) off the picture; maps
   the page into slot 3 only when it changes */
static uint8_t page_shown;
static uint8_t shown_pixel(int16_t x, int16_t y)
{
    uint8_t pg;
    if ((uint16_t)x > 255 || (uint16_t)y > 191) return 0;
    pg = slot_bank[shown] * 2 + (uint8_t)(y >> 5);
    if (pg != page_shown) {
        page_shown = pg;
        ZXN_WRITE_REG(0x53, pg);
    }
    return *((uint8_t *)0x6000 + ((uint16_t)(y & 31) << 8) + (uint8_t)x);
}

static int32_t m_x, m_z;
static uint8_t m_fall = 0xFF, m_slot = 0xFF;

void view_marble(const MTGame *g)
{
    int32_t x = g->x >> 16, z = g->z >> 16, y = MT_R;
    int16_t sx, sy, mx, my, t;
    uint8_t size, row, falling = (uint8_t)g->falling, saved, i;
    const int8_t *m;
    if (shown == 0xFF) return;
    if (x == m_x && z == m_z && falling == m_fall && shown == m_slot) return;     /* nothing moved */
    m_x = x;
    m_z = z;
    m_fall = falling;
    m_slot = shown;
    memset(pattern, L2_TRANSPARENT, 256);
    saved = ZXN_READ_REG(0x53);
    page_shown = 0xFF;
    row = (uint8_t)mt_row_of(g->z);
    if (falling) {                                    /* dropping below the board: the full projection */
        trig();
        t = MT_FALL_T - g->falling;
        y = MT_R - (int32_t)t * t;
        size = (uint8_t)(MT_R - t / 4);
        if (size < 2) size = 2;
        project(x, y, z);
        mx = px >> 4;
        my = py >> 4;
        i = (uint8_t)(((int32_t)size * MARBLE_FOV * 2) / pz);       /* half pixels */
    } else {
        /* the shadow: an ellipse on the floor, 3 units right and nearer */
        place((int16_t)x + 3, (int16_t)z - 3, 0);
        sx = px >> 4;
        sy = py >> 4;
        place((int16_t)x, (int16_t)z, 0);
        mx = px;
        my = py;
        i = (uint8_t)((pz * 9) / (40 * 8));           /* the radius in half pixels: 9 units of the cell's 40 */
        place((int16_t)x, (int16_t)z, 1);
        mx = (mx + (int16_t)(((int32_t)(px - mx) * MT_R) / 22)) >> 4;   /* at the marble's middle, 9 up */
        my = (my + (int16_t)(((int32_t)(py - my) * MT_R) / 22)) >> 4;
        for (m = marble_shadow; m[0] != 127; m += 2) {
            int16_t ax = sx + m[0], ay = sy + m[1], ox = ax - mx + 8, oy = ay - my + 8;
            uint8_t q;
            if ((uint16_t)ox > 15 || (uint16_t)oy > 15) continue;
            q = shown_pixel(ax, ay);
            if (q >= 16 && q < 32) pattern[(uint8_t)(oy * 16 + ox)] = q;
        }
    }
    if (i >= MARBLE_SIZES) i = MARBLE_SIZES - 1;
    for (m = marble_images[i]; m[0] != 127; m += 3) {
        uint8_t q = shown_pixel(mx + m[0], my + m[1]);
        if (falling ? q >= 16 : (q >= 32 && ((q - 32) >> 3) > row)) continue;
        pattern[(uint8_t)((m[1] + 8) * 16 + m[0] + 8)] = MARBLE_PAL + m[2];
    }
    ZXN_WRITE_REG(0x53, saved);
    spr_pattern(0, pattern);
    spr_show(0, mx - 8 + 32, my - 8 + 32, 0, 0);
}

void view_hide_marble(void)
{
    spr_hide(0);
    m_slot = 0xFF;                           /* shown again at the next view_marble */
}
