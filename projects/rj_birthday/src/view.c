/*
 * view.c - RJ's Birthday Bash's picture on the Next (the original's
 * draw.c and rooms.c, for the play screen).
 *
 * Layer 2, 320 x 256: rows 0..15 are the HUD bar (draw_hud's), drawn once;
 * rows 16..255 a ring of the house's 320 columns in view, copied in from
 * the column RLE as the camera moves (sdk l2pic) and shown by the
 * hardware X scroll. A copper list splits the screen at row 16: above it
 * Layer 2 and the tilemap are not scrolled (the HUD), below it both
 * scroll with the camera; from row 240 the tilemap is unscrolled again
 * (the room's name at the bottom).
 *
 * The tilemap (40 x 32) is the text: the HUD, and in the scrolled part the
 * text that belongs to the house: name tags, speech bubbles, the
 * messages. A text's tile column is its world x / 8 (mod 40): it moves with
 * the house. port: so text sits on an 8-pixel grid in the house (a guest's
 * name follows the guest in 8-pixel steps; a message centred on the screen
 * is redrawn when the camera moves 8 pixels).
 *
 * Sprites: everything else, in draw.c's order (items, guests, Boing balls,
 * the police, RJ, the puffs: a higher sprite number is drawn over).
 */
#include <string.h>
#include <stdio.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nextspr.h"
#include "nexttile.h"
#include "nextcopper.h"
#include "nextsplit.h"
#include "l2pic.h"
#include "game.h"
#include "gfx_data.h"
#include "view.h"

uint8_t rom_font[768];

#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))

/* tilemap palette offsets: (ink, background) - pen 1 the ink, pen 3 the
   background of the opaque font, pen 2 a dark shadow */
static const uint8_t pen_ink[TX_COUNT][2] = {
    { COL_WHITE, COL_BG }, { COL_BTYELLOW, COL_BG }, { COL_BLUE, COL_WHITE }, { COL_RED, COL_BTYELLOW },
    { COL_RED, COL_BG }, { COL_YELLOW, COL_BG }, { COL_LTGREEN, COL_BG }, { COL_GREY, COL_BG },
    { COL_DKBROWN, COL_BG }, { COL_PINK, COL_BG }, { COL_LTBLUE, COL_BG }, { COL_ORANGE, COL_BG },
    { COL_GREEN, COL_BG }, { COL_WHITE, COL_DKBROWN }, { COL_BTYELLOW, COL_DKBROWN }, { COL_BG, COL_BTYELLOW },
};

/* the original's 16 colours, RGB333 (from the Layer 2 palette's middle shades) */
static uint16_t col9(uint8_t c) { return l2_pal[1 + c * 5 + 2]; }

/* ---- text ---- */
static void font_tiles(uint8_t first, uint8_t bg)
{
    uint8_t k, y, x, def[32];
    for (k = 0; k < 96; k++) {
        const uint8_t *g = rom_font + k * 8;
        uint8_t *d = def;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x += 2, d++) {
                uint8_t a = g[y] & (0x80 >> x) ? 1 : bg, b = g[y] & (0x40 >> x) ? 1 : bg;
                *d = a << 4 | b;
            }
        tm_def(first + k, def, 1);
    }
}

void view_text_init(void)
{
    static const uint8_t hat[8] = { 0x10, 0x10, 0x38, 0x38, 0x7C, 0x7C, 0xFE, 0x00 };   /* draw_hud's party hat */
    uint8_t k, def[32];
    tm_init();
    font_tiles(T_FONT, 0);
    font_tiles(T_FONTB, 3);
    tm_font(T_HAT, hat, 1, 1, 0);
    memset(def, 0x33, sizeof(def));
    tm_def(T_SOLID, def, 1);
    for (k = 0; k < TX_COUNT; k++) {
        tm_pal(k, 1, col9(pen_ink[k][0]));
        tm_pal(k, 2, RGB9(0, 0, 0));
        tm_pal(k, 3, col9(pen_ink[k][1]));
    }
    tm_pal(TX_HAT, 1, col9(COL_RED));
}

void view_print(uint8_t col, uint8_t row, const char *s, uint8_t pen, uint8_t opaque)
{
    tm_print(col, row, s, opaque ? T_FONTB : T_FONT, pen);
}

void view_cprint(uint8_t row, const char *s, uint8_t pen, uint8_t opaque)
{
    view_print(20 - (uint8_t)(strlen(s) + 1) / 2, row, s, pen, opaque);
}

/* ---- the house on Layer 2 ---- */
static int16_t cam_shown = -1;
static uint16_t scroll;

static void hud_bar(void)
{
    uint16_t x;
    uint8_t s2 = ZXN_READ_REG(0x52), s3 = ZXN_READ_REG(0x53);
    ZXN_WRITE_REG(0x52, HUD_PAGE);
    for (x = 0; x < 320; x++) {
        ZXN_WRITE_REG(0x53, l2w_bank * 2 + (uint8_t)(x >> 5));
        memcpy((uint8_t *)0x6000 + ((x & 31) << 8), (const uint8_t *)0x4000 + x * 16, 16);
    }
    ZXN_WRITE_REG(0x52, s2);
    ZXN_WRITE_REG(0x53, s3);
}

