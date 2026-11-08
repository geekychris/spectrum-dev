/*
 * screens.c - Lander 3D's text (the original's render.c hud(),
 * render_game(), render_title(), render_help()) through hud.c: the HUD's
 * lines (redrawn when their values change), the result box, the title over
 * the demo, game over, the initials, the help pages. In bank 24 with hud.c.
 *
 * port: the HUD's lines are on 8-pixel tile rows (the original's are 12
 * apart); the big text is the original's 16 x 16 font as sprites (at twice
 * its size where the original draws it at 2 or 3); the high-score table
 * takes turns with the start panel on the title.
 */
#include <string.h>
#include <stdio.h>
#include "zxgame.h"
#include "nexttile.h"
#include "game.h"
#include "hud.h"
#include "screens.h"

static char tmp[44];
static const char *const world_names[4] = { "MOON", "MARS", "ASTEROID", "TOUR" };

/* what the HUD shows now (redrawn when it changes) */
static int16_t sh_alt, sh_vs, sh_hs, sh_tilt, sh_dist;
static int8_t sh_thr, sh_fuel, sh_ships;
static int32_t sh_score;
static uint8_t sh_vsk, sh_hsk, sh_tk, sh_blink, sh_msg;

static uint8_t limit_ink(fix v, fix lim)       /* render.c limit_pen() */
{
    if (v <= fmul(lim, FX(0.7))) return TX_GREEN;
    if (v <= lim) return TX_AMBER;
    return TX_RED;
}

static void tenths(fix v)                      /* "n.n" into tmp + its length */
{
    int32_t t = (v < 0 ? -v : v) * 10 >> 16;
    sprintf(tmp + strlen(tmp), "%s%ld.%ld ", v < 0 && t ? "-" : "", t / 10, t % 10);
}

static const char *const help_lines[2][12] = {
    { "THE MAIN ENGINE PUSHES ALONG THE",
      "LANDER'S OWN UP: TIP IT TO MOVE",
      "SIDEWAYS, LEVEL IT TO STOP, AND KEEP",
      "ENOUGH THRUST ON TO STOP GRAVITY.",
      "TOUCH DOWN WITH EVERY LEG ON A PAD:",
      "SLOW (GREEN V/S), NOT SLIDING (H/S)",
      "AND NEARLY LEVEL (TILT).",
      "",
      "GREEN PAD x1   YELLOW x2   MAGENTA x5",
      "SCORE (100 + SOFTNESS + FUEL) x PAD",
      "SAFE OFF A PAD: 25. ELSE: A SHIP LOST.",
      "MARS HAS WIND. THE ASTEROID: DELICATE." },
    { "SPACE / J / FIRE  FULL THRUST",
      "Q / E             STEADY THROTTLE - / +",
      "W / UP            TIP FORWARD",
      "S / DOWN          TIP BACK",
      "A / LEFT          TIP LEFT",
      "D / RIGHT         TIP RIGHT",
      "P                 PAUSE",
      "H                 THESE PAGES",
      "B                 THE AUTOPILOT",
      "BREAK             TO THE TITLE",
      "THE ARROW AT THE TOP POINTS TO THE",
      "NEAREST PAD. THE SHADOW SHOWS HEIGHT." },
};

