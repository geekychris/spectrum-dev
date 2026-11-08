/*
 * ball.c - Boing Twist's ball on the Next (the original's render.c).
 *
 * The original draws one textured mesh per tile with OpenGL, each tile
 * showing the part of the picture its piece carries (turned round when the
 * piece is upside down), the grooves between tiles drawn into the picture,
 * the selection as lines over the ball, shaded afterwards by a table.
 *
 * port: here the pieces are put together into one texture (256 x 96 texels:
 * longitude x latitude), slot by slot from the picture, with the grooves
 * and the selection drawn into it; the ball is drawn from a view's table
 * (render.asm: the texel and shade of every pixel, made on the host from
 * the original's camera and light). Turning the ball adds to the texel's
 * longitude; a twist turns the band's rows of the texture (by DMA, from a
 * copy); a flip draws from a second texture laid out round the flip's axis
 * (TF, made from the first by the same renderer), where the flip is a turn
 * too.
 */
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "bt.h"
#include "ball.h"

uint8_t ball_cols[BT_MAX_S + 1];
uint8_t ball_rows[BT_MAX_B + 1];
static uint8_t g_bands, g_segs, src_page, half_seg;
static uint8_t brow[96];                  /* each texture row's band */
static uint8_t home_b[BT_MAX_T], home_s[BT_MAX_T];
static uint8_t slots[BT_MAX_T * 2];       /* the puzzle's pieces (home, orient), copied */

static void set_mmu(uint8_t slot, uint8_t page) { ZXN_WRITE_REG(0x50 + slot, page); }

void logic_map(void)
{
    set_mmu(0, LOGIC_PAGE);
    set_mmu(1, LOGIC_PAGE + 1);
    set_mmu(2, RULES_PAGE);
    set_mmu(3, 11);
}

#define restore_low logic_map

/* the picture's palette (224 entries: shade k, colour c at k * 32 + c) and
   tile classes, from the meta pages (mapped at 0x4000-0x7FFF) */
static uint8_t cls[256];

static void load_meta(uint16_t off, uint8_t grid, BtPuzzle *p, uint8_t classes)
{
    uint8_t k, c, h, n = g_bands * g_segs;
    const uint8_t *m;
    set_mmu(2, META_PAGE + (off >> 13));
    set_mmu(3, META_PAGE + (off >> 13) + 1);
    m = (const uint8_t *)(0x4000 + (off & 0x1FFF));
    ZXN_WRITE_REG(0x43, 0x10);               /* Layer 2's first palette, auto-increment */
    for (k = 0; k < 8; k++) {
        ZXN_WRITE_REG(0x40, k << 5);
        for (c = 0; c < 28; c++, m += 2) {
            ZXN_WRITE_REG(0x44, m[1] << 7 | m[0] >> 1);
            ZXN_WRITE_REG(0x44, m[0] & 1);
        }
    }
    if (classes) memcpy(cls, m + grid * 256, 256);
    logic_map();                             /* (the rules are back at 0x4000) */
    if (classes)
        for (h = 0; h < n; h++) bt_set_sig(p, h, cls[2 * h], cls[2 * h + 1]);
}

/* the write window (0x0000-0x3FFF) on third t of bank b */
static void write_bank(uint8_t b) { ZXN_WRITE_REG(0x13, b); }
static void write_third(uint8_t t) { z80_outp(0x123B, 0x0B | (t << 6)); }
static void write_off(void) { z80_outp(0x123B, 0x02); }

static uint8_t seg_w(uint8_t s)
{
    return (uint8_t)((s + 1 == g_segs ? 0 : ball_cols[s + 1]) - ball_cols[s]);
}

/* main_os4.c boing_picture: red and white squares, one per tile */
static void boing_picture(BtPuzzle *p)
{
    uint8_t v, s, b;
    write_bank(BOING_BANK);
    for (v = 0; v < 96; v++) {
        uint8_t *dst = (uint8_t *)((uint16_t)(v & 63) << 8);
        write_third(v >> 6);
        b = brow[v];
        for (s = 0; s < g_segs; s++)
            rv_fill(dst + ball_cols[s], (b + s) & 1 ? 0 : 1, seg_w(s));
    }
    write_off();
    for (v = 0; v < g_bands * g_segs; v++) {
        b = (home_b[v] + home_s[v]) & 1;
        bt_set_sig(p, v, b, b);              /* a square looks the same turned round */
    }
}

void ball_picture(uint8_t pic, uint8_t bands, uint8_t segs, BtPuzzle *p)
{
    uint8_t i, b;
    g_bands = bands;
    g_segs = segs;
    half_seg = segs >> 1;
    for (i = 0; i <= segs; i++) ball_cols[i] = (uint8_t)(((uint16_t)i * 512u / segs + 1) >> 1);
    ball_cols[segs] = 0;                     /* (256: the next texel after the last is 0, never used) */
    for (i = 0; i <= bands; i++) ball_rows[i] = (uint8_t)(i * 96u / bands);
    for (b = 0; b < bands; b++)
        for (i = ball_rows[b]; i < ball_rows[b + 1]; i++) brow[i] = b;
    for (i = 0; i < bands * segs; i++) {
        home_b[i] = i / segs;
        home_s[i] = i % segs;
    }
    load_meta(pics[pic].meta, bands == 4 ? 0 : bands == 6 ? 1 : 2, p, pic != 0);
    if (pic == 0) {
        src_page = BOING_PAGE;
        boing_picture(p);
    } else
        src_page = pics[pic].page;
}

uint8_t ball_half_u(uint8_t half)
{
    /* (half + segs / 4) * 256 / segs, rounded */
    return (uint8_t)((((uint16_t)half * 4 + g_segs) * 128u / g_segs + 1) >> 1);
}

