/*
 * screens.c - RJ's Birthday Bash's screens other than play (the original's
 * draw_title, draw_help, draw_credits, draw_gameover, draw_win,
 * draw_hiscore, draw_enter_name, draw_guest_edit, draw_jail) and the play
 * screen's HUD and text (draw_hud, the bubbles and name tags of
 * draw_one_guest and draw_player).
 *
 * Text is the tilemap: the original's strings at its places, on the
 * 8-pixel grid (port: a baseline y becomes row (y - 6) / 8, x column x / 8).
 * The pictures (the title's RJ, the jail cell, the win screen's cake) are
 * Layer 2: the title from the host (tools/gen_gfx.py), the others filled
 * here as the original fills them.
 */
#include <string.h>
#include <stdio.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "game.h"
#include "gfx_data.h"
#include "view.h"
#include "screens.h"

static char buf[44];

#define COL(x) ((uint8_t)((x) >> 3))
#define ROW(y) ((uint8_t)(((y) - 6) >> 3))       /* a topaz baseline -> the row of the text */
#define PEN(c) (1 + (c) * 5 + 2)                 /* Layer 2: the original's colour, its middle shade */

static void text(int16_t x, int16_t y, const char *s, uint8_t pen) { view_print(COL(x), ROW(y), s, pen, 0); }

/* Layer 2 rectangles, the original's RectFill (inclusive) */
static void fill(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t c)
{
    if (x1 < 0) x1 = 0;
    if (x2 > 319) x2 = 319;
    if (y1 < 0) y1 = 0;
    if (y2 > 255) y2 = 255;
    if (x1 > x2 || y1 > y2) return;
    l2w_fill((uint16_t)x1, (uint8_t)y1, (uint16_t)(x2 - x1 + 1), (uint16_t)(y2 - y1 + 1), PEN(c));
}

/* ---- the play screen's HUD (draw_hud) ---- */
static int32_t hud_score = -1;
static int16_t hud_lives = -1, hud_mins = -1, hud_room = -1;

void hud_reset(void) { hud_score = -1; hud_lives = hud_mins = hud_room = -1; }

void hud_draw(const GameState *gs)
{
    int16_t i, hour, sub, mins;
    if (gs->score != hud_score) {
        hud_score = gs->score;
        sprintf(buf, "SCORE: %ld   ", (long)gs->score);
        view_print(0, 0, buf, TX_YELLOW, 0);
    }
    if (gs->lives != hud_lives) {
        hud_lives = gs->lives;
        for (i = 0; i < 5; i++)                       /* party hats (port: on the 8-pixel grid) */
            tm_put((uint8_t)(17 + i * 2), 0, i < gs->lives ? T_HAT : 0, TX_HAT);
    }
    hour = 7 + (int16_t)(((int32_t)gs->party_clock * 5) / PARTY_FRAMES);
    if (hour > 12) hour = 12;
    sub = (int16_t)(((int32_t)gs->party_clock * 5) % PARTY_FRAMES);
    mins = (int16_t)(((int32_t)sub * 60) / PARTY_FRAMES);
    if (hour * 60 + mins != hud_mins) {
        hud_mins = hour * 60 + mins;
        sprintf(buf, "%2d:%02d", hour, mins);
        view_print(COL(260), 0, buf, TX_WHITE, 0);
    }
    if (gs->current_room != hud_room) {               /* the room's name at the bottom */
        hud_room = gs->current_room;
        tm_fill(0, 30, 40, 1, 0, 0);
        if (gs->current_room >= 0 && gs->current_room < ROOM_COUNT) view_cprint(30, room_names[gs->current_room], TX_YELLOW, 1);
    }
}