static void draw_help(void)
{
    uint8_t k;
    hud_clear();
    hud_panel(0, 0, 40, 32);
    hud_on_panel = 1;
    hud_big_centre(BIG_MSG, 12, lander.help == 1 ? "HOW TO FLY" : "CONTROLS", TX_AMBER, 1);
    for (k = 0; k < 12; k++) hud_text(1, 7 + k * 2, help_lines[lander.help - 1][k], TX_WHITE);
    hud_text(1, 30, lander.help == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", TX_GREY);
    hud_text(18, 30, "H: NEXT  FIRE: FLY", TX_GREEN);
    hud_on_panel = 0;
}

static void draw_hiscores(uint8_t row)
{
    uint8_t k;
    hud_panel(8, row, 24, 9);
    hud_on_panel = 1;
    hud_ctext(row + 1, "BEST PILOTS", TX_AMBER);
    for (k = 0; k < HS_ENTRIES; k++) {
        sprintf(tmp, "%u  %s  %6lu", k + 1, hs_table[k].name, hs_table[k].score);
        hud_ctext(row + 3 + k, tmp, k == 0 ? TX_GREEN : TX_WHITE);
    }
    hud_on_panel = 0;
}

/* render.c render_title(): the big name over the demo, the world, fire,
   help; the bottom panel */
static void draw_title(uint8_t blink)
{
    hud_big_centre(BIG_TITLE, 24, "LANDER 3D", TX_WHITE, 1);
    hud_strip(27, 5);
    hud_on_panel = 1;
    sprintf(tmp, "BEST %lu", hs_table[0].score);
    hud_text(1, 28, tmp, TX_AMBER);
    hud_text(1, 30, "THE AUTOPILOT IS FLYING THE DEMO", TX_GREY);
    hud_on_panel = 0;
    if (lander.title_page) {
        draw_hiscores(15);
        return;
    }
    sprintf(tmp, "<  %s  >", world_names[lander.menu]);
    hud_ctext(18, tmp, TX_AMBER);
    hud_ctext(21, "PRESS FIRE TO START", blink ? TX_GREEN : TX_WHITE);
    hud_ctext(23, "H: HOW TO FLY", TX_GREY);
}

static void draw_entry(void)
{
    uint8_t k;
    char c[2] = { 0, 0 };
    hud_clear();
    hud_panel(6, 9, 28, 12);
    hud_on_panel = 1;
    hud_ctext(10, "A HIGH SCORE!", TX_GREEN);
    sprintf(tmp, "%ld  PLACE %u", lander.score, ed_place + 1);
    hud_ctext(12, tmp, TX_WHITE);
    hud_ctext(14, "YOUR INITIALS:", TX_AMBER);
    for (k = 0; k < 3; k++) {
        c[0] = ed.name[k];
        hud_text(18 + k * 2, 16, c, k == ed.pos ? TX_AMBER : TX_WHITE);
        hud_text(18 + k * 2, 17, k == ed.pos ? "-" : " ", TX_AMBER);
    }
    hud_ctext(19, "UP/DOWN OR TYPE, FIRE", TX_GREY);
    hud_on_panel = 0;
}

/* render.c hud(): three lines on the panel at the bottom, the distance to
   the nearest pad at the top (its arrow is drawn by the view), the wind */
static void draw_play(void)
{
    int16_t alt = (int16_t)(hv.agl < 0 ? 0 : FINT(hv.agl)), vs = (int16_t)((hv.vs * 10) >> 16), hs = (int16_t)((hv.hs * 10) >> 16);
    int16_t tilt = (int16_t)FINT(fmul(hv.tilt, FX(57.29578))), dist = (int16_t)hv.pad_dist;
    uint8_t vsk = hv.vs > 0 ? limit_ink(hv.vs, hv.lim_vs) : TX_GREEN, hsk = limit_ink(hv.hs, hv.lim_hs);
    uint8_t tk = limit_ink(hv.tilt, hv.lim_tilt), msg;
    msg = lander.paused == 1 ? 1 : (lander.state == LS_RESULT || lander.state == LS_OVER) ? 2 : 0;
    if (hud_dirty) {
        hud_clear();
        hud_strip(27, 5);
        sh_alt = sh_vs = sh_hs = sh_tilt = sh_dist = -32768;
        sh_thr = sh_fuel = sh_ships = -1;
        sh_score = -1;
        sh_vsk = sh_hsk = sh_tk = sh_msg = 0xFF;
        hud_on_panel = 1;
        hud_text(12, 31, "FUEL", TX_GREY);
        sprintf(tmp, "%s %d", world_names[level.world], lander.level);
        hud_text(25, 29, tmp, TX_CYAN);
        hud_on_panel = 0;
        if (hv.wind) hud_text(33, 4, "WIND", TX_GREY);
        if (lander.topped) hud_ctext(3, "TANK TOPPED UP", TX_AMBER);
    }
    hud_on_panel = 1;
    if (alt != sh_alt) { sh_alt = alt; sprintf(tmp, "ALT %4d ", alt); hud_text(1, 27, tmp, TX_WHITE); }
    if (vs != sh_vs || vsk != sh_vsk) { sh_vs = vs; sh_vsk = vsk; strcpy(tmp, "V/S "); tenths(hv.vs); hud_text(1, 29, tmp, vsk); }
    if (hs != sh_hs || hsk != sh_hsk) { sh_hs = hs; sh_hsk = hsk; strcpy(tmp, "H/S "); tenths(hv.hs); hud_text(1, 31, tmp, hsk); }
    if (tilt != sh_tilt || tk != sh_tk) { sh_tilt = tilt; sh_tk = tk; sprintf(tmp, "TILT %2d ", tilt); hud_text(12, 27, tmp, tk); }
    if ((int8_t)hv.thr != sh_thr) { sh_thr = (int8_t)hv.thr; sprintf(tmp, "THR %3u%% ", hv.thr); hud_text(12, 29, tmp, TX_GREY); }
    if ((int8_t)hv.fuel != sh_fuel) {
        sh_fuel = (int8_t)hv.fuel;
        hud_fuel(17, 31, (uint8_t)(hv.fuel / 2 > 48 ? 48 : hv.fuel / 2), hv.fuel < 20 ? TX_RED : TX_AMBER);
    }
    if (lander.score != sh_score) { sh_score = lander.score; sprintf(tmp, "SCORE %ld", lander.score); hud_text(25, 27, tmp, TX_WHITE); }
    if (lander.ships != sh_ships) { sh_ships = lander.ships; sprintf(tmp, "SHIPS %d", (int)lander.ships); hud_text(25, 31, tmp, TX_GREY); }
    hud_on_panel = 0;
    if (dist != sh_dist) {
        sh_dist = dist;
        sprintf(tmp, "%dM   ", dist);
        hud_text(22, 2, tmp, TX_RING1 + hv.pad);
    }
    if (msg != sh_msg) {
        sh_msg = msg;
        if (msg == 1) hud_ctext(12, "PAUSED", TX_WHITE);
        else if (msg == 2) {
            uint8_t ink = level.outcome == OUT_LANDED ? TX_GREEN : level.outcome == OUT_OFFPAD ? TX_AMBER : TX_RED;
            hud_panel(3, 7, 34, 11);
            hud_big_centre(BIG_MSG, 70, lander.msg, ink, 1);
            hud_on_panel = 1;
            hud_ctext(13, lander.msg2, TX_WHITE);
            if (lander.state == LS_OVER) hud_ctext(15, "GAME OVER", TX_RED);
            else if (level.outcome == OUT_LANDED || level.outcome == OUT_OFFPAD) {
                sprintf(tmp, "+%ld", lander.last_score);
                hud_ctext(15, tmp, TX_AMBER);
            }
            hud_on_panel = 0;
        }
    }
}

void screens_draw(void)
{
    uint8_t blink = (lander.frame & 16) != 0;
    if (lander.help) {
        if (hud_dirty) draw_help();
        hud_dirty = 0;
        return;
    }
    if (lander.state == LS_ENTRY) {
        if (hud_dirty) draw_entry();
        hud_dirty = 0;
        return;
    }
    if (lander.state == LS_TITLE) {
        if (hud_dirty || blink != sh_blink) {
            if (hud_dirty) hud_clear();
            sh_blink = blink;
            draw_title(blink);
        }
        hud_dirty = 0;
        return;
    }
    draw_play();
    hud_dirty = 0;
}
