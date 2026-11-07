/*
 * screens.c - Orbit Docking's text screens (the original's render.c
 * hud(), render_game(), render_title(), render_help()) through hud.c: the
 * play screen's lines (redrawn when their values change), the title, game
 * over, the initials, the help pages. In bank 24 with hud.c.
 */
#include <string.h>
#include <stdio.h>
#include "zxgame.h"
#include "nexttile.h"
#include "game.h"
#include "hud.h"
#include "screens.h"

static char tmp[40];

/* what the HUD shows now (redrawn when it changes) */
static int16_t shown_range = -32768, shown_close = -32768, shown_fuel = -1;
static int32_t shown_score = -1;
static uint8_t shown_msg = 0xFF, shown_blink = 2, shown_auto = 2, shown_spin = 0xFF, shown_level = 0;

static const char *const help_lines[2][12] = {
    { "DOCK WITH THE SLOT IN THE STATION'S HUB.",
      "IT SPINS: ROLL TO MATCH IT. WHEN YOU",
      "TURN WITH IT, THE STATION STANDS STILL.",
      "",
      "COME IN ALONG THE AXIS (THE BOX AT THE",
      "TOP RIGHT), SLOWLY: UNDER 1.6 AT CONTACT",
      "THE DIAL: THE SLOT'S ROLL. GREEN IS GOOD",
      "",
      "NO FRICTION IN SPACE: A PUSH LASTS UNTIL",
      "YOU PUSH BACK. FUEL IS LIMITED.",
      "FROM STATION 4, DEBRIS DRIFTS ACROSS.",
      "THREE CRASHES AND IT'S OVER." },
    { "QAOP/CURSOR/JOYSTICK MOVE ACROSS",
      "W / FIRE + UP        FORWARD (IN)",
      "S / FIRE + DOWN      BACK (OUT)",
      "K L / FIRE + LEFT RIGHT  ROLL",
      "ENTER                PAUSE",
      "B                    AUTOPILOT",
      "H                    THESE PAGES",
      "BREAK                TO THE TITLE",
      "",
      "DOCKING: 1000 + 500 X STATION, 2 FOR",
      "EACH UNIT OF FUEL LEFT, LESS TIME.",
      "" },
};