/* the text in the house: name tags, bubbles, the message and the easter egg */
void world_text(const GameState *gs)
{
    uint8_t i;
    int16_t cam = gs->camera_x;
    for (i = 0; i < MAX_GUESTS; i++) {
        const Guest *g = &gs->guests[i];
        if (!g->active) {
            view_wtext(WT_NAME0 + i, 0, 0, 0, 0, 0);
            view_wtext(WT_BUBBLE0 + i, 0, 0, 0, 0, 0);
            continue;
        }
        if (g->name_idx >= 0 && g->name_idx < gs->name_count)
            view_wtext(WT_NAME0 + i, g->x - 2, g->y + 7, gs->names[g->name_idx], TX_WHITE, 0);   /* port: the row under the feet */
        else
            view_wtext(WT_NAME0 + i, 0, 0, 0, 0, 0);
        if (g->bubble_timer > 0 && g->bubble_text[0]) {
            int16_t bw = (int16_t)strlen(g->bubble_text) * 8 + 4;
            view_wtext(WT_BUBBLE0 + i, g->x + 5 - bw / 2 + 2, g->y - GUEST_H - 16 + 2, g->bubble_text, TX_BUBBLE, 1);
        } else
            view_wtext(WT_BUBBLE0 + i, 0, 0, 0, 0, 0);
    }
    if (gs->rj_bubble_timer > 0 && gs->rj_bubble[0]) {
        int16_t bw = (int16_t)strlen(gs->rj_bubble) * 8 + 4;
        view_wtext(WT_RJ, gs->player.x + 6 - bw / 2 + 2, gs->player.y - PLAYER_H - 18 + 2, gs->rj_bubble, TX_RJ, 1);
    } else
        view_wtext(WT_RJ, 0, 0, 0, 0, 0);
    if (gs->msg_timer > 0) {                          /* centred on the screen */
        int16_t mw = (int16_t)strlen(gs->msg) * 8 + 8;
        view_wtext(WT_MSG, cam + (SCREEN_W - mw) / 2 + 4, WALK_Y_MIN - 15 - 7, gs->msg,
                   (gs->msg_timer & 4) ? TX_YELLOW : TX_WHITE, 1);
    } else
        view_wtext(WT_MSG, 0, 0, 0, 0, 0);
    if (gs->egg_timer > 0) {
        int16_t ew = (int16_t)strlen(gs->egg_text) * 8 + 8;
        view_wtext(WT_EGG, cam + (SCREEN_W - ew) / 2 + 4, SCREEN_H - 28 - 7, gs->egg_text, TX_YELLOW, 1);
    } else
        view_wtext(WT_EGG, 0, 0, 0, 0, 0);
}

/* ---- the other screens ---- */
static uint8_t blink_shown = 2;
static int16_t scroll_shown = -1, entry_shown = -1;

static void title_text(void)
{
    text(60, 60, "RJ'S 70TH BIRTHDAY", TX_RED);
    text(116, 80, "BASH!", TX_YELLOW);
    text(44, 175, "KEEP THE PARTY GOING!", TX_WHITE);
    text(56, 190, "ARROWS/JOYSTICK+FIRE", TX_WHITE);
    text(36, 222, "H=HELP E=GUESTS", TX_LTGREEN);          /* port: no Q=QUIT (Q is up) */
    text(48, 238, "A TRIBUTE TO RJ MICAL", TX_GREY);
    text(20, 252, "chris@hitorro.com 2026", TX_DKBROWN);
}

/* port: the original's lines 11 pixels apart (16 or 20 after a heading) are
   the tilemap's rows one after another (a blank row for the bigger gaps) */
static void help_text(void)
{
    uint8_t r = 2;
    view_print(9, r, "HOW TO PARTY!", TX_YELLOW, 0); r += 2;
    view_print(1, r++, "YOU ARE RJ MICAL. IT'S YOUR", TX_WHITE, 0);
    view_print(1, r++, "70TH BIRTHDAY! KEEP GUESTS", TX_WHITE, 0);
    view_print(1, r++, "HAPPY ACROSS 6 THEMED ROOMS.", TX_WHITE, 0); r++;
    view_print(1, r++, "CONTROLS:", TX_YEL2, 0);
    view_print(1, r++, "QAOP/CURSOR/JOYSTICK = MOVE", TX_WHITE, 0);   /* port: the Spectrum's keys */
    view_print(1, r++, "SPACE/FIRE = INTERACT", TX_WHITE, 0);
    view_print(1, r++, "BREAK = CREDITS, TITLE", TX_WHITE, 0); r++;
    view_print(1, r++, "ROOMS:", TX_YEL2, 0);
    view_print(1, r, "FOYER", TX_ORANGE, 0); view_print(11, r++, "- CATCH GIFTS!", TX_GREY, 0);
    view_print(1, r, "AMSTERDAM", TX_GREEN, 0); view_print(11, r++, "- SERVE+COLLECT", TX_GREY, 0);
    view_print(1, r, "TOKYO", TX_RED, 0); view_print(11, r++, "- MATCH SUSHI", TX_GREY, 0);
    view_print(1, r, "SILICON V", TX_LTBLUE, 0); view_print(11, r++, "- DEMO+ARCADES", TX_GREY, 0);
    view_print(1, r, "BAY AREA", TX_YEL2, 0); view_print(11, r++, "- DODGE+DELIVER", TX_GREY, 0);
    view_print(1, r, "LIVING RM", TX_PINK, 0); view_print(11, r++, "- BIRTHDAY CAKE", TX_GREY, 0); r++;
    view_print(1, r, "GREET FRIENDS WITH FIRE!", TX_LTGREEN, 0);
    text(44, 248, "PRESS FIRE TO GO BACK", TX_GREY);
}

