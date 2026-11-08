/*
 * screens.c - Boing Twist's text (the original's draw_hud): the title, the
 * select screen, the bars of the play screen, the solved screen, the help,
 * the high scores and the initials. In the logic bank (bt.h).
 */
#include <string.h>
#include <stdio.h>
#include "zxgame.h"
#include "bt_core.h"
#include "gfx_data.h"
#include "room.h"
#include "hud.h"
#include "game.h"

char tmp[44];

/* ---------------- the text ---------------- */
static void fmt_time(char *b, uint16_t s)
{
    sprintf(b, "%u:%02u", s / 60, s % 60);
}

static void draw_scores(uint8_t row)
{
    uint8_t i;
    hud_on_panel = 1;
    hud_panel(10, row, 20, 9);
    hud_ctext(row + 1, "HIGH SCORES", TX_YELLOW);
    for (i = 0; i < HS_ENTRIES; i++) {
        sprintf(tmp, "%u. %s  %5lu", i + 1, hs_table[i].name, (unsigned long)hs_table[i].score);
        hud_ctext(row + 3 + i, tmp, i == 0 ? TX_WHITE : TX_GREY);
    }
    hud_on_panel = 0;
}

static void draw_help(void)
{
    static const char *const lines[] = {
        "THE PICTURE ON THE BALL IS CUT INTO",
        "TILES AND JUMBLED. PUT IT BACK.",
        "",
        "TWIST: A BAND OF TILES TURNS ROUND",
        " UP/DOWN PICKS THE BAND (YELLOW)",
        " LEFT/RIGHT TWISTS IT ONE TILE",
        "FLIP: HALF THE BALL TURNS OVER",
        " Z/X (OR FIRE+LEFT/RIGHT) PICK",
        " THE HALF (CYAN), SPACE/FIRE FLIPS",
        " IT: ITS TILES END UP UPSIDE DOWN",
        "",
        "K/L W/S TURN THE BALL  C FACE",
        "U UNDO  H PEEK (HOLD)  ENTER PAUSE",
        "R GIVE UP  N NEW  T ROOM",
        "QAOP, CURSOR KEYS OR JOYSTICK",
        "",
        "PRESS ANY KEY",
    };
    uint8_t i, n = sizeof(lines) / sizeof(lines[0]);
    hud_on_panel = 1;
    hud_panel(1, 4, 38, n + 4);
    hud_ctext(5, "HOW TO PLAY", TX_YELLOW);
    for (i = 0; i < n; i++) hud_text(3, 7 + i, lines[i], i == n - 1 ? TX_YELLOW : TX_WHITE);
    hud_on_panel = 0;
}

static void draw_select(void)
{
    static const char *const tips[BT_NLEVELS] = {
        "BIG TILES, BOLD PICTURES", "MORE TILES, LONGER SCRAMBLE", "THE BOING BALL'S OWN 128", "128 TILES, TWICE AS JUMBLED" };
    const BtLevel *lv = &bt_levels[sel_level];
    uint8_t list[NPICS], n = 0, at = 0, first, i, rows = 7, y;
    hud_ctext(2, "CHOOSE A PUZZLE", TX_YELLOW);
    hud_on_panel = 1;
    hud_panel(1, 4, 22, 23);
    hud_text(2, 5, sel_row == 0 ? ">" : " ", TX_YELLOW);
    sprintf(tmp, "LEVEL < %s >", lv->name);
    hud_text(3, 5, tmp, sel_row == 0 ? TX_YELLOW : TX_GREY);
    sprintf(tmp, "%d X %d, SCRAMBLED %d", lv->bands, lv->segs, lv->moves);
    hud_text(3, 6, tmp, TX_DIM);
    hud_text(3, 7, tips[sel_level], TX_DIM);
    for (i = 0; i < NPICS; i++)
        if (pic_allowed(i, sel_level)) {
            if (i == sel_pic) at = n;
            list[n++] = i;
        }
    sprintf(tmp, "PICTURE %u OF %u", at + 1, n);
    hud_text(2, 9, sel_row == 1 ? ">" : " ", TX_YELLOW);
    hud_text(3, 9, tmp, sel_row == 1 ? TX_YELLOW : TX_GREY);
    first = at > rows / 2 ? at - rows / 2 : 0;
    if (first + rows > n) first = n > rows ? n - rows : 0;
    for (i = 0; i < rows; i++) {
        y = 10 + i;
        if (first + i >= n) continue;
        hud_text(2, y, list[first + i] == sel_pic ? ">" : " ", sel_row == 1 ? TX_YELLOW : TX_GREY);
        sprintf(tmp, "%-18.18s", pics[list[first + i]].title);
        hud_text(3, y, tmp, list[first + i] == sel_pic ? TX_WHITE : TX_DIM);
    }
    if (first > 0) hud_text(21, 10, "^", TX_DIM);
    if (first + rows < n) hud_text(21, 16, "V", TX_DIM);
    if (sel_pic) {
        sprintf(tmp, "%s AND UP%s", bt_levels[pics[sel_pic].level].name, pics[sel_pic].room ? " +ROOM" : "");
        hud_text(3, 18, tmp, TX_GREY);
    } else
        hud_text(3, 18, "WARM-UP: ALIKE SQUARES", TX_YELLOW);
    hud_text(2, 20, sel_row == 2 ? ">" : " ", TX_YELLOW);
    hud_text(3, 20, "ROOM", sel_row == 2 ? TX_YELLOW : TX_GREY);
    sprintf(tmp, "%-15.15s", rooms[room_cur].title);
    hud_text(3, 21, tmp, sel_row == 2 ? TX_WHITE : TX_DIM);
    if (best_mv[sel_pic][sel_level]) {
        char t[8];
        fmt_time(t, best_s[sel_pic][sel_level]);
        sprintf(tmp, "BEST %u MOVES %s", best_mv[sel_pic][sel_level], t);
    } else
        strcpy(tmp, "NOT SOLVED YET");
    hud_text(3, 23, tmp, TX_CYAN);
    hud_on_panel = 0;
    hud_ctext(29, "ARROWS CHOOSE  FIRE PLAY  BREAK BACK", TX_DIM);
}

