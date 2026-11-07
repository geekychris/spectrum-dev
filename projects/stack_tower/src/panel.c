/*
 * panel.c - the original's panel (render.c panel(): score, level, rings,
 * the next three pieces, the held one, the whole tower unrolled, the mode
 * and the time), its title panel, the words over the view (the flash, the
 * game over box, PAUSED, the title's name), the help pages and the title
 * painting's words.
 *
 * The panel is in Layer 2 next to the view, which is double buffered: each
 * buffer remembers what its panel shows and only what changed is redrawn.
 * The words over the view are drawn every frame (the view's base picture
 * covers them again). Help and the painting's words are on the tilemap.
 */
#include <string.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "zxgame.h"
#include "nextspr.h"
#include "sprtext.h"
#include "stack.h"

enum { LAY_NONE, LAY_GAME, LAY_TITLE };

typedef struct {
    uint8_t layout;
    int32_t score;
    int16_t level, lines;
    int8_t next[3], hold;
    uint16_t board_ver;
    Piece piece;
    uint8_t mode, secs, mins, blink, hs_ver;
    int16_t start_level;
} Shown;

static Shown shown[2];
static uint8_t hs_ver;                  /* bumped when the table changes */

char *fmt_u32(char *p, uint32_t v)
{
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = '0' + (char)(v % 10); v /= 10; } while (v);
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

char *fmt_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

static const char *mode_title(uint8_t m)
{
    return m == MODE_MARATHON ? "MARATHON" : m == MODE_RISING ? "RISING" : "DIG";
}

void panel_invalidate(void)
{
    shown[0].layout = shown[1].layout = LAY_NONE;
    hs_ver++;
}

/* panel text: its line cleared to the panel's colour first */
static void ptext(uint16_t x, uint8_t y, const char *s, uint8_t pen, uint8_t narrow)
{
    l2w_fill(x, y, 319 - x + 1, 8, PEN_PANEL);
    l2w_text(x, y, s, pen, narrow);
}

/* a piece as a little 2D icon (render.c icon()) */
static void icon(int8_t type, uint16_t x, uint8_t y, uint8_t sz)
{
    uint8_t i;
    const int8_t *c;
    if (type < 0) return;
    c = &st_cells[type][0][0];
    for (i = 0; i < st_ncells[type]; i++, c += 2)
        l2w_fill(x + c[0] * sz, y - c[1] * sz, sz - 1, sz - 1, lit_pen(type + 1));
    l2w_done();
}

/* the whole tower unrolled, centred on the piece (render.c map()): 16 x 20
   cells of 3 x 3 pixels at (236, 150), each map column built and copied */
static void draw_map(void)
{
    uint8_t buf[ST_ROWS * 3], j, r, k, n;
    int8_t cells[5][2];
    const StGame *g = &game;
    uint8_t mid = g->piece.col;
    n = (stack.state == SS_PLAY && !g->over) ? st_cells_of(&g->piece, cells) : 0;
    for (j = 0; j < ST_COLS; j++) {
        uint8_t col = (mid - 8 + j) & (ST_COLS - 1), *p = buf + (ST_ROWS - 1) * 3;
        const uint8_t *b = &g->board[col];
        for (r = 0; r < ST_ROWS; r++, b += ST_COLS, p -= 3) {
            uint8_t v = *b;
            p[0] = p[1] = v ? lit_pen(v) : PEN_BLACK;
            p[2] = PEN_BLACK;
        }
        for (k = 0; k < n; k++)
            if (cells[k][0] == col && cells[k][1] >= 0 && cells[k][1] < ST_ROWS) {
                p = buf + (ST_ROWS - 1 - cells[k][1]) * 3;
                p[0] = p[1] = PEN_WHITE;
            }
        memcpy(l2w_column(236 + 3 * j) + 150, buf, sizeof(buf));
        memcpy(l2w_column(237 + 3 * j) + 150, buf, sizeof(buf));
    }
    l2w_done();
}

static void box(uint16_t x0, uint8_t y0, uint16_t x1, uint8_t y1, uint8_t fill, uint8_t edge)
{
    l2w_fill(x0, y0, x1 - x0 + 1, y1 - y0 + 1, edge);
    l2w_fill(x0 + 1, y0 + 1, x1 - x0 - 1, y1 - y0 - 1, fill);
}

