/*
 * Terra Strike - the HUD on the Next's tilemap (the original's render_hud,
 * render_title and radar): the panel under the view, the messages over it.
 *
 * port: the tilemap (40 x 32 cells of 8 x 8 over the whole 320 x 256
 * display) carries the text, so it is redrawn only when it changes; the
 * panel's layout follows the original's on 8-pixel cells; the radar is 8 x 6
 * cells whose tile definitions are redrawn every other frame. The code runs
 * from its own memory bank (src/main.c).
 */
#include <stdint.h>
#include <string.h>
#include "nexttile.h"
#include "ts.h"
#include "models.h"
#include "hud.h"
#include "points.h"
#include "nextgfx.h"
#include <z80.h>

/* the ROM font's ' '..'_', copied to 0x6000 by main.c (the ROM is paged out) */
#define ROM_FONT ((const uint8_t *)0x6000)
#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))

#define T_CLEAR    0                    /* see-through */
#define T_PANEL    1
#define FONT_PANEL 32                   /* ' '..'_' on the panel */
#define FONT_OVER  96                   /* ' '..'_' over the view */
#define T_BAR      160                  /* + 0..8 pixels filled */
#define T_RADAR    176                  /* 8 x 6 cells */
#define RADAR_COL  31
#define RADAR_ROW  26
#define RADAR_W    8
#define RADAR_H    6

#define PANEL_ROW  26                   /* the original's VIEW_H: under the 170 lines of view (row 4 + 21.25) */

/* tilemap palettes: pen 1 text, 2 the panel, 3 shadow / black */
enum { HP_WHITE, HP_GREY, HP_RED, HP_YELLOW, HP_CRAFT, HP_BAR_FUEL, HP_BAR_LOW, HP_BAR_SHLD, HP_RADAR, HP_COUNT };
static const uint16_t pen1[HP_COUNT] = {
    RGB9(7, 7, 7), RGB9(4, 4, 4), RGB9(7, 1, 0), RGB9(7, 7, 0), RGB9(4, 7, 7),
    RGB9(7, 7, 0), RGB9(7, 1, 0), RGB9(4, 7, 7), RGB9(0, 0, 0),
};

static void radar_palette(void)
{
    tm_pal(HP_RADAR, 1, 0);                 /* the radar's black */
    tm_pal(HP_RADAR, 4, RGB9(4, 4, 4));     /* frame (PEN_GREY) */
    tm_pal(HP_RADAR, 5, RGB9(7, 7, 0));     /* pad (PEN_YELLOW) */
    tm_pal(HP_RADAR, 6, RGB9(7, 1, 0));     /* tank (PEN_RED) */
    tm_pal(HP_RADAR, 7, RGB9(4, 7, 7));     /* ship (PEN_CRAFT) */
    tm_pal(HP_RADAR, 8, RGB9(7, 7, 7));     /* building, the craft (PEN_WHITE) */
}

static void bar_tiles(void)
{
    static uint8_t def[32];
    uint8_t f, y, x;
    for (f = 0; f <= 8; f++) {
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x += 2) {
                uint8_t on = y >= 2 && y < 7;           /* a 5-pixel bar, as the original's */
                uint8_t a = !on ? 2 : x < f ? 1 : 3, b = !on ? 2 : x + 1 < f ? 1 : 3;
                def[y * 4 + x / 2] = (a << 4) | b;
            }
        tm_def(T_BAR + f, def, 1);
    }
}

void hud_init(void)
{
    uint8_t p;
    tm_init();
    tm_font(T_CLEAR, ROM_FONT, 1, 1, 0);
    tm_font(T_PANEL, ROM_FONT, 1, 1, 2);
    tm_font(FONT_PANEL, ROM_FONT, 64, 1, 2);
    tm_font(FONT_OVER, ROM_FONT, 64, 1, 0);
    for (p = 0; p < HP_COUNT; p++) {
        tm_pal(p, 1, pen1[p]);
        tm_pal(p, 2, RGB9(1, 1, 1));            /* PEN_PANEL 0x333 */
        tm_pal(p, 3, 0);
    }
    radar_palette();
    bar_tiles();
    hud_reset();
}

/* ------------------------------------------------------------ text */

static char cache[8][24];

static void line(uint8_t slot, uint8_t col, uint8_t row, uint8_t w, const char *s, uint8_t pal)
{
    if (strcmp(cache[slot], s) == 0) return;
    strcpy(cache[slot], s);
    tm_fill(col, row, w, 1, T_PANEL, pal);
    tm_print(col, row, s, FONT_PANEL, pal);
}

static char *num(char *p, int32_t v, uint8_t digits)
{
    uint8_t i = digits;
    p[digits] = 0;
    if (v < 0) v = 0;
    while (i--) { p[i] = '0' + (uint8_t)(v % 10); v /= 10; }
    return p + digits;
}

