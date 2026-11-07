/*
 * Emberkeep - the Next's screen (replaces the original's render.cpp).
 *
 * The original draws the whole 320 x 256 frame every frame (tiles, sprites,
 * then four black rectangles around the lantern, then the HUD). Here:
 *
 * - Layer 2 in its 320 x 256 mode holds the chamber, drawn once a level from
 *   the original's tiles (256 colours, tools/gen_gfx.py); a pickup redraws
 *   one 16 x 16 tile, the open portal flickers by redrawing its tile.
 * - The player and the guards are 16 x 16 hardware sprites.
 * - The lantern is the clip window of Layer 2 and of the sprites: outside
 *   it they aren't shown and the screen is the fallback black. The copper
 *   narrows the window on the lines of the light square's corners (the
 *   original's chopped corners) - no CPU time per pixel at all.
 * - The HUD (score, lantern bar, level / runes / lives) is the tilemap, on
 *   top, along rows 30-31.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "nextcopper.h"
#include "nextsplit.h"
#include "game.h"
#include "gfx_data.h"
#include "ekscreen.h"

#define ROM_FONT ((const uint8_t *)0x3D00)
#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))

#define HUD_ROW   30                    /* tilemap rows 30-31: the 16 lines below the chamber */
#define BAR_TILE  160                   /* lantern bar tiles: BAR_TILE + 0..8 filled pixels */
#define BAR_COL   12                    /* 15 cells: the original's 120-pixel bar */
#define BAR_CELLS 15

/* tilemap palettes (pen 1 the text, pen 2 its background) */
static const uint16_t tm_colours[TP_COUNT] = {
    RGB9(7, 7, 7),          /* TP_WHITE */
    RGB9(7, 7, 1),          /* TP_YELLOW */
    RGB9(1, 6, 1),          /* TP_GREEN */
    RGB9(7, 2, 2),          /* TP_RED */
    RGB9(7, 5, 1),          /* TP_ORANGE */
    RGB9(4, 4, 4),          /* TP_GREY */
    RGB9(2, 6, 7),          /* TP_CYAN */
    RGB9(2, 4, 7),          /* TP_BLUE */
};

/* ------------------------------------------------------------ Layer 2 tiles */

void scr_tile(uint8_t col, uint8_t row, uint8_t tile)
{
    const uint8_t *src = gfx_tiles[tile];
    uint16_t x = (uint16_t)col << 4;
    uint8_t j, y = row << 4;
    for (j = 0; j < 16; j++, src += 16)
        memcpy(l2w_column(x + j) + y, src, 16);
    l2w_done();
}

/* the tile a grid cell shows (render.cpp's draw_tile) */
static uint8_t tile_of(uint8_t col, uint8_t row)
{
    switch (gs.grid[row][col]) {
    case T_WALL:   return ((col + row) & 1) ? TILE_WALL_A : TILE_WALL_B;
    case T_RUNE:   return TILE_RUNE;
    case T_OIL:    return TILE_OIL;
    case T_HAZARD: return TILE_HAZARD;
    case T_PORTAL:
        if (gs.runes_remaining == 0 && ((gs.tick / 6) & 1)) return TILE_PORTAL_OPEN;
        return TILE_PORTAL_LOCKED;
    }
    return TILE_FLOOR;
}

static uint8_t portal_col, portal_row, portal_shown;

void scr_cell(uint8_t col, uint8_t row)
{
    scr_tile(col, row, tile_of(col, row));
}

void scr_room(void)
{
    uint8_t col, row;
    portal_shown = 255;
    for (row = 0; row < GRID_H; row++)
        for (col = 0; col < GRID_W; col++) {
            if (gs.grid[row][col] == T_PORTAL) {
                portal_col = col;
                portal_row = row;
            }
            scr_cell(col, row);
        }
}

/* the open portal flickers (its tile every 6 frames) */
void scr_portal(void)
{
    uint8_t t;
    if (gs.grid[portal_row][portal_col] != T_PORTAL) return;
    t = tile_of(portal_col, portal_row);
    if (t != portal_shown) {
        scr_tile(portal_col, portal_row, t);
        portal_shown = t;
    }
}

void scr_l2_clear(void)
{
    l2w_fill(0, 0, 320, 256, 0);
}

/* ------------------------------------------------------------ sprites */

void scr_sprites(uint8_t show)
{
    static const uint8_t pose[4] = { SPR_POSE_UP, SPR_POSE_RIGHT, SPR_POSE_DOWN, SPR_POSE_LEFT };
    const Enemy *E = gs.enemies;
    uint8_t i;
    if (!show) {
        for (i = 0; i <= MAX_ENEMIES; i++) spr_hide(i);
        return;
    }
    for (i = 0; i < MAX_ENEMIES; i++, E++) {
        uint8_t base;
        if (i >= gs.num_enemies || !E->alive) {
            spr_hide(i + 1);
            continue;
        }
        base = E->colour == 5 ? SPR_ENEMY_B : E->colour == 6 ? SPR_ENEMY_C : SPR_ENEMY_A;
        spr_show(i + 1, E->x, E->y, base + ((E->frame >> 2) & 7), 0);
    }
    spr_show(0, gs.player.x, gs.player.y, pose[gs.player.facing & 3], 0);   /* the player on top */
}