static void panel_game(Shown *s)
{
    char tmp[24];
    int8_t q[3];
    uint8_t k, secs, mins;
    int16_t rings;
    StGame *g = &game;
    if (s->layout != LAY_GAME) {
        memset(s, 0xFF, sizeof(*s));
        s->layout = LAY_GAME;
        l2w_fill(200, 0, 120, 256, PEN_PANEL);
        l2w_text(206, 12, "SCORE", PEN_GREY, 0);
        l2w_text(206, 70, "NEXT", PEN_GREY, 0);
        l2w_text(206, 112, "HOLD", PEN_GREY, 0);
        box(234, 148, 236 + ST_COLS * 3 + 1, 150 + ST_ROWS * 3 + 1, PEN_BLACK, PEN_GREY);
        l2w_done();
    }
    if (s->score != g->score) {
        s->score = g->score;
        fmt_u32(tmp, g->score);
        ptext(206, 24, tmp, PEN_WHITE, 0);
    }
    if (s->level != g->level) {
        s->level = g->level;
        fmt_u32(fmt_str(tmp, "LEVEL "), g->level);
        ptext(206, 40, tmp, PEN_YELLOW, 0);
    }
    rings = g->mode == MODE_DIG ? g->garbage_left : g->lines;
    if (s->lines != rings || s->mode != g->mode) {
        s->lines = rings;
        fmt_u32(fmt_str(tmp, g->mode == MODE_DIG ? "DIG " : "RINGS "), rings);
        ptext(206, 52, tmp, PEN_CYAN, 0);
    }
    st_preview(g, 3, q);
    if (memcmp(s->next, q, 3)) {
        memcpy(s->next, q, 3);
        l2w_fill(206, 80, 114, 30, PEN_PANEL);
        for (k = 0; k < 3; k++) icon(q[k], 222 + k * 30, 92, k ? 5 : 7);
    }
    if (s->hold != g->hold) {
        s->hold = g->hold;
        l2w_fill(206, 122, 60, 24, PEN_PANEL);
        icon(g->hold, 230, 132, 6);
    }
    if (s->board_ver != g->board_ver || memcmp(&s->piece, &g->piece, sizeof(Piece))) {
        s->board_ver = g->board_ver;
        s->piece = g->piece;
        draw_map();
    }
    if (s->mode != g->mode) {
        s->mode = g->mode;
        ptext(206, 228, mode_title(g->mode), PEN_GREEN, 0);
    }
    secs = (uint8_t)((g->time_ms / 1000) % 60);
    mins = (uint8_t)(g->time_ms / 60000);
    if (s->secs != secs || s->mins != mins) {
        char *p;
        s->secs = secs;
        s->mins = mins;
        p = fmt_u32(tmp, mins);
        *p++ = ':';
        *p++ = '0' + secs / 10;
        *p++ = '0' + secs % 10;
        *p = 0;
        ptext(206, 242, tmp, PEN_GREY, 0);
    }
}

static void panel_title(Shown *s)
{
    char tmp[24], *p;
    uint8_t k, blink = (frame_count >> 4) & 1;
    if (s->layout != LAY_TITLE) {
        memset(s, 0xFF, sizeof(*s));
        s->layout = LAY_TITLE;
        l2w_fill(200, 0, 120, 256, PEN_PANEL);
        l2w_text(206, 30, "MODE", PEN_GREY, 0);
        l2w_text(206, 66, "LEVEL", PEN_GREY, 0);
        l2w_text(206, 104, "HIGH SCORES", PEN_GREY, 0);
        l2w_text(206, 214, "H: RULES", PEN_GREY, 0);
        l2w_text(206, 242, "AN AMIGA GAME", PEN_GREY, 0);
        l2w_done();
    }
    if (s->mode != stack.mode || s->hs_ver != hs_ver) {
        s->mode = stack.mode;
        s->hs_ver = hs_ver;
        p = fmt_str(tmp, "< ");
        p = fmt_str(p, mode_title(stack.mode));
        fmt_str(p, " >");
        ptext(206, 44, tmp, PEN_GREEN, 0);
        for (k = 0; k < HS_ENTRIES; k++) {
            p = tmp;
            *p++ = '1' + k;
            *p++ = ' ';
            p = fmt_str(p, hs_table[k].name);
            *p++ = ' ';
            fmt_u32(p, hs_table[k].score);
            ptext(206, 118 + k * 12, tmp, k ? PEN_WHITE : PEN_YELLOW, 1);
        }
    }
    if (s->start_level != stack.start_level) {
        s->start_level = stack.start_level;
        p = fmt_str(tmp, "  ");
        p = fmt_u32(p, stack.start_level);
        fmt_str(p, "  (UP/DOWN)");
        ptext(206, 80, tmp, PEN_WHITE, 1);
    }
    if (s->blink != blink) {
        s->blink = blink;
        ptext(206, 200, "FIRE: START", blink ? PEN_YELLOW : PEN_WHITE, 0);
    }
}

void panel_draw(void)
{
    Shown *s = &shown[scr_back];
    if (stack.state == SS_TITLE) panel_title(s);
    else panel_game(s);
}

/* the original's big text (g_text_big) as sprites: the font5 font zoomed
   by the hardware, centred on column cx */
#define SPR_BIG_MAX 24
static uint8_t big_used;

static void big(uint16_t cx, uint8_t y, const char *s, uint8_t pal, uint8_t zoom)
{
    big_used = sprtext_show(big_used, cx - sprtext_width(s, zoom) / 2, y, s, pal, zoom);
}

void big_words(uint8_t show)
{
    uint8_t before = big_used;
    big_used = 0;
    if (show) {
        if (stack.state == SS_TITLE) {
            big(100, 12, "STACK", BIG_YELLOW, 2);
            big(100, 46, "TOWER", BIG_CYAN, 2);
        } else if (stack.state == SS_OVER || stack.state == SS_NAME) {
            big(100, 98, game.won ? "DUG OUT!" : "TOPPED", game.won ? BIG_GREEN : BIG_RED, 1);
        } else if (stack.flash_timer && stack.flash) {
            big(100, 30, stack.flash, BIG_YELLOW, 1);
        }
    }
    if (big_used < before) sprg_hide(big_used, before - big_used);
}