static void draw_help(void)
{
    uint8_t k;
    hud_clear();
    hud_panel(0, 0, 40, 32);
    hud_on_panel = 1;
    hud_big_centre(BIG_BANNER, 12, dk.help == 1 ? "HOW TO PLAY" : "CONTROLS", TX_YELLOW, 1);
    for (k = 0; k < 12; k++) hud_text(1, 7 + k * 2, help_lines[dk.help - 1][k], TX_WHITE);
    hud_text(1, 30, dk.help == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", TX_GREY);
    hud_text(18, 30, "H: NEXT  FIRE: PLAY", TX_YELLOW);
    hud_on_panel = 0;
}

static void draw_hiscores(uint8_t row)
{
    uint8_t k;
    hud_panel(8, row, 24, 9);
    hud_on_panel = 1;
    hud_ctext(row + 1, "BEST PILOTS", TX_YELLOW);
    for (k = 0; k < HS_ENTRIES; k++) {
        sprintf(tmp, "%u  %s  %6lu", k + 1, hs_table[k].name, hs_table[k].score);
        hud_ctext(row + 3 + k, tmp, k == 0 ? TX_GREEN : TX_WHITE);
    }
    hud_on_panel = 0;
}

static void draw_title(uint8_t blink)
{
    hud_big(BIG_TITLE1, 80, 16, "ORBIT", TX_YELLOW, 1);
    hud_big(BIG_TITLE2, 48, 52, "DOCKING", TX_CYAN, 1);
    if (dk.title_page) {
        draw_hiscores(22);
        return;
    }
    hud_panel(5, 24, 30, 8);
    hud_on_panel = 1;
    sprintf(tmp, "START AT STATION %u (UP/DOWN)", dk.start_level);
    hud_ctext(25, tmp, TX_GREEN);
    hud_ctext(27, "PRESS FIRE TO FLY", blink ? TX_YELLOW : TX_WHITE);
    sprintf(tmp, "BEST %lu   H: HOW TO PLAY", hs_table[0].score);
    hud_ctext(29, tmp, TX_GREY);
    hud_on_panel = 0;
}

static void draw_entry(void)
{
    uint8_t k;
    char c[2] = { 0, 0 };
    hud_clear();
    hud_panel(6, 9, 28, 12);
    hud_on_panel = 1;
    hud_ctext(10, "A HIGH SCORE!", TX_GREEN);
    sprintf(tmp, "%ld  PLACE %u", game.score, ed_place + 1);
    hud_ctext(12, tmp, TX_WHITE);
    hud_ctext(14, "YOUR INITIALS:", TX_YELLOW);
    for (k = 0; k < 3; k++) {
        c[0] = ed.name[k];
        hud_text(18 + k * 2, 16, c, k == ed.pos ? TX_YELLOW : TX_WHITE);
        hud_text(18 + k * 2, 17, k == ed.pos ? "-" : " ", TX_YELLOW);
    }
    hud_ctext(19, "UP/DOWN OR TYPE, FIRE", TX_GREY);
    hud_on_panel = 0;
}

/* a fix as "12.3" (render.c fixed_text) */
static void fixed_text(char *out, fix v)
{
    int32_t t = (v * 10) >> 16;
    if (t < 0) t = -t;
    sprintf(out, "%s%ld.%ld", v < 0 ? "-" : "", t / 10, t % 10);
}

/* the play screen (render.c hud() and render_game()): lines redrawn when their values change */
static void draw_play(uint8_t blink)
{
    int16_t range = (int16_t)(game.z >> 16) - (OD_PORT_Z + OD_NOSE), close = (int16_t)((-game.vz * 10) >> 16);
    int16_t spin = (int16_t)((game.roll_rate - game.st_spin) >> 8), err = scr_err;
    uint8_t spin_k = spin < -20 ? 0 : spin > 20 ? 2 : 1, msg, fuel_k;
    msg = game.between ? 1 : (dk.state == DS_PLAY && game.level_ticks < 60) ? 2 : dk.paused == 1 ? 3 : 0;
    if (hud_dirty) {
        hud_clear();
        hud_clip(255);
        shown_range = shown_close = -32768;
        shown_fuel = -1;
        shown_score = -1;
        shown_msg = shown_auto = shown_spin = 0xFF;
        shown_level = 0;
        hud_text(30, 7, "AXIS", TX_GREY);
        hud_text(0, 31, "FUEL", TX_GREY);
        if (dk.flash_timer > 0) hud_ctext(12, dk.flash, dk.flash_ink);
    }
    if (game.level != shown_level) {
        shown_level = (uint8_t)game.level;
        sprintf(tmp, "STATION %d", game.level);
        hud_text(0, 0, tmp, TX_YELLOW);
    }
    hud_instruments(game.x, game.y, (int8_t)game.lives, err, err < OD_TOL_ROLL && err > -OD_TOL_ROLL);
    if (range != shown_range) {
        shown_range = range;
        sprintf(tmp, "RANGE %d   ", range);
        hud_text(0, 3, tmp, TX_WHITE);
    }
    {
        uint8_t ink = range < 150 && -game.vz > OD_TOL_CLOSE ? TX_RED : range < 150 ? TX_GREEN : TX_WHITE;
        if (close * 4 + ink != shown_close) {
            char num[12];
            shown_close = close * 4 + ink;
            fixed_text(num, -game.vz);
            sprintf(tmp, "CLOSE %s   ", num);
            hud_text(0, 4, tmp, ink);
        }
    }
    if (game.score != shown_score) {
        shown_score = game.score;
        sprintf(tmp, "%ld", game.score);
        hud_text(0, 6, tmp, TX_GREY);
    }
    if (spin_k != shown_spin) {
        shown_spin = spin_k;
        hud_text(24, 28, spin_k == 0 ? "<< TURN" : spin_k == 2 ? "TURN >>" : "MATCHED", spin_k == 1 ? TX_GREEN : TX_RED);
    }
    fuel_k = (uint8_t)(game.fuel_max > 0 ? (game.fuel >> 8) * 78 / (game.fuel_max >> 8) : 0);
    if ((int16_t)fuel_k != shown_fuel) {
        shown_fuel = fuel_k;
        hud_fuel(fuel_k, fuel_k > 20 ? TX_CYAN : TX_RED);
    }
    if (dk.autoplay != shown_auto) {
        shown_auto = dk.autoplay;
        hud_text(35, 31, dk.autoplay ? "AUTO" : "    ", TX_GREEN);
    }
    if (msg != shown_msg) {
        shown_msg = msg;
        tm_fill(0, 9, 40, 1, 0, 0);
        tm_fill(0, 14, 40, 1, 0, 0);
        tm_fill(0, 19, 40, 1, 0, 0);
        hud_big(BIG_BANNER, 0, 0, "", 0, 0);
        if (msg == 1) {
            hud_big_centre(BIG_BANNER, 80, "DOCKED", TX_GREEN, 1);
            if (game.level < OD_LEVELS) {
                sprintf(tmp, "STATION %d NEXT", game.level + 1);
                hud_ctext(14, tmp, TX_WHITE);
            }
        } else if (msg == 2) {
            sprintf(tmp, "STATION %d: DOCK WITH THE SLOT", game.level);
            hud_ctext(9, tmp, TX_YELLOW);
        } else if (msg == 3) hud_ctext(19, "PAUSED", TX_WHITE);
    }
    if (dk.state == DS_OVER && (hud_dirty || blink != shown_blink)) {
        shown_blink = blink;
        hud_panel(6, 10, 28, 10);
        hud_on_panel = 1;
        hud_big_centre(BIG_BANNER, 88, game.won ? "ALL DOCKED" : "GAME OVER", game.won ? TX_GREEN : TX_RED, 0);
        sprintf(tmp, "%d DOCKED  %d CRASHED", game.docks, game.crashes);
        hud_ctext(14, tmp, TX_YELLOW);
        sprintf(tmp, "SCORE %ld", game.score);
        hud_ctext(16, tmp, TX_WHITE);
        if (dk.state_timer == 0) hud_ctext(18, "PRESS FIRE", TX_YELLOW);
        hud_on_panel = 0;
    }
}

void screens_draw(void)
{
    uint8_t blink = (uint8_t)((game.tick / 12) & 1);
    if (dk.help) {
        if (hud_dirty) draw_help();
    } else if (dk.state == DS_TITLE) {
        if (hud_dirty) {
            hud_clear();
            shown_blink = 2;
        }
        if (hud_dirty || blink != shown_blink) {
            shown_blink = blink;
            draw_title(blink);
        }
    } else if (dk.state == DS_ENTRY) {
        if (hud_dirty) draw_entry();
    } else draw_play(blink);
    hud_dirty = 0;
}

