/*
 * view.c - Marble Tilt's picture on the Next: the original's render.c.
 *
 * The board: tools/gen_view.py worked out on the host where every corner
 * of the board lands on the screen for each tilt step the original eases
 * through (its camera and its tilt drawn double, geom.py), and each level's
 * polygons in draw order. Here a board is drawn only when what it shows
 * changes (the tilt steps, the gates, the level): the void's bands, then
 * the polygons by l2poly, into the back buffer, shown at the next frame.
 *
 * The marble: a hardware sprite, projected here with the same camera at
 * the tilt drawn. port: the original sorts it with the walls; here its
 * pattern is made each frame from the Layer 2 pixels under it: a pixel of
 * a block from a nearer board row hides it (the blocks' colours encode
 * their row, see gen_view.py), as does anything but the void when it has
 * dropped below the board. Its shadow (the original's flat dark octagon)
 * takes the floor's own colour from a darker sprite palette.
 */
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "l2poly.h"
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
uint8_t view_roll, view_pitch;           /* the tilt steps (0..10) of the board shown */

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
    l2_init();
    l2p_xmin = 0;
    l2p_xmax = 255;
    l2p_top = 0;
    l2p_bot = 192;
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

/* go on drawing until the next frame interrupt (one polygon over at most) */
static void work(void)
{
    uint8_t t0 = frame_ticks, skip, more, level = (uint8_t)(draw_key >> 9) - 1;
    l2_back = slot_bank[drawing];
    if (stage == 0) {
        load_tilt(level, (uint8_t)draw_key);
        bands();
        draw_p = (const uint8_t *)view_lists[level];
        stage = 1;
        if (frame_ticks != t0) return;
    }
    skip = draw_key & 0x100 ? 2 : 1;             /* gates open: skip the shut ones */
    ZXN_WRITE_MMU2(VIEW_LIST_PAGE);
    l2p_begin();
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
        l2p_poly(xy, n, col);
    }
    l2p_end();
    more = *draw_p;
    ZXN_WRITE_MMU2(SLOT2_PAGE);
    if (more) return;
    slot_key[drawing] = draw_key;                /* done */
    view_draws++;
    if (draw_key == want_key) pending = drawing;
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

/* the Layer 2 pixel shown at (x, y) (front buffer), 0 off the picture */
static uint8_t shown_pixel(int16_t x, int16_t y)
{
    if ((uint16_t)x > 255 || (uint16_t)y > 191) return 0;
    ZXN_WRITE_REG(0x53, slot_bank[shown] * 2 + (y >> 5));
    return *((uint8_t *)0x6000 + ((uint16_t)(y & 31) << 8) + x);
}

void view_marble(const MTGame *g)
{
    int32_t x = g->x >> 16, z = g->z >> 16, y = MT_R;
    int16_t sx, sy, mx, my;
    uint8_t size, row, falling = g->falling != 0, saved, i;
    const int8_t *m;
    if (shown == 0xFF) return;
    trig();
    memset(pattern, L2_TRANSPARENT, 256);
    saved = ZXN_READ_REG(0x53);
    row = (uint8_t)mt_row_of(g->z);
    if (falling) {
        int16_t t = MT_FALL_T - g->falling;
        y = MT_R - (int32_t)t * t;
        size = (uint8_t)(MT_R - t / 4);
        if (size < 2) size = 2;
    } else {
        size = MT_R;
        /* the shadow: an ellipse on the floor, 3 units right and nearer */
        project(x + 3, 1, z - 3);
        sx = px >> 4;
        sy = py >> 4;
        project(x, MT_R, z);
        mx = px >> 4;
        my = py >> 4;
        for (m = marble_shadow; m[0] != 127; m += 2) {
            int16_t ax = sx + m[0], ay = sy + m[1];
            uint8_t q = shown_pixel(ax, ay);
            if (q >= 16 && q < 32) pattern[(uint8_t)((ay - my + 8) * 16 + (ax - mx + 8))] = q;
        }
    }
    project(x, y, z);
    mx = px >> 4;
    my = py >> 4;
    /* the marble's picture for its size on the screen: radius size * fov / depth */
    i = (uint8_t)(((int32_t)size * MARBLE_FOV * 2) / pz);       /* half pixels */
    if (i >= MARBLE_SIZES) i = MARBLE_SIZES - 1;
    for (m = marble_images[i]; m[0] != 127; m += 3) {
        int16_t ax = mx + m[0], ay = my + m[1];
        uint8_t q = shown_pixel(ax, ay);
        if (falling ? q >= 16 : (q >= 32 && ((q - 32) >> 3) > row)) continue;
        pattern[(uint8_t)((m[1] + 8) * 16 + m[0] + 8)] = MARBLE_PAL + m[2];
    }
    ZXN_WRITE_REG(0x53, saved);
    spr_pattern(0, pattern);
    spr_show(0, mx - 8 + 32, my - 8 + 32, 0, 0);
}

void view_hide_marble(void) { spr_hide(0); }