void screens_draw(void)
{
    char t[8];
    hud_clear();
    if (help) { draw_help(); return; }
    switch (screen) {
    case SC_TITLE:
        hud_big_title(4, TX_RED);
        hud_ctext(8, "PUT THE PICTURE BACK TOGETHER", TX_WHITE);
        if (title_page) draw_scores(11);
        hud_ctext(27, "H HELP    T ROOM", TX_DIM);
        sprintf(tmp, "ROOM: %s", rooms[room_cur].title);
        hud_ctext(29, tmp, TX_DIM);
        break;
    case SC_SELECT:
        draw_select();
        break;
    case SC_PLAY:
    case SC_SOLVED:
        sprintf(tmp, "%.20s %s", pics[play_pic].title, bt_levels[play_level].name);
        hud_text(1, 1, tmp, TX_WHITE);
        fmt_time(t, secs);
        sprintf(tmp, "MOVES %d  TIME %s", P.moves, t);
        hud_rtext(39, 2, tmp, TX_YELLOW);
        if (screen == SC_PLAY) {
            if (busy) hud_ctext(26, "SCRAMBLING...", TX_YELLOW);
            else if (peeking) hud_ctext(4, "PEEK: THE PICTURE AS IT SHOULD BE", TX_CYAN);
            hud_ctext(29, "ARROWS TWIST  Z/X SPACE FLIP  U UNDO", TX_DIM);
            hud_ctext(30, "H PEEK  K/L W/S LOOK  R GIVE UP", TX_DIM);
            if (paused) {
                hud_on_panel = 1;
                hud_panel(12, 13, 16, 5);
                hud_ctext(14, "PAUSED", TX_WHITE);
                hud_ctext(16, "ENTER TO GO ON", TX_DIM);
                hud_on_panel = 0;
            }
        } else {
            hud_ctext(5, gave_up ? "SOLVED FOR YOU" : "SOLVED!", gave_up ? TX_DIM : TX_YELLOW);
            if (!gave_up) {
                fmt_time(t, secs);
                sprintf(tmp, "%d MOVES IN %s%s", P.moves, t, best_new ? " - A NEW BEST!" : "");
                hud_ctext(7, tmp, best_new ? TX_CYAN : TX_WHITE);
                sprintf(tmp, "SCORE %u", score);
                hud_ctext(9, tmp, TX_YELLOW);
            }
            hud_ctext(29, "FIRE: CARRY ON    BREAK: TITLE", TX_DIM);
        }
        break;
    case SC_ENTRY:
        hud_on_panel = 1;
        hud_panel(8, 9, 24, 10);
        hud_ctext(10, "A HIGH SCORE!", TX_YELLOW);
        sprintf(tmp, "SCORE %u", score);
        hud_ctext(12, tmp, TX_WHITE);
        hud_ctext(14, "TYPE YOUR INITIALS", TX_GREY);
        sprintf(tmp, "%c %c %c", ed.name[0], ed.name[1], ed.name[2]);
        hud_ctext(16, tmp, TX_CYAN);
        hud_text(17 + ed.pos * 2, 17, "^", TX_YELLOW);
        hud_on_panel = 0;
        break;
    }
}