/* ------------------------------------------------------------ the lantern */

static int16_t lamp_key[3] = { -1, -1, -1 };

/* a clip window for Layer 2 and the sprites: x in pixel pairs */
static void clip_both(uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2)
{
    copper_move(0x1C, 0x03);            /* reset the Layer 2 and sprite clip indices */
    copper_move(0x18, x1);
    copper_move(0x18, x2);
    copper_move(0x18, y1);
    copper_move(0x18, y2);
    copper_move(0x19, x1);
    copper_move(0x19, x2);
    copper_move(0x19, y1);
    copper_move(0x19, y2);
}

/* x1..x2 (pixels, inclusive) as pairs: a lit pixel pair if either is lit */
#define PAIR(x) ((uint8_t)((uint16_t)(x) >> 1))

/* render.cpp's apply_lantern_mask as clip windows: the light square
   [lx, rx) x [ty, by) around the player's centre, its corners cut by r / 3
   where they aren't at the screen's edge */
void scr_lantern(uint8_t on)
{
    int16_t cx, cy, r, lx, rx, ty, by, chop;
    uint8_t cut_tl, cut_tr, cut_bl, cut_br;

    if (!on) {
        if (lamp_key[0] == -2) return;
        lamp_key[0] = -2;
        split_begin();
        clip_both(0, 159, 0, 255);
        split_end();
        return;
    }
    cx = gs.player.x + 8;
    cy = gs.player.y + 8;
    if (cx == lamp_key[0] && cy == lamp_key[1] && gs.lantern == lamp_key[2]) return;
    lamp_key[0] = cx;
    lamp_key[1] = cy;
    lamp_key[2] = gs.lantern;

    /* the radius shrinks as the lantern runs low: 40 full, 8 empty */
    r = 8 + (int16_t)((uint16_t)(LIGHT_RADIUS_PX - 8) * (uint16_t)gs.lantern / LANTERN_MAX);
    if (r < 8) r = 8;
    lx = cx - r; if (lx < 0) lx = 0;
    rx = cx + r; if (rx > PLAY_W) rx = PLAY_W;
    ty = cy - r; if (ty < 0) ty = 0;
    by = cy + r; if (by > PLAY_H) by = PLAY_H;
    chop = r / 3;
    cut_tl = ty > 0 && lx > 0;
    cut_tr = ty > 0 && rx < PLAY_W;
    cut_bl = by < PLAY_H && lx > 0;
    cut_br = by < PLAY_H && rx < PLAY_W;

    split_begin();
    /* the top band (rows ty .. ty + chop - 1) */
    clip_both(PAIR(lx + (cut_tl ? chop : 0)), PAIR(rx - 1 - (cut_tr ? chop : 0)), ty, by - 1);
    if (chop > 0) {
        split_row(ty + chop);           /* the middle: the whole width */
        clip_both(PAIR(lx), PAIR(rx - 1), ty, by - 1);
        split_row(by - chop);           /* the bottom band */
        clip_both(PAIR(lx + (cut_bl ? chop : 0)), PAIR(rx - 1 - (cut_br ? chop : 0)), ty, by - 1);
    }
    split_end();
}

void scr_lantern_reset(void)
{
    lamp_key[0] = -1;
}

/* ------------------------------------------------------------ tilemap text + HUD */

/* two upper-case fonts (' ' .. '_'): see-through for text over the
   picture (tiles 96..159), and on the HUD's strip (tiles 32..95) */
#define FONT_CLEAR 96
#define FONT_HUD   32

void tm_text(uint8_t col, uint8_t row, const char *s, uint8_t pal)
{
    tm_print(col, row, s, FONT_CLEAR, pal);
}

void tm_centre(uint8_t row, const char *s, uint8_t pal)
{
    uint8_t n = (uint8_t)strlen(s);
    tm_print((uint8_t)((TM_COLS - n) / 2), row, s, FONT_CLEAR, pal);
}

static char numbuf[11];

static const char *digits_of(uint32_t v, uint8_t digits)
{
    uint8_t i = digits;
    numbuf[digits] = 0;
    while (i--) {
        numbuf[i] = '0' + (uint8_t)(v % 10);
        v /= 10;
    }
    return numbuf;
}

void tm_num(uint8_t col, uint8_t row, uint32_t v, uint8_t digits, uint8_t pal)
{
    tm_print(col, row, digits_of(v, digits), FONT_CLEAR, pal);
}

static void hud_text(uint8_t col, const char *s, uint8_t pal)
{
    tm_print(col, HUD_ROW, s, FONT_HUD, pal);
}

static void hud_num(uint8_t col, uint32_t v, uint8_t digits, uint8_t pal)
{
    hud_text(col, digits_of(v, digits), pal);
}