static uint8_t prog[BT_MAX_S * 5];        /* a band's slots for rv_row */

void ball_build(const BtPuzzle *p, uint8_t flags, uint8_t sel_band, uint8_t sel_half, uint8_t only)
{
    uint8_t v, s, b, dv, third = 0xFF, h, base, home, o, ws, wh, *r, b0 = 0, b1 = g_bands;
    uint8_t *dst;
    if (only < g_bands) { b0 = only; b1 = only + 1; }
    memcpy(slots, p->slot, (uint16_t)g_bands * g_segs * 2);
    set_mmu(0, src_page);
    set_mmu(1, src_page + 1);
    set_mmu(2, src_page + 2);
    write_bank(TEX_BANK);
    h = ball_rows[1];                        /* rows a band */
    for (b = b0; b < b1; b++) {
        /* the band's slots: where each piece's row starts in the picture */
        base = b * g_segs;
        for (s = 0, r = prog; s < g_segs; s++, r += 5) {
            if (flags & BB_PEEK) {
                home = base + s;
                o = 0;
            } else {
                home = slots[(uint16_t)(base + s) * 2];
                o = slots[(uint16_t)(base + s) * 2 + 1] & 1;
            }
            ws = seg_w(s);
            wh = seg_w(home_s[home]);
            r[0] = o ? ball_cols[home_s[home]] + wh - 1 : ball_cols[home_s[home]];
            r[1] = ball_rows[home_b[home]] + (o ? h - 1 : 0);
            r[2] = ws < wh ? ws : wh;
            r[3] = ws;
            r[4] = o;
        }
        for (dv = 0; dv < h; dv++) {
            v = ball_rows[b] + dv;
            dst = (uint8_t *)((uint16_t)(v & 63) << 8);
            if ((v >> 6) != third) { third = v >> 6; write_third(third); }
            if (dv == 0) rv_fill(dst, C_GROOVE, 256);   /* the groove along the band's top */
            else rv_row(dst, prog, g_segs, dv);
        }
    }
    logic_map();                             /* ring_uv is in the logic bank: read there, written through the window */
    if (flags & BB_MARKS) {
        /* the half a flip turns: its two meridians and the ring round its axis */
        uint8_t c1 = ball_cols[sel_half], c2 = ball_cols[(sel_half + half_seg) % g_segs], au = ball_half_u(sel_half);
        uint16_t i;
        for (v = 0; v < 96; v++) {
            dst = (uint8_t *)((uint16_t)(v & 63) << 8);
            if ((v >> 6) != third) { third = v >> 6; write_third(third); }
            dst[c1] = C_HALF;
            dst[c2] = C_HALF;
        }
        for (i = 0; i < NRING; i++) {
            v = ring_uv[i][1];
            if ((v >> 6) != third) { third = v >> 6; write_third(third); }
            ((uint8_t *)((uint16_t)(v & 63) << 8))[(uint8_t)(ring_uv[i][0] + au)] = C_HALF;
        }
        /* the band a twist turns: yellow rings along its edges */
        for (b = sel_band; b <= sel_band + 1; b++) {
            if (b == 0 || b == g_bands) continue;
            v = ball_rows[b];
            if ((v >> 6) != third) { third = v >> 6; write_third(third); }
            rv_fill((uint8_t *)((uint16_t)(v & 63) << 8), C_RING, 256);
        }
    }
    write_off();
}

static void map_tex(void)
{
    set_mmu(0, TEX_PAGE);
    set_mmu(1, TEX_PAGE + 1);
    set_mmu(2, TEX_PAGE + 2);
    set_mmu(3, SAVE_PAGE);
}

void ball_twist_begin(uint8_t band)
{
    uint8_t i, n = ball_rows[band + 1] - ball_rows[band];
    map_tex();
    for (i = 0; i < n; i++)
        dma_copy((void *)(0x6000 + ((uint16_t)i << 8)), (void *)((uint16_t)(ball_rows[band] + i) << 8), 256);
    restore_low();
}

void ball_twist_shift(uint8_t band, uint8_t d)
{
    uint8_t i, n = ball_rows[band + 1] - ball_rows[band];
    map_tex();
    for (i = 0; i < n; i++) {
        uint8_t *row = (uint8_t *)((uint16_t)(ball_rows[band] + i) << 8);
        uint8_t *save = (uint8_t *)(0x6000 + ((uint16_t)i << 8));
        if (d == 0) dma_copy(row, save, 256);
        else {
            dma_copy(row + d, save, 256 - d);
            dma_copy(row, save + 256 - d, d);
        }
    }
    restore_low();
}

void ball_flip_begin(uint8_t half)
{
    rv_page = v_ct.page;
    rv_cx = v_ct.cx;
    rv_cy = v_ct.cy;
    rv_off = ball_half_u(half);
    rv_bank = TF_BANK;
    rv_tex = TEX_PAGE;
    rv_draw();
}

void ball_draw(const View *v, uint8_t off, int16_t x, int16_t y)
{
    rv_page = v->page;
    rv_cx = x;
    rv_cy = y;
    rv_off = off;
    rv_bank = l2_back;
    rv_tex = TEX_PAGE;
    rv_draw();
}

void ball_draw_flip(uint8_t pitch, uint8_t half, uint8_t phi)
{
    rv_page = v_flip[pitch].page;
    rv_cx = v_flip[pitch].cx;
    rv_cy = v_flip[pitch].cy;
    rv_off = (uint8_t)-phi;
    rv_bank = l2_back;
    rv_tex = TF_PAGE;
    rv_draw();
    if (v_fstat[pitch].page) {               /* the other half, at the rim at steep tilts */
        rv_page = v_fstat[pitch].page;
        rv_off = ball_half_u(half);
        rv_tex = TEX_PAGE;
        rv_draw();
    }
}