void view_init(void)
{
    l2w_init(9);
    pal_load(1, l2_pal, L2_COLOURS);
    pal_load(2, spr_pal, 256);
    spr_patterns_from_pages(SPR_BANK * 2, 2, 0, SPR_PATTERNS, 10);
    l2pic_init(INDEX_PAGE, WORLD_COLS + 320, HUD_H);          /* the title's columns follow the house's */
}

/* the copper: no scroll above row 16 (the HUD), the camera's below, the
   tilemap unscrolled again from row 240 (the room's name) */
static void split(uint16_t s)
{
    split_begin();
    copper_move(0x16, 0);
    copper_move(0x71, 0);
    copper_move(0x2F, 0);
    copper_move(0x30, 0);
    split_row(HUD_H);
    copper_move(0x16, (uint8_t)s);
    copper_move(0x71, (uint8_t)(s >> 8) & 1);
    copper_move(0x2F, (uint8_t)(s >> 8) & 3);
    copper_move(0x30, (uint8_t)s);
    split_row(240);
    copper_move(0x2F, 0);
    copper_move(0x30, 0);
    split_end();
}

void view_house(void)
{
    hud_bar();
    l2pic_reset();
    cam_shown = -1;
    z80_outp(0x123B, 0x02);
}

void view_camera(int16_t cam)
{
    if (cam == cam_shown) return;
    cam_shown = cam;
    scroll = l2pic_view(cam);
    split(scroll);
}

void view_title(void)
{
    uint16_t x;
    copper_stop();
    l2pic_scroll(0);
    ZXN_WRITE_REG(0x2F, 0);
    ZXN_WRITE_REG(0x30, 0);
    l2w_fill(0, 0, 320, HUD_H, 1 + COL_BG * 5 + 2);
    for (x = 0; x < 320; x++) l2pic_column(x, TITLE_COL0 + x);
    l2pic_reset();
    cam_shown = -1;
    z80_outp(0x123B, 0x02);
}

void view_blank(void)
{
    copper_stop();
    l2pic_scroll(0);
    ZXN_WRITE_REG(0x2F, 0);
    ZXN_WRITE_REG(0x30, 0);
    l2w_fill(0, 0, 320, 256, 1 + COL_BG * 5 + 2);
    l2pic_reset();
    cam_shown = -1;
}

/* ---- text in the house (the scrolled tilemap) ---- */
typedef struct { int16_t col; uint8_t row, len, pen, opaque, used; int16_t cam; char s[24]; } WText;
static WText wt[WT_COUNT];

static void wt_cells(WText *t, uint8_t erase)
{
    int16_t c0 = cam_shown >> 3, i;
    for (i = 0; i < t->len; i++) {
        int16_t c = t->col + i;
        if (c < c0 || c >= c0 + 40) continue;          /* off the screen (the map is only 40 wide) */
        if (erase) tm_put((uint8_t)(c % 40), t->row, 0, 0);
        else tm_put((uint8_t)(c % 40), t->row, (t->opaque ? T_FONTB : T_FONT) + t->s[i] - 32, t->pen);
    }
}

/* text slot k: s at world pixel (wx, y) (s = 0: none) */
void view_wtext(uint8_t k, int16_t wx, int16_t y, const char *s, uint8_t pen, uint8_t opaque)
{
    WText *t = &wt[k];
    int16_t col = wx >> 3;
    uint8_t row = (uint8_t)(y >> 3), len;
    if (!s || !*s || y < 16 || y >= 240) {
        if (t->used) {
            wt_cells(t, 1);
            t->used = 0;
        }
        return;
    }
    len = (uint8_t)strlen(s);
    if (len > 23) len = 23;
    if (t->used && t->col == col && t->row == row && t->pen == pen && t->opaque == opaque && t->cam == (cam_shown >> 3) &&
        t->len == len && !memcmp(t->s, s, len))
        return;
    if (t->used) wt_cells(t, 1);
    t->col = col;
    t->row = row;
    t->len = len;
    t->pen = pen;
    t->opaque = opaque;
    t->cam = cam_shown >> 3;
    memcpy(t->s, s, len);
    t->used = 1;
    wt_cells(t, 0);
}

void view_wtext_clear(void)
{
    uint8_t k;
    for (k = 0; k < WT_COUNT; k++) wt[k].used = 0;
}

/* ---- sprites ---- */
static uint8_t ns;

static void show(uint8_t img, uint8_t pat, uint8_t rows, int16_t sx, int16_t sy, uint8_t pal)
{
    int16_t x = sx - sprite_anchor[img][0], y = sy - sprite_anchor[img][1];
    if (x <= -16 || x >= 320 || y <= -32 || y >= 256 || ns > 126 - rows) return;
    ns = sprg_show_ex(ns, x, y, 1, rows, pat, 0, pal, 0);
}

static uint8_t guest_pal(WORD colour)
{
    static const uint8_t order[16] = { 0, 0, 0, 0, 0, 1, 2, 7, 6, 3, 4, 5, 0, 0, 0, 0 };   /* COL_* -> block */
    return order[colour & 15];
}