/* lantern bar cells: 0..8 pixels filled (pen 1), the rest the bar's grey
   (pen 3), a line of the HUD's background (pen 2) above and below */
static void bar_tiles(void)
{
    static uint8_t def[32];
    uint8_t f, y, x;
    for (f = 0; f <= 8; f++) {
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 8; x += 2) {
                uint8_t a = (y == 0 || y == 7) ? 2 : (x < f ? 1 : 3);
                uint8_t b = (y == 0 || y == 7) ? 2 : (x + 1 < f ? 1 : 3);
                def[y * 4 + x / 2] = (a << 4) | b;
            }
        }
        tm_def(BAR_TILE + f, def, 1);
    }
}

static int16_t hud_lantern = -1, hud_lives = -1, hud_level = -1, hud_runes = -1;
static uint32_t hud_score = 0xFFFFFFFF;

void scr_hud(uint8_t force)
{
    if (force) {
        tm_fill(0, HUD_ROW, TM_COLS, 2, 1, TP_WHITE);           /* the original's dim strip */
        hud_text(1, "SCORE", TP_WHITE);
        hud_lantern = hud_lives = hud_level = hud_runes = -1;
        hud_score = 0xFFFFFFFF;
    }
    if (gs.score != hud_score) {
        hud_num(7, gs.score, 6, TP_WHITE);                      /* SCORE %06ld */
        hud_score = gs.score;
    }
    if (gs.lantern != hud_lantern) {
        /* the bar: orange, red below a quarter */
        uint16_t fill = (uint16_t)((uint32_t)gs.lantern * (BAR_CELLS * 8) / LANTERN_MAX);
        uint8_t pal = gs.lantern < LANTERN_MAX / 4 ? TP_BAR_LOW : TP_BAR, c;
        for (c = 0; c < BAR_CELLS; c++) {
            uint8_t f = fill >= 8 ? 8 : (uint8_t)fill;
            fill -= f;
            tm_put(BAR_COL + c, HUD_ROW + 1, BAR_TILE + f, pal);
        }
        hud_lantern = gs.lantern;
    }
    if (gs.level_index != hud_level || gs.runes_remaining != hud_runes || gs.lives != hud_lives) {
        /* "LV%ld R%ld/%ld L%ld" */
        hud_text(28, "LV", TP_WHITE);
        hud_num(30, gs.level_index + 1, 1, TP_WHITE);
        hud_text(32, "R", TP_YELLOW);
        hud_num(33, gs.runes_total - gs.runes_remaining, 1, TP_YELLOW);
        hud_text(34, "/", TP_YELLOW);
        hud_num(35, gs.runes_total, 1, TP_YELLOW);
        hud_text(37, "L", TP_RED);
        hud_num(38, gs.lives, 1, TP_RED);
        hud_level = gs.level_index;
        hud_runes = gs.runes_remaining;
        hud_lives = gs.lives;
    }
}

/* ------------------------------------------------------------ set-up */

void scr_init(void)
{
    uint8_t p;
    pal_load(1, gfx_pal, 256);                  /* Layer 2 */
    pal_load(2, gfx_pal, 256);                  /* sprites (0xE3: see-through) */
    l2w_init(L2_BANK);
    scr_l2_clear();

    tm_init();
    tm_font(0, ROM_FONT, 1, 1, 0);              /* tile 0: blank, see-through */
    tm_font(1, ROM_FONT, 1, 1, 2);              /* tile 1: blank, the palette's pen 2 */
    tm_font(FONT_HUD, ROM_FONT, 64, 1, 2);      /* ' ' .. '_' on the HUD strip (pen 2) */
    tm_font(FONT_CLEAR, ROM_FONT, 64, 1, 0);    /* ' ' .. '_' see-through */
    for (p = 0; p < TP_COUNT; p++) {
        tm_pal(p, 1, tm_colours[p]);
        tm_pal(p, 2, RGB9(1, 1, 1));            /* the original's HUD strip, pen 15 */
    }
    tm_pal(TP_BAR, 1, RGB9(7, 5, 2));           /* pen 10, orange */
    tm_pal(TP_BAR_LOW, 1, RGB9(7, 2, 2));       /* pen 9, red */
    tm_pal(TP_BAR, 3, RGB9(2, 2, 2));           /* pen 8, the bar's grey */
    tm_pal(TP_BAR_LOW, 3, RGB9(2, 2, 2));
    tm_pal(TP_BAR, 2, RGB9(1, 1, 1));
    tm_pal(TP_BAR_LOW, 2, RGB9(1, 1, 1));
    bar_tiles();

    spr_init();
    spr_patterns_from_pages(SPR_PAGE, 3, 0, NUM_SPRITE_PATTERNS, 11);   /* from the .nex's bank */

    ZXN_WRITE_REG(0x4A, 0);                     /* outside every layer: black */
    ZXN_WRITE_REG(0x15, 0x33);                  /* sprites on, over the border, clipped; U S L */
    scr_lantern(0);
}