static void gameover_text(const GameState *gs)
{
    text(76, 80, "PARTY'S OVER, MAN!", TX_RED);
    text(108, 110, "FINAL SCORE:", TX_WHITE);
    sprintf(buf, "%ld", (long)gs->score);
    text(136, 130, buf, TX_YELLOW);
    text(60, 180, "PRESS FIRE TO CONTINUE", TX_GREY);
}

static void win_screen(const GameState *gs)
{
    text(56, 60, "WHAT A PARTY!!!", TX_YELLOW);
    text(108, 90, "FINAL SCORE:", TX_WHITE);
    sprintf(buf, "%ld", (long)gs->score);
    text(136, 108, buf, TX_YEL2);
    fill(120, 130, 200, 170, COL_WHITE);                   /* the cake */
    fill(122, 128, 198, 132, COL_PINK);
    fill(122, 148, 198, 152, COL_PINK);
    text(148, 163, "70", TX_RED);
    fill(140, 118, 143, 128, COL_BTYELLOW);
    fill(158, 118, 161, 128, COL_BTYELLOW);
    fill(176, 118, 179, 128, COL_BTYELLOW);
    fill(140, 114, 143, 118, COL_ORANGE);
    fill(158, 114, 161, 118, COL_ORANGE);
    fill(176, 114, 179, 118, COL_ORANGE);
    text(36, 200, "HAPPY BIRTHDAY RJ MICAL!", TX_YELLOW);
    text(60, 230, "PRESS FIRE FOR CREDITS", TX_GREY);
}

static void hiscore_text(const GameState *gs)
{
    int16_t i;
    text(76, 40, "HALL OF PARTY!", TX_YELLOW);
    text(36, 60, "RANK  NAME         SCORE", TX_ORANGE);
    fill(36, 63, 280, 63, COL_GREY);
    for (i = 0; i < gs->hi_count; i++) {
        int16_t y = 78 + i * 16;                     /* port: 16 pixels apart (the original's 18), two rows */
        sprintf(buf, "%d.", i + 1);
        text(44, y, buf, TX_WHITE);
        text(76, y, gs->hi_names[i], i == 0 ? TX_YELLOW : TX_WHITE);
        sprintf(buf, "%6ld", (long)gs->hi_scores[i]);
        text(216, y, buf, TX_YEL2);
    }
    text(20, 190, "FIRE: ADD YOUR NAME TO", TX_LTGREEN);
    text(44, 205, "THE GUEST LIST!", TX_LTGREEN);
    text(52, 235, "PRESS FIRE TO CONTINUE", TX_GREY);
}

static void entry_static(const GameState *gs)
{
    if (gs->state == GS_ENTER_NAME) {
        text(60, 50, "NEW HIGH SCORE!", TX_YELLOW);
        sprintf(buf, "%ld", (long)gs->score);
        text(136, 70, buf, TX_YEL2);
    } else {
        text(28, 50, "JOIN THE GUEST LIST!", TX_LTGREEN);
        text(20, 75, "TYPE YOUR NAME TO ATTEND", TX_WHITE);
        text(36, 90, "RJ'S BIRTHDAY PARTY!", TX_WHITE);
    }
    text(52, 120, "ENTER YOUR NAME:", TX_WHITE);
    fill(60, 135, 260, 155, COL_DKBROWN);
    fill(62, 137, 258, 153, COL_BROWN);
    text(44, 180, "TYPE + ENTER TO CONFIRM", TX_GREY);   /* port: ENTER, BREAK */
    text(68, 200, "BREAK TO SKIP", TX_GREY);
}