static const uint8_t item_img[11] = { 0, SP_ITEM1, SP_ITEM2, SP_ITEM3, SP_ITEM4, SP_ITEM5, SP_ITEM6, SP_ITEM7,
                                      SP_ITEM8, SP_ITEM9, SP_ITEM10 };
static const uint8_t item_pat[11] = { 0, PAT_ITEM1, PAT_ITEM2, PAT_ITEM3, PAT_ITEM4, PAT_ITEM5, PAT_ITEM6, PAT_ITEM7,
                                      PAT_ITEM8, PAT_ITEM9, PAT_ITEM10 };

void view_sprites(const GameState *gs)
{
    int16_t cam = gs->camera_x;
    uint8_t i;
    ns = 0;
    for (i = 0; i < MAX_ITEMS; i++) {                          /* draw_items */
        const Item *it = &gs->items[i];
        uint8_t t;
        if (!it->active) continue;
        t = it->type > 10 ? ITEM_DEMO : (uint8_t)it->type;
        show(item_img[t], item_pat[t], t == ITEM_SCOOTER ? 2 : 1, it->x - cam, it->y, 0);
    }
    for (i = 0; i < MAX_GUESTS; i++) {                         /* draw_guests */
        const Guest *g = &gs->guests[i];
        int16_t sx;
        uint8_t st;
        if (!g->active) continue;
        sx = g->x - cam;
        st = g->state == GUEST_HAPPY ? 1 : g->state == GUEST_ANGRY ? 2 : 0;
        show(SP_GUEST0 + st, PAT_GUEST0 + st * 2, 2, sx, g->y, guest_pal(g->color));
        if (!(g->bubble_timer > 0 && g->bubble_text[0]) && g->state == GUEST_WANT && g->bubble_timer > 0)
            show(SP_WANT, PAT_WANT, 1, sx + 5, g->y - GUEST_H - 12, (uint8_t)(g->want > 7 ? 0 : g->want));
        if (g->state == GUEST_WANT && g->patience_max > 0) {
            int16_t lv = (int16_t)((int32_t)g->patience * 5 / g->patience_max);
            if (lv < 0) lv = 0;
            if (lv > 5) lv = 5;
            show(SP_PATIENCE0 + lv, PAT_PATIENCE0 + lv, 1, sx, g->y - GUEST_H - 3, 0);
        }
    }
    for (i = 0; i < MAX_BOINGS; i++) {                         /* draw_boings */
        const BoingBall *b = &gs->boings[i];
        int16_t sx;
        uint8_t ph;
        if (!b->active) continue;
        sx = b->x - cam;
        ph = (uint8_t)((b->rot >> 2) % 6);
        show(SP_BOING0 + ph, PAT_BOING0 + ph, 1, sx, b->y, 0);
        show(SP_BSHADOW, PAT_BSHADOW, 1, sx, FLOOR_Y, 0);
    }
    for (i = 0; i < MAX_COPS; i++) {                           /* draw_cops */
        const PotCop *c = &gs->cops[i];
        uint8_t k;
        if (!c->active) continue;
        if (c->cooldown > 0 && (c->cooldown & 4)) continue;    /* flashing */
        k = (uint8_t)((c->frame & 1) + (c->chase ? 2 : 0));
        show(SP_COP00 + k, PAT_COP00 + k * 2, 2, c->x - cam, c->y, 0);
    }
    {                                                          /* draw_player */
        const Player *p = &gs->player;
        int16_t sx = p->x - cam, sy = p->y;
        show(SP_RJ0 + (p->frame & 1), PAT_RJ0 + (p->frame & 1) * 2, 2, sx, sy, 0);
        if (p->carrying != ITEM_NONE && p->carrying <= 10)     /* port: the item itself in RJ's hand */
            show(item_img[p->carrying], item_pat[p->carrying], 1, sx + 12, sy - PLAYER_H + 18, 0);
        if (gs->arcade_timer > 0 && (gs->arcade_timer & 4)) {  /* sparks */
            show(SP_SPARK, PAT_SPARK, 1, sx - 3, sy - PLAYER_H - 2, 0);
            show(SP_SPARK, PAT_SPARK, 1, sx + 13, sy - PLAYER_H - 2, 0);
            show(SP_SPARK, PAT_SPARK, 1, sx - 3, sy - 4, 0);
            show(SP_SPARK, PAT_SPARK, 1, sx + 13, sy - 4, 0);
        }
    }
    for (i = 0; i < MAX_PUFFS; i++) {                          /* draw_puffs */
        const SmokePuff *p = &gs->puffs[i];
        uint8_t s;
        if (p->life <= 0) continue;
        s = p->size < 2 ? 2 : p->size > 5 ? 5 : (uint8_t)p->size;
        show(SP_PUFF2 + s - 2, PAT_PUFF2 + s - 2, 1, p->x - cam, p->y,
             p->life > PUFF_LIFE / 2 ? 0 : p->life > PUFF_LIFE / 4 ? 1 : 2);
    }
    sprg_hide_from(ns);
}

void view_hide_sprites(void) { sprg_hide_from(0); }