static void centre(uint8_t y, const char *s, uint8_t pen)
{
    l2w_text(100 - (uint16_t)strlen(s) * 4, y, s, pen, 0);
}

/* port: the view's box is 36..155 wide (the original's 16..184), inside
   the columns the base picture redraws every frame */
void view_text(void)
{
    char tmp[24];
    if (stack.state == SS_TITLE) return;
    if (stack.state == SS_OVER || stack.state == SS_NAME) {
        box(36, 90, 155, 164, PEN_PANEL, PEN_LINE);
        fmt_u32(fmt_str(tmp, "SCORE "), game.score);
        centre(120, tmp, PEN_WHITE);
        if (stack.state == SS_NAME) {
            extern hs_editor hs_ed;
            uint8_t k;
            centre(134, "HIGH SCORE!", PEN_YELLOW);
            fmt_str(fmt_str(tmp, "NAME "), hs_ed.name);
            centre(148, tmp, PEN_WHITE);
            for (k = 0; k < 3; k++)                  /* the letter being entered underlined */
                if (k == hs_ed.pos) l2w_fill(100 - 36 + (5 + k) * 8, 156, 7, 1, PEN_YELLOW);
            l2w_done();
        } else if (stack.state_timer == 0) {
            centre(144, "PRESS FIRE", PEN_YELLOW);
        }
        l2w_done();
        return;
    }
    if (stack.paused == 1) centre(120, "PAUSED", PEN_WHITE);
    l2w_done();
}

static const char *const help_lines[2][12] = {
    { "PIECES LAND ON THE OUTSIDE OF A",
      "ROUND TOWER OF 16 COLUMNS. THERE ARE",
      "NO SIDE WALLS: LEFT AND RIGHT TURN",
      "THE TOWER UNDER THE PIECE.",
      "",
      "FILL A WHOLE RING TO CLEAR IT - THE",
      "BACK OF THE TOWER TOO. THE MAP SHOWS",
      "THE WHOLE TOWER UNROLLED.",
      "",
      "MARATHON: FASTER EVERY 10 RINGS",
      "RISING: RUBBLE PUSHES UP FROM BELOW",
      "DIG: CLEAR 10 RINGS OF RUBBLE" },
    /* port: the Spectrum's keys */
    { "O / P          TURN THE TOWER",
      "Q / X          ROTATE CLOCKWISE",
      "Z              ROTATE ANTICLOCKWISE",
      "A              SOFT DROP",
      "SPACE          HARD DROP",
      "C              HOLD",
      "G              GHOST ON / OFF",
      "ENTER          PAUSE",
      "H              THESE PAGES",
      "BREAK          TITLE",
      "CURSOR KEYS, KEMPSTON, SINCLAIR TOO",
      "SCORE: 100 300 600 1000 1500 x LEVEL" },
};

void help_draw(uint8_t page)
{
    uint8_t k;
    tm_cover();
    if (!page) {
        tm_blank();
        return;
    }
    tm_text_box(page == 1 ? 14 : 16, 2, page == 1 ? "HOW TO PLAY" : "CONTROLS", TP_CYAN);
    for (k = 0; k < 12; k++) tm_text_box(2, 6 + k * 2, help_lines[page - 1][k], TP_WHITE);
    tm_text_box(1, 30, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", TP_GREY);
    tm_text_box(19, 30, "H: NEXT  FIRE: PLAY", TP_YELLOW);
}

/* the original's words over the title painting (render_title) */
static uint8_t tp_mode = 0xFF, tp_blink = 0xFF;
static int16_t tp_level = -1;

void title_painting_reset(void) { tp_mode = tp_blink = 0xFF; }

void title_painting_text(void)
{
    uint8_t b = (frame_count >> 4) & 1;
    char tmp[40], *p;
    if (tp_mode == stack.mode && tp_level == stack.start_level && tp_blink == b) return;
    if (tp_mode != stack.mode || tp_level != stack.start_level) {
        tp_mode = stack.mode;
        tp_level = stack.start_level;
        tm_fill(0, 27, TM_COLS, 1, 0, 0);
        p = fmt_str(tmp, "< ");
        p = fmt_str(p, mode_title(tp_mode));
        p = fmt_str(p, " >   LEVEL ");
        p = fmt_u32(p, tp_level);
        fmt_str(p, " (UP/DOWN)");
        tm_text_centre(27, tmp, TP_GREEN);
        fmt_u32(fmt_str(tmp, "BEST "), hs_table[0].score);
        p = tmp + strlen(tmp);
        fmt_str(p, "    H: RULES");
        tm_fill(0, 31, TM_COLS, 1, 0, 0);
        tm_text_centre(31, tmp, TP_GREY);
    }
    tp_blink = b;
    tm_text_centre(29, "PRESS FIRE TO START", b ? TP_YELLOW : TP_WHITE);
}