static void entry_name(const GameState *gs)
{
    tm_fill(COL(68), ROW(149), 24, 1, 0, 0);
    view_print(COL(68), ROW(149), gs->entry_name, TX_YELLOW, 0);
    if ((gs->entry_blink >> 3) & 1)                   /* the cursor */
        tm_put((uint8_t)(COL(68) + gs->entry_pos), ROW(149), (uint8_t)(T_FONTB + 63), TX_YELLOW);
}

static void editor(const GameState *gs)
{
    int16_t i, y, max_visible = 10, scroll_top, total = gs->name_count + 1;
    tm_clear();
    scroll_top = gs->edit_cursor - max_visible / 2;
    if (scroll_top < 0) scroll_top = 0;
    if (scroll_top > total - max_visible) scroll_top = total - max_visible;
    if (scroll_top < 0) scroll_top = 0;
    text(60, 30, "GUEST LIST EDITOR", TX_YELLOW);
    sprintf(buf, "%d GUESTS", gs->name_count);
    text(200, 30, buf, TX_GREY);
    if (scroll_top > 0) text(285, 50, "^", TX_ORANGE);
    y = 50;
    for (i = scroll_top; i < gs->name_count && i < scroll_top + max_visible; i++) {
        uint8_t sel = i == gs->edit_cursor;
        sprintf(buf, "%d.", i + 1);
        view_print(COL(42), ROW(y), buf, TX_GREY, sel);
        view_print(COL(66), ROW(y), gs->names[i], sel ? TX_HUDYELLOW : TX_WHITE, sel);
        y += 16;
    }
    if (gs->name_count >= scroll_top && gs->name_count < scroll_top + max_visible)
        text(50, y, "+ ADD NEW GUEST", gs->edit_cursor >= gs->name_count ? TX_LTGREEN : TX_GREEN);
    if (scroll_top + max_visible < total) text(285, 50 + (max_visible - 1) * 16, "v", TX_ORANGE);
    text(28, 220, "UP/DOWN = SELECT", TX_GREY);
    text(28, 232, "DEL = REMOVE  ENTER = ADD", TX_GREY);
    text(28, 244, "BREAK = SAVE AND EXIT", TX_GREY);
}

static void jail_picture(void)
{
    int16_t bx, by, i, rx = 150, ry = 150;
    fill(60, 50, 260, 200, COL_GREY);                      /* the cell */
    fill(65, 55, 255, 195, COL_DKBROWN);
    for (by = 55; by < 195; by += 14) {                    /* bricks */
        int16_t off = ((by / 14) & 1) ? 20 : 0;
        fill(65, by, 255, by, COL_GREY);
        for (bx = 65 + off; bx < 255; bx += 40) fill(bx, by, bx, by + 13, COL_GREY);
    }
    for (i = 0; i < 7; i++) fill(85 + i * 25, 55, 87 + i * 25, 195, COL_WHITE);   /* bars */
    for (i = 0; i < 8; i++) fill(rx + 6 - i / 2, ry - 24 + i, rx + 6 + i / 2, ry - 24 + i, COL_RED);   /* RJ */
    fill(rx + 3, ry - 15, rx + 9, ry - 8, COL_TAN);
    fill(rx + 3, ry - 13, rx + 5, ry - 11, COL_BLUE);
    fill(rx + 7, ry - 13, rx + 9, ry - 11, COL_BLUE);
    fill(rx + 4, ry - 10, rx + 8, ry - 10, COL_RED);
    fill(rx + 4, ry - 7, rx + 8, ry + 3, COL_ORANGE);
    text(108, 30, "BUSTED!", TX_RED);
    text(64, 245, "PARTY TIME IS TICKING!", TX_GREY);
}

