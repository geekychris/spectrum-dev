/*
 * The HUD, the title and full time (the original's draw_hud) on the
 * Next's tilemap: 8 x 8 text over the panes, in the ROM font, on the
 * original's dark HUD boxes. The layer is scrolled 4 pixels right and down
 * so the text sits where the original's topaz text does (x 4, rows 4 and
 * 132); column 39 and row 31 stay blank (they wrap round).
 *
 * port: the original's kick-off screen is the title here, with a choice of
 * one or two players and the Spectrum's controls; at full time FIRE goes
 * back to it (the original starts the next match at once).
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "roto.h"
#include "hud.h"
#include "gfx_data.h"
#include "nexttile.h"
#include "palfx.h"

#define ROM_FONT ((const uint8_t *)0x3D00)
#define T_FONT   1                      /* characters 32..127 on the HUD box */
#define T_BIG    97                     /* 2x letters of the title */
#define BIG_CHARS "ROTBAL"

/* tilemap palettes: pen 1 text, pen 2 its box / shadow */
#define P_HUD    0
#define P_P1     1
#define P_P2     2
#define P_TITLE  3
#define P_HI     4

static uint8_t big_tile(char c)
{
    const char *p = strchr(BIG_CHARS, c);
    return T_BIG + (uint8_t)(p - BIG_CHARS) * 4;
}

void hud_init(void)
{
    const char *c;
    tm_init();
    tm_scroll(316, 252);
    tm_font(0, ROM_FONT, 1, 1, 0);                      /* tile 0: blank, transparent */
    tm_font(T_FONT, ROM_FONT, 96, 1, 2);
    for (c = BIG_CHARS; *c; c++) tm_glyph2x(big_tile(*c), ROM_FONT + (*c - 32) * 8, 1, 2);
    tm_pal(P_HUD, 1, RGB_HUD_FG);
    tm_pal(P_HUD, 2, RGB_HUD_BG);
    tm_pal(P_P1, 1, RGB_GOAL_P1);
    tm_pal(P_P1, 2, RGB_HUD_BG);
    tm_pal(P_P2, 1, RGB_GOAL_P2);
    tm_pal(P_P2, 2, RGB_HUD_BG);
    tm_pal(P_TITLE, 1, RGB_GOAL_P2);
    tm_pal(P_TITLE, 2, RGB_BLACK);
    tm_pal(P_HI, 1, RGB_WHITE);
    tm_pal(P_HI, 2, RGB_HUD_BG);
}

static char buf[42];

static void centre(uint8_t row, const char *s, uint8_t pal)
{
    tm_print((uint8_t)((40 - strlen(s)) / 2), row, s, T_FONT, pal);
}

static void clear_rows(uint8_t row, uint8_t n) { tm_fill(0, row, 39, n, 0, 0); }

static void big(uint8_t col, uint8_t row, const char *s)
{
    for (; *s; s++, col += 2) {
        uint8_t t = big_tile(*s);
        tm_put(col, row, t, P_TITLE);
        tm_put(col + 1, row, t + 1, P_TITLE);
        tm_put(col, row + 1, t + 2, P_TITLE);
        tm_put(col + 1, row + 1, t + 3, P_TITLE);
    }
}

/* what's on screen, to redraw only changes */
static uint8_t shown_state = 0xFF, shown_sel, shown_carrier, shown_players;
static int16_t shown_score[2];
static uint16_t shown_secs;

static void scores(void)
{
    uint8_t p;
    for (p = 0; p < 2; p++) {
        uint8_t row = p ? 0 : 16, me = G.players == 1 && p == 1;
        tm_fill(0, row, 17, 1, 0, 0);
        sprintf(buf, "P%u%s%d %s", p + 1, G.players == 2 ? ": " : me ? " CPU: " : " YOU: ", G.score[p],
                G.ball.carrier == (int8_t)p ? "*BALL*" : "");
        tm_print(0, row, buf, T_FONT, p ? P_P2 : P_P1);
    }
}

static void clock(uint16_t secs)
{
    sprintf(buf, "%02u:%02u", secs / 60, secs % 60);
    tm_print(17, 0, buf, T_FONT, P_HUD);
}

static void title(uint8_t sel)
{
    centre(18, sel == 1 ? "> 1  ONE PLAYER V CPU " : "  1  ONE PLAYER V CPU ", sel == 1 ? P_HI : P_HUD);
    centre(19, sel == 2 ? "> 2  TWO PLAYERS      " : "  2  TWO PLAYERS      ", sel == 2 ? P_HI : P_HUD);
}

void hud_update(uint8_t sel)
{
    uint16_t secs = (uint16_t)((G.frames_left + CLOCK_HZ - 1) / CLOCK_HZ);
    if (G.state != shown_state) {
        clear_rows(1, 15);
        clear_rows(17, 14);
        shown_score[0] = -1;
        shown_secs = 0xFFFF;
        if (G.state == MS_TITLE) {
            big(12, 1, "ROTOBALL");
            centre(3, " A FUTURISTIC BALL SPORT ", P_HUD);
            title(sel);
            centre(23, "P1  W/S DRIVE   A/D STRAFE  ", P_P1);
            centre(24, "    KEMPSTON  SINCLAIR 1-5  ", P_P1);
            centre(26, "P2  I/K DRIVE   J/L STRAFE  ", P_P2);
            centre(27, "    KEMPSTON 2  SINCLAIR 6-0", P_P2);
            centre(29, "FIRE OR SPACE TO START", P_HI);
            shown_sel = sel;
        } else if (G.state == MS_FULLTIME) {
            const char *w;
            if (G.score[0] == G.score[1]) w = "DRAW";
            else if (G.players == 2) w = G.score[0] > G.score[1] ? "P1 WINS!" : "P2 WINS!";
            else w = G.score[0] > G.score[1] ? "YOU WIN!" : "CPU WINS";
            centre(14, w, P_HI);
            centre(19, "FULL TIME", P_HUD);
            sprintf(buf, "P1 %d  -  %d %s", G.score[0], G.score[1], G.players == 2 ? "P2" : "CPU");
            centre(21, buf, P_HUD);
            centre(24, "PRESS SPACE TO CONTINUE", P_HI);
        }
        shown_state = G.state;
    }
    if (G.state == MS_TITLE && sel != shown_sel) {
        title(sel);
        shown_sel = sel;
    }
    if (G.score[0] != shown_score[0] || G.score[1] != shown_score[1] || G.ball.carrier != (int8_t)shown_carrier ||
        G.players != shown_players) {
        scores();
        shown_score[0] = G.score[0];
        shown_score[1] = G.score[1];
        shown_carrier = (uint8_t)G.ball.carrier;
        shown_players = G.players;
    }
    if (secs != shown_secs) {
        clock(secs);
        shown_secs = secs;
    }
}