static char *strput(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

/* the number without leading zeros, right-aligned in `w` */
static char *num_r(char *p, int32_t v, uint8_t w)
{
    char t[11];
    uint8_t n = 0, i;
    if (v < 0) v = 0;
    do { t[n++] = '0' + (uint8_t)(v % 10); v /= 10; } while (v && n < 10);
    for (i = n; i < w; i++) *p++ = ' ';
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static int16_t shown_fuel = -1, shown_shield = -1;
static uint8_t msg_row = 0;

void hud_reset(void)
{
    memset(cache, 0xFF, sizeof(cache));
    tm_clear();
    tm_fill(0, PANEL_ROW, TM_COLS, TM_ROWS - PANEL_ROW, T_PANEL, HP_GREY);
    tm_print(13, PANEL_ROW + 1, "FUEL", FONT_PANEL, HP_GREY);
    tm_print(13, PANEL_ROW + 2, "SHLD", FONT_PANEL, HP_GREY);
    shown_fuel = shown_shield = -1;
    msg_row = 0;
    radar_frame();
}

static void bar(uint8_t col, uint8_t row, int16_t val, int16_t max, uint8_t pal)
{
    /* 68 pixels as the original: 9 cells (72) here */
    uint16_t fill = (uint16_t)((int32_t)(val < 0 ? 0 : val) * 72 / max), c;
    for (c = 0; c < 9; c++) {
        uint8_t f = fill >= 8 ? 8 : (uint8_t)fill;
        fill -= f;
        tm_put(col + c, row, T_BAR + f, pal);
    }
}

/* ------------------------------------------------------------ the radar */

/* port: the radar's dots are hardware sprites (80..127) over its frame in
   tiles; the original redraws the radar's pixels */
#define RSPR_X (RADAR_COL * 8)          /* the radar's top left in sprite coordinates */
#define RSPR_Y (RADAR_ROW * 8)
static uint8_t radar_shown;

static void rsprite(int16_t x, int16_t y, uint8_t pattern, uint8_t pen)
{
    x += RSPR_X;
    y += RSPR_Y;
    z80_outp(0x303B, RADAR_SPRITE + radar_shown++);
    z80_outp(0x57, (uint8_t)x);
    z80_outp(0x57, (uint8_t)y);
    z80_outp(0x57, (uint8_t)(points_offset_of_pen(pen) << 4) | ((x >> 8) & 1));
    z80_outp(0x57, 0xC0 | pattern);
    z80_outp(0x57, 0);
}

static void radar_frame(void)
{
    static uint8_t def[32];
    uint8_t tx, ty, y, x;
    for (ty = 0; ty < RADAR_H; ty++)
        for (tx = 0; tx < RADAR_W; tx++) {
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x += 2) {
                    uint8_t py = ty * 8 + y, px = tx * 8 + x;
                    uint8_t edge = py == 0 || py == RADAR_H * 8 - 1;
                    uint8_t a = edge || px == 0 ? 4 : 1, b = edge || px + 1 == RADAR_W * 8 - 1 ? 4 : 1;
                    def[y * 4 + x / 2] = (a << 4) | b;
                }
            tm_def(T_RADAR + ty * RADAR_W + tx, def, 1);
            tm_put(RADAR_COL + tx, RADAR_ROW + ty, T_RADAR + ty * RADAR_W + tx, HP_RADAR);
        }
}

static void radar(const World *w)
{
    const int16_t W8 = RADAR_W * 8 - 1, H8 = RADAR_H * 8 - 1, cx = W8 / 2, cy = H8 / 2;
    int32_t px = w->p.x >> FBITS, pz = w->p.z >> FBITS, padx, padz;
    int rx, ry;
    uint8_t i, was = radar_shown;
    radar_shown = 0;
    pad_centre(w, &padx, &padz);
    radar_map(ts_wrap_delta(padx, px), ts_wrap_delta(padz, pz), w->p.yaw, 6, &rx, &ry);
    if (cx + rx > 1 && cx + rx < W8 - 2 && cy + ry > 1 && cy + ry < H8 - 2) rsprite(cx + rx - 1, cy + ry - 1, PAT_DOT + 2, PEN_YELLOW);
    for (i = 0; i < MAX_ENTS && radar_shown < 47; i++) {
        const Ent *e = &w->ents[i];
        if (e->type == ENT_NONE || e->type == ENT_TREE) continue;
        radar_map(ts_wrap_delta(e->x >> FBITS, px), ts_wrap_delta(e->z >> FBITS, pz), w->p.yaw, 6, &rx, &ry);
        if (cx + rx <= 0 || cx + rx >= W8 - 1 || cy + ry <= 0 || cy + ry >= H8 - 1) continue;
        rsprite(cx + rx, cy + ry, PAT_DOT + 1, e->type == ENT_TANK ? PEN_RED : e->type == ENT_SHIP ? PEN_CRAFT : PEN_WHITE);
    }
    rsprite(cx, cy - 2, PAT_DOT + 5, PEN_WHITE);         /* the craft */
    for (i = radar_shown; i < was; i++) spr_hide(RADAR_SPRITE + i);
}

static void message(uint8_t row, const char *s, uint8_t pal)
{
    uint8_t n = (uint8_t)strlen(s);
    tm_print((uint8_t)((TM_COLS - n) / 2), row, s, FONT_OVER, pal);
}

void hud_title(uint8_t frame)
{
    message(8, "T E R R A   S T R I K E", HP_YELLOW);
    message(10, "A SPECTRUM NEXT PORT", HP_GREY);
    message(21, "5-8 FLY      A / Z THROTTLE", HP_WHITE);
    message(22, "SPACE FIRE   P PAUSE", HP_WHITE);
    message(24, "PRESS FIRE TO START", (frame & 32) ? HP_YELLOW : HP_WHITE);
}

void hud_draw(const World *w, uint8_t paused)
{
    static uint8_t frame;
    char t[24], *q;
    const Craft *p = &w->p;
    int32_t alt = (p->y >> FBITS) - ground_height(w, p->x >> FBITS, p->z >> FBITS);
    uint8_t row = 0;
    const char *m1 = 0, *m2 = 0;
    uint8_t pal1 = HP_YELLOW, pal2 = HP_WHITE;

    q = strput(t, "SCORE ");
    num(q, w->score, 6);
    line(0, 1, PANEL_ROW + 1, 12, t, HP_WHITE);
    q = strput(t, "LIVES ");
    q = num_r(q, w->lives, 1);
    q = strput(q, " LV ");
    num_r(q, w->level, 1);
    line(1, 1, PANEL_ROW + 2, 12, t, HP_GREY);
    q = strput(t, "TARGETS ");
    num_r(q, w->targets_left, 1);
    line(2, 1, PANEL_ROW + 3, 12, t, HP_RED);
    q = strput(t, "ALT");
    q = num_r(q, alt < 0 ? 0 : alt, 4);
    q = strput(q, " SPD");
    num_r(q, p->speed >> FBITS, 3);
    line(3, 13, PANEL_ROW + 3, 16, t, HP_GREY);
    if (p->fuel != shown_fuel) {
        bar(18, PANEL_ROW + 1, p->fuel, 1000, p->fuel < 200 ? HP_BAR_LOW : HP_BAR_FUEL);
        shown_fuel = p->fuel;
    }
    if (p->shield != shown_shield) {
        bar(18, PANEL_ROW + 2, p->shield, 100, HP_BAR_SHLD);
        shown_shield = p->shield;
    }
    if ((++frame & 1) == 0) radar(w);  /* port: every 2nd screen */

    /* the original's messages over the view (rows of its y / 8, at 0.8) */
    switch (w->state) {
    case GS_PLAY:
        if (p->state == PS_LANDED) { m1 = "LANDED - UP TO LIFT OFF"; row = 7; }
        else if (p->state == PS_DEAD && w->lives > 0) { m1 = "CRAFT LOST"; row = 10; pal1 = HP_RED; }
        else if (p->fuel < 150 && !w->tune.inf_fuel && (w->frame & 16)) { m1 = "LOW FUEL - RETURN TO PAD"; row = 7; pal1 = HP_RED; }
        break;
    case GS_LEVELCLEAR:
        q = strput(t, "LEVEL ");
        num_r(q, w->level, 1);
        strput(t + strlen(t), " CLEARED");
        m1 = t;
        row = 10;
        break;
    case GS_GAMEOVER:
        m1 = "GAME OVER";
        row = 11;
        pal1 = HP_RED;
        if (w->state_timer == 0) m2 = "PRESS FIRE";
        break;
    }
    if (paused) { m1 = "PAUSED"; row = 14; pal1 = HP_WHITE; }
    /* clear the rows the messages used (4..25 is the view) */
    if (msg_row != row || row) {
        tm_fill(0, 7, TM_COLS, 9, T_CLEAR, HP_WHITE);
        msg_row = row;
    }
    if (m1) message(row, m1, pal1);
    if (w->state == GS_LEVELCLEAR) {
        q = strput(t, "BONUS ");
        num_r(q, 500 * (int32_t)w->level, 1);
        message(12, t, HP_WHITE);
    }
    if (m2) message(row + 2, m2, pal2);
    if (w->state == GS_TITLE) hud_title((uint8_t)w->frame);
}