static void jail_frame(const GameState *gs)
{
    int16_t secs = gs->jail_timer / 50 + 1;
    if (secs > 9) secs = 9;
    sprintf(buf, "RELEASED IN %d", secs);
    text(100, 220, buf, TX_YELLOW);
    fill(40, 45, 55, 55, (gs->jail_timer & 8) ? COL_RED : COL_BLUE);     /* the siren */
    fill(265, 45, 280, 55, (gs->jail_timer & 8) ? COL_RED : COL_BLUE);
    tm_fill(0, ROW(105), 40, 1, 0, 0);
    if (gs->rj_bubble_timer > 0 && gs->rj_bubble[0]) {
        int16_t bw = (int16_t)strlen(gs->rj_bubble) * 8 + 4;
        view_print(COL(160 - bw / 2 + 2), ROW(105), gs->rj_bubble, TX_RJ, 1);
    }
}

/* the credits scroll up from the bottom: the tilemap's fine Y scroll, the
   text rewritten every 8 lines */
static void credits(const GameState *gs)
{
    int16_t base = gs->credits_scroll & ~7, y, i;
    tm_scroll(0, (uint8_t)(gs->credits_scroll & 7));
    if (base == scroll_shown) return;
    scroll_shown = base;
    tm_clear();
    y = 256 - base;
#define CL(x, s, pen) do { if (y > 6 && y < 256) view_print(COL(x), ROW(y), s, pen, 0); } while (0)
    CL(52, "HAPPY 70TH BIRTHDAY", TX_YELLOW); y += 16;
    CL(116, "RJ!", TX_YELLOW); y += 32;
    CL(72, "FROM YOUR FRIENDS:", TX_WHITE); y += 24;
    for (i = 0; i < gs->name_count; i++) {
        CL((SCREEN_W - (int16_t)strlen(gs->names[i]) * 8) / 2, gs->names[i], TX_YEL2);
        y += 16;
    }
    y += 16;
    CL(36, "THANK YOU FOR THE AMIGA", TX_LTBLUE); y += 16;
    CL(52, "THE LYNX, THE 3DO,", TX_LTBLUE); y += 16;
    CL(28, "AND EVERYTHING IN BETWEEN.", TX_LTBLUE); y += 24;
    CL(44, "YOU CHANGED THE WORLD.", TX_YELLOW); y += 32;
    CL(20, "chris@hitorro.com 2026", TX_GREY); y += 24;
    CL(80, "PRESS FIRE TO PLAY", TX_GREY);
#undef CL
}

void screen_enter(const GameState *gs)
{
    view_hide_sprites();
    tm_clear();
    tm_scroll(0, 0);
    blink_shown = 2;
    scroll_shown = entry_shown = -1;
    if (gs->state == GS_TITLE) {
        view_title();
        title_text();
        return;
    }
    view_blank();
    switch (gs->state) {
    case GS_HELP: help_text(); break;
    case GS_GAMEOVER: gameover_text(gs); break;
    case GS_WIN: win_screen(gs); break;
    case GS_HISCORE: hiscore_text(gs); break;
    case GS_ENTER_NAME: case GS_ADD_GUEST: entry_static(gs); break;
    case GS_JAIL: jail_picture(); break;
    }
}

void screen_frame(const GameState *gs)
{
    switch (gs->state) {
    case GS_TITLE:
        if (((gs->title_blink >> 4) & 1) != blink_shown) {
            blink_shown = (gs->title_blink >> 4) & 1;
            text(60, 206, blink_shown ? "PRESS FIRE TO PARTY!" : "                    ", TX_YEL2);
        }
        break;
    case GS_CREDITS: credits(gs); break;
    case GS_ENTER_NAME: case GS_ADD_GUEST:
        if (gs->entry_pos * 2 + ((gs->entry_blink >> 3) & 1) != entry_shown) {
            entry_shown = gs->entry_pos * 2 + ((gs->entry_blink >> 3) & 1);
            entry_name(gs);
        }
        break;
    case GS_GUEST_EDIT:
        if (gs->edit_cursor * 32 + gs->name_count != entry_shown) {
            entry_shown = gs->edit_cursor * 32 + gs->name_count;
            editor(gs);
        }
        break;
    case GS_JAIL: jail_frame(gs); break;
    }
}
