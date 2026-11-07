// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - ScAIry Fun's front end on the Next (port of the original's game.c
 * and render.c's overlays): the keys, the rules pages, pause, the views,
 * sounds for the simulation's events, the log the tests read, the HUD, the
 * title, the route boxes, the software update and the high scores.
 *
 * port: text and panels are on the tilemap (sdk nexttile.h), over Layer 2
 * and the sprites: 40 x 32 cells over the whole 320 x 256 display, so the 3D
 * view never redraws text. The AI's view from above is drawn into Layer 2
 * under a hole in the HUD. Runs from bank 20 at 0x0000 (map_front()).
 * High scores are new (the original has none): five, with initials.
 */
#include <stdint.h>
#include <string.h>
#include <input.h>
#include <arch/zxn.h>

#include "zxgame.h"
#include "nexttile.h"
#include "l2poly.h"
#include "zxlog.h"
#include "sc.h"
#include "render.h"
#include "game.h"
#include "palfx.h"

/* tilemap layout (main.c ui_font): tiles 32..127 the ROM font with a shadow
   on nothing, 128..223 on the panel, 1 solid panel, 2..9 box edges, 10..18
   bars, 220..255 the title's big letters */
#define T_SHADOW   32
#define T_PANEL    128
#define T_SOLID    1
#define T_BAR      10
#define T_BIG      220
/* palette offsets: the text colour (pen 1); +8: on black instead of the panel */
enum { C_WHITE, C_AI, C_AI2, C_YELLOW, C_ORANGE, C_RED, C_GREY, C_PANEL };
#define C_DARK 8

#define ROW(y)  (4 + (y) / 8)            /* tile row of Layer 2 row y */
/* zxgame.h's input_read() bits (sc.h has the world's IN_* names) */
#define IN_LEFT_SDK  0x01
#define IN_RIGHT_SDK 0x02
#define IN_UP_SDK    0x04
#define IN_DOWN_SDK  0x08
#define IN_FIRE_SDK  0x10
#define IN_START_SDK 0x40
#define IN_QUIT_SDK  0x80
#define COL(x)  (4 + (x) / 8)

unsigned game_in;
uint8_t game_reset;                       /* back to the title (main.c does it) */
static unsigned prev_in;
static uint8_t paused, help_page, help_auto, view_was = 0xFF, last_state = 0xFF;
static uint16_t title_t;                  /* 1/50 s into the title's cycle */
static uint8_t screen;                    /* what the tilemap shows (redrawn when it changes) */
static uint8_t entry;                     /* typing initials */
static hs_editor ed;
static int8_t entry_rank;
static char hud_txt[6][14];                   /* the HUD's text as last drawn */
static uint8_t bar_cover, bar_pos;
static uint8_t blink;
static uint16_t frames;

#define TITLE_DEMO 600
#define TITLE_PAGE 400
#define HELP_PAGES 3

/* the original world.c's (port: here, with what shows them) */
const char *const msg_text[MSG_COUNT] = {
    "", "AUTOPILOT ENGAGED", "AUTOPILOT DISENGAGED", "I HAD THAT.",
    "GOOD CATCH.", "RECALCULATING...", "THAT IS A ROAD. PROBABLY.",
    "OBSTACLE DETECTED!", "KEEP YOUR HANDS ON THE WHEEL", "OOPS.",
    "UPDATE INSTALLED. I AM BETTER NOW.", "AUTOPILOT UNAVAILABLE",
    "WELCOME TO SCAIRY FUN", "WATER DETECTED. PROCEEDING ANYWAY.",
    "YOU HAVE ARRIVED.", "CAMERA BLINDED", "I AM VERY PATIENT.",
    "TICKET: FAILED TO YIELD", "TOW TRUCK ON ITS WAY", "POLICY CANCELLED"
};

const char *const ev_name[EV_COUNT] = {
    "none", "ap_on", "ap_off", "unavailable", "save", "needless", "scrape",
    "cone", "debris", "rearend", "crash", "splash", "roll", "ticket",
    "respawn", "phantom", "blinded", "aquaplane", "arrived", "game_over",
    "mult_up", "siren", "route_start", "hit_animal", "say", "win"
};

#ifndef SC_TRACE
static const char *const state_names[] = { "title", "intro", "play", "arrived", "update", "gameover", "win" };

/* ---------- text ---------- */
static void put(uint8_t col, uint8_t row, const char *s, uint8_t c)
{
    tm_print(col, row, s, T_SHADOW, c);
}

static void put_panel(uint8_t col, uint8_t row, const char *s, uint8_t c)
{
    tm_print(col, row, s, T_PANEL, c);
}

static void centre(uint8_t row, const char *s, uint8_t c)
{
    put(20 - (uint8_t)(strlen(s) / 2), row, s, c);
}

static void centre_panel(uint8_t row, const char *s, uint8_t c)
{
    put_panel(20 - (uint8_t)(strlen(s) / 2), row, s, c);
}

/* the big letters made at start-up: S C A I R Y F U N, space = blank */
static void big(uint8_t col, uint8_t row, const char *s, uint8_t c)
{
    static const char letters[] = "SCAIRYFUN";
    for (; *s; s++, col += 2) {
        const char *p = strchr(letters, *s);
        uint8_t t;
        if (!p || *s == ' ') continue;
        t = T_BIG + (uint8_t)(p - letters) * 4;
        tm_put(col, row, t, c);
        tm_put(col + 1, row, t + 1, c);
        tm_put(col, row + 1, t + 2, c);
        tm_put(col + 1, row + 1, t + 3, c);
    }
}

static char *num(char *p, uint32_t v, uint8_t digits)     /* zero-padded */
{
    uint8_t i = digits;
    p[i] = 0;
    while (i--) { p[i] = '0' + (uint8_t)(v % 10); v /= 10; }
    return p + digits;
}

static char *unum(char *p, uint32_t v)                     /* no padding */
{
    char t[11];
    uint8_t i = 10;
    t[10] = 0;
    do { t[--i] = '0' + (uint8_t)(v % 10); v /= 10; } while (v);
    strcpy(p, t + i);
    return p + strlen(p);
}

static void panel(uint8_t c0, uint8_t r0, uint8_t c1, uint8_t r1)
{
    tm_fill(c0, r0, c1 - c0 + 1, r1 - r0 + 1, T_SOLID, C_AI);
    tm_fill(c0, r0, c1 - c0 + 1, 1, 2, C_AI);               /* top edge */
    tm_fill(c0, r1, c1 - c0 + 1, 1, 3, C_AI);               /* bottom */
    tm_fill(c0, r0, 1, r1 - r0 + 1, 4, C_AI);               /* left */
    tm_fill(c1, r0, 1, r1 - r0 + 1, 5, C_AI);               /* right */
    tm_put(c0, r0, 6, C_AI); tm_put(c1, r0, 7, C_AI);
    tm_put(c0, r1, 8, C_AI); tm_put(c1, r1, 9, C_AI);
}

static void clear_view(void)                                /* the 3D view's text */
{
    tm_fill(0, 0, 40, 24, 0, 0);
}

static void blank_screen(void)                              /* black all over */
{
    tm_fill(0, 0, 40, 32, T_SOLID, C_DARK);
}

/* ---------- sound ---------- */
static void sfx(uint8_t id)
{
    sfx_queue = id;
}

/* ---------- input ---------- */
uint8_t game_input(void)
{
    uint8_t k = input_read(), fr = 0;

    k |= input_pressed();                     /* taps the interrupt latched */
    unsigned in = 0, edge;
    const World *w = &world;

    if (k & IN_LEFT_SDK) in |= IN_LEFT;
    if (k & IN_RIGHT_SDK) in |= IN_RIGHT;
    if (k & IN_UP_SDK) in |= IN_UP;
    if (k & IN_DOWN_SDK) in |= IN_DOWN;
    if (k & IN_FIRE_SDK) in |= IN_FIRE;
    if (k & IN_START_SDK) in |= IN_START;
    if (k & IN_QUIT_SDK) in |= IN_ESC;
    /* (latched by the interrupt, like input_read's: a tap shorter than a frame
       of the game still counts) */
    {
        uint8_t l = keys_latched;
        keys_latched = 0;
        if (l & 1 || key_down(IN_KEY_SCANCODE_v)) in |= IN_VIEW;
        if (l & 2 || key_down(IN_KEY_SCANCODE_h)) in |= IN_HELP;
        if (l & 4 || key_down(IN_KEY_SCANCODE_s)) in |= IN_PAUSE;
    }
    edge = in & ~prev_in;
    prev_in = in;
    game_in = in;

    if (entry) return 0;                        /* the initials screen reads the keys itself */
    if (edge & IN_HELP) {
        help_page = help_page < HELP_PAGES ? help_page + 1 : 0;
        help_auto = 0;
        zx_log("scairy: help page %u\n", help_page);
    }
    if (help_page && !help_auto && w->state != GS_TITLE && (edge & (IN_FIRE | IN_START | IN_ESC))) {
        help_page = 0;                          /* back to the game; this press does nothing else */
        game_in = 0;
        edge = 0;
    }
    if (edge & IN_PAUSE) {
        paused = !paused;
        zx_log(paused ? "scairy: paused\n" : "scairy: resumed\n");
    }
    if (edge & IN_VIEW) {
        world.view ^= 1;
        zx_log("scairy: view %s\n", world.view ? "cockpit" : "chase");
    }
    if ((edge & IN_ESC) && w->state != GS_TITLE) {
        zx_log("scairy: back to title\n");
        game_reset = 1;
    }
    /* game over: the press that would go back to the title enters a high score first */
    if ((w->state == GS_GAMEOVER || w->state == GS_WIN) && w->state_timer == 0 &&
        (edge & (IN_FIRE | IN_START)) && (entry_rank = hs_rank(w->score)) >= 0) {
        entry = 1;
        hs_edit_begin(&ed);
        zx_log("scairy: hiscore rank=%d\n", entry_rank);
        game_in = 0;
        screen = 0xFF;
        return 0;
    }

    /* on the title, the rules in turn between autopilot demos, then the best scores */
    if (w->state == GS_TITLE) {
        uint16_t cycle = TITLE_DEMO + (HELP_PAGES + 1) * TITLE_PAGE;
        if (++title_t >= cycle) title_t = 0;
        if (!help_page || help_auto) {
            uint8_t p = title_t < TITLE_DEMO ? 0 : 1 + (uint8_t)((title_t - TITLE_DEMO) / TITLE_PAGE);
            help_auto = p > 0;
            help_page = p;                      /* HELP_PAGES + 1: the high scores */
        }
    } else {
        title_t = 0;
        if (help_auto) help_page = help_auto = 0;
    }
    if (!paused && !(help_page && w->state != GS_TITLE)) fr |= FR_PLAY;
    if (!help_page && w->state != GS_UPDATE) fr |= FR_3D;
    return fr;
}

/* ---------- the simulation's events ---------- */
void game_events(void)
{
    Event e;
    World *w = &world;
    while (world_pop_event(w, &e)) {
        switch (e.type) {
        case EV_AP_ON:      sfx(SFX_CHIME_ON); break;
        case EV_AP_OFF:     sfx(SFX_CHIME_OFF); break;
        case EV_UNAVAILABLE:
        case EV_MULT_UP:    sfx(SFX_BLIP); break;
        case EV_SAVE:
        case EV_ARRIVED:    sfx(SFX_SAVE); break;
        case EV_SCRAPE:     sfx(SFX_SCRAPE); break;
        case EV_CONE:       sfx(SFX_CONE); break;
        case EV_DEBRIS: case EV_REAREND: case EV_CRASH: case EV_ROLL: case EV_HIT_ANIMAL:
                            sfx(SFX_CRASH); break;
        case EV_SPLASH:     sfx(SFX_SPLASH); break;
        case EV_PHANTOM:
        case EV_TICKET:     sfx(SFX_ALERT); break;
        }
        if (e.type == EV_SAY) zx_log("scairy: say %s\n", msg_text[e.a]);
        else zx_log("scairy: ev %s a=%ld b=%ld seg=%d score=%ld cover=%ld\n", ev_name[e.type], e.a, e.b,
                    seg_of(w->car.z), w->score, w->cover);
    }
    if (w->siren && (w->frame % 13) == 0) sfx(SFX_SIREN);
    if (w->state != last_state) {
        zx_log("scairy: state %s route %d score %ld\n", state_names[w->state], w->route + 1, w->score);
        if (w->state == GS_TITLE) zx_log("scairy: title\n");
        last_state = w->state;
        screen = 0xFF;
    }
}

/* ---------- the screens ---------- */
static const char *const help_title[HELP_PAGES] = { "HOW TO PLAY", "CONTROLS", "READING THE AI" };
static const char *const help_text[HELP_PAGES][12] = {
    { "YOUR CAR DRIVES ITSELF. YOU SCORE",
      "FOR EVERY METRE IT DRIVES ON AUTOPILOT,",
      "x THE MULTIPLIER: +1 EVERY 8 SECONDS",
      "THE AUTOPILOT STAYS ON, UP TO x8.",
      "",
      "BUT IT HAS BLIND SPOTS. TAKE THE WHEEL",
      "JUST BEFORE IT GETS SOMETHING WRONG,",
      "GET THROUGH, THEN HAND IT BACK.",
      "",
      "GOOD CATCH: YOU TOOK OVER IN TIME +500",
      "NEEDLESS: IT HAD THAT. MULTIPLIER x1.",
      "" },
    /* port: the Spectrum's keys */
    { "O / P          STEER (TAKES OVER)",
      "A              BRAKE (TAKES OVER)",
      "Q              ACCELERATE (MANUAL)",
      "SPACE          AUTOPILOT ON / OFF",
      "               (ONLY NEAR A LANE",
      "               CENTRE)",
      "V              CHASE / COCKPIT VIEW",
      "S              PAUSE",
      "H              THESE PAGES",
      "BREAK          TO THE TITLE",
      "CURSOR KEYS, SINCLAIR OR KEMPSTON",
      "JOYSTICK ALSO WORK." },
    { "CYAN RIBBON    WHERE IT MEANS TO GO",
      "GREEN BOXES    WHAT IT HAS SEEN",
      "RED BOX ?      SOMETHING IT IMAGINES",
      "BOTTOM RIGHT   ITS VIEW FROM ABOVE",
      "",
      "THERE ARE NO WARNINGS. WATCH THE ROAD:",
      "FLOOD WATER, OLD LANE LINES, OVERPASS",
      "SHADOWS, CONES, THE LOW SUN, SIRENS,",
      "DEBRIS. CRASHES COST INSURANCE COVER;",
      "AT ZERO YOUR POLICY IS CANCELLED.",
      "EACH DESTINATION ADDS COVER AND A",
      "BONUS FOR AUTOPILOT USE." },
};

static const char *const patch_notes[NUM_ROUTES][4] = {
    { "", "", "", "" },
    { "+ NOW DETECTS WATER", "+ IMPROVED LANE CONFIDENCE", "- REMOVED FEAR OF TRACTORS",
      "KNOWN ISSUE: SHADOWS LOOK SOLID" },
    { "+ PHANTOM BRAKING FIXED (MOSTLY)", "+ MOUNTAIN MODE", "+ SUNGLASSES (BETA)",
      "KNOWN ISSUE: SUNSETS" },
    { "+ CONFIDENCE INCREASED 400%", "+ CITY DRIVING", "- REMOVED SELF-DOUBT",
      "KNOWN ISSUE: EVERYTHING" },
};

static void help_screen(uint8_t page)
{
    uint8_t k;
    char tmp[24], *p;
    blank_screen();
    if (page > HELP_PAGES) {                    /* (the title's cycle) the best scores */
        centre_panel(6, "BEST DRIVERS", C_AI | C_DARK);
        for (k = 0; k < HS_ENTRIES; k++) {
            p = unum(tmp, k + 1);
            *p++ = '.'; *p++ = ' ';
            memcpy(p, hs_table[k].name, 3);
            p += 3;
            *p++ = ' '; *p++ = ' ';
            num(p, hs_table[k].score, 6);
            centre_panel(10 + k * 2, tmp, (k ? C_WHITE : C_YELLOW) | C_DARK);
        }
        centre_panel(26, "PRESS FIRE TO START", C_YELLOW | C_DARK);
        return;
    }
    centre_panel(3, help_title[page - 1], C_AI | C_DARK);
    for (k = 0; k < 12; k++)
        put_panel(1, 6 + k * 2, help_text[page - 1][k],
                  ((page == 1 && k >= 9) ? (k == 9 ? C_AI2 : C_ORANGE) : C_WHITE) | C_DARK);
    p = unum(tmp, page);
    strcpy(p, " OF 3");
    put_panel(1, 30, "PAGE ", C_GREY | C_DARK);
    put_panel(6, 30, tmp, C_GREY | C_DARK);
    put_panel(17, 30, "H: NEXT  FIRE: PLAY", C_YELLOW | C_DARK);
}

static void update_screen(const World *w)
{
    uint8_t next = w->route + 1 < NUM_ROUTES ? w->route + 1 : NUM_ROUTES - 1, k;
    uint16_t done = 200 - w->state_timer, fill;
    char tmp[32], *p;
    if (screen != 2) {
        blank_screen();
        centre_panel(4, "SOFTWARE UPDATE", C_AI | C_DARK);
        strcpy(tmp, "AUTOPILOT v");
        strcat(tmp, route_info[next].ai_name);
        centre_panel(8, tmp, C_WHITE | C_DARK);
        screen = 2;
    }
    for (k = 0; k < 4; k++)
        if (done > 20 + k * 12) put_panel(5, 12 + k * 2, patch_notes[next][k], (k == 3 ? C_ORANGE : C_AI2) | C_DARK);
    fill = done * 2;
    if (fill > 240) fill = 240;
    /* the bar: 30 cells of 8 = 240 */
    for (k = 0; k < 30; k++) {
        uint16_t f = fill > k * 8 ? fill - k * 8 : 0;
        tm_put(5 + k, 22, T_BAR + (f > 8 ? 8 : f), C_AI);
    }
    if (fill >= 240) {
        centre_panel(25, "INSTALLED. I AM BETTER NOW.", C_WHITE | C_DARK);
        if (done > 40) centre_panel(27, "PRESS FIRE", (blink ? C_YELLOW : C_WHITE) | C_DARK);
    } else {
        strcpy(tmp, "INSTALLING... ");
        p = unum(tmp + strlen(tmp), (uint32_t)fill * 100 / 240);
        strcpy(p, "%");
        centre_panel(25, tmp, C_GREY | C_DARK);
    }
}

/* the boxes over the road (the original's state_box) */
static void state_box(const World *w)
{
    const RouteInfo *r = &route_info[w->route];
    char tmp[40], *p;
    switch (w->state) {
    case GS_INTRO:
        panel(5, 8, 34, 20);
        strcpy(tmp, "ROUTE ");
        p = unum(tmp + 6, w->route + 1);
        strcpy(p, " OF 4");
        centre_panel(10, tmp, C_GREY);
        centre_panel(12, r->name, C_YELLOW);
        strcpy(tmp, "TO ");
        strcat(tmp, r->dest);
        centre_panel(14, tmp, C_WHITE);
        strcpy(tmp, "AUTOPILOT v");
        strcat(tmp, r->ai_name);
        centre_panel(16, tmp, C_AI);
        centre_panel(18, "PRESS FIRE", blink ? C_YELLOW : C_WHITE);
        break;
    case GS_ARRIVED:
        panel(4, 5, 35, 21);
        strcpy(tmp, "ARRIVED: ");
        strcat(tmp, r->dest);
        centre_panel(7, tmp, C_YELLOW);
#define LINE(row, label, v, c) do { strcpy(tmp, label); p = tmp + strlen(tmp); num(p, v, 3); \
            centre_panel(row, tmp, c); } while (0)
        strcpy(tmp, "AUTOPILOT USED   ");
        p = num(tmp + 17, world_ap_pct(w), 3);
        strcpy(p, "%");
        centre_panel(9, tmp, C_AI);
        LINE(11, "GOOD CATCHES     ", w->saves, C_AI2);
        LINE(13, "NEEDLESS GRABS   ", w->needless, C_ORANGE);
        LINE(15, "INSURANCE COVER  ", w->cover, C_WHITE);
#undef LINE
        strcpy(tmp, "BONUS ");
        unum(tmp + 6, w->route_bonus);
        centre_panel(17, tmp, C_YELLOW);
        if (w->state_timer < 210) centre_panel(19, "PRESS FIRE", blink ? C_YELLOW : C_WHITE);
        break;
    case GS_GAMEOVER:
        panel(5, 8, 34, 19);
        centre_panel(10, "CANCELLED", C_RED);
        centre_panel(12, "YOUR INSURER HAS HAD ENOUGH", C_WHITE);
        strcpy(tmp, "SCORE ");
        unum(tmp + 6, w->score);
        centre_panel(14, tmp, C_YELLOW);
        if (w->state_timer == 0) centre_panel(17, "PRESS FIRE", C_WHITE);
        break;
    case GS_WIN:
        panel(3, 6, 36, 21);
        centre_panel(8, "YOU'RE HOME", C_YELLOW);
        strcpy(tmp, "FINAL SCORE ");
        unum(tmp + 12, w->score);
        centre_panel(10, tmp, C_WHITE);
        strcpy(tmp, "GOOD CATCHES ");
        p = unum(tmp + 13, w->total_saves);
        strcpy(p, "  NEEDLESS ");
        unum(p + 11, w->total_needless);
        centre_panel(12, tmp, C_AI2);
        centre_panel(14, "THE AI THANKS YOU", C_AI);
        centre_panel(16, "FOR YOUR TRAINING DATA", C_AI);
        if (w->state_timer == 0) centre_panel(19, "PRESS FIRE", C_WHITE);
        break;
    }
}

static void title_text(void)
{
    big(10, 6, "SC", C_WHITE);
    big(14, 6, "AI", C_AI);
    big(18, 6, "RY FUN", C_WHITE);
    centre(10, "A SELF-DRIVING ADVENTURE", C_YELLOW);
    centre(18, "STEER OR BRAKE TO TAKE OVER", C_WHITE);
    centre(19, "SPACE: AUTOPILOT   V: VIEW", C_WHITE);
    centre(21, "PRESS FIRE TO START", blink ? C_YELLOW : C_WHITE);
    centre(22, "H: HOW TO PLAY", C_GREY);
}

/* ---------- the HUD ---------- */
static void hud_line(uint8_t slot, uint8_t col, uint8_t row, const char *s, uint8_t c)
{
    if (!strcmp(hud_txt[slot], s)) return;
    strcpy(hud_txt[slot], s);
    tm_fill(col, row, 12, 1, T_SOLID, C_PANEL);
    put_panel(col, row, s, c);
}

static void hud_reset(void)
{
    uint8_t k;
    for (k = 0; k < 6; k++) hud_txt[k][0] = 1;          /* nothing matches: redraw */
    tm_fill(0, 24, 40, 8, T_SOLID, C_PANEL);
    tm_fill(COL(192), 24, 8, 4, 0, 0);              /* the hole for the AI's view */
}

static void bar(uint8_t col, uint8_t row, uint8_t cells, uint16_t val, uint16_t max, uint8_t c)
{
    uint16_t fill = (uint16_t)(((uint32_t)val * cells * 8) / max);
    uint8_t k;
    for (k = 0; k < cells; k++) {
        uint16_t f = fill > k * 8 ? fill - k * 8 : 0;
        tm_put(col + k, row, T_BAR + (f > 8 ? 8 : f), c);
    }
}

static void hud(const World *w)
{
    char tmp[16], *p;
    int32_t togo;
    if (w->state == GS_TITLE) {
        hud_line(0, 1, 25, "A NEXT GAME", C_WHITE);
        hud_line(1, 1, 26, "AUTOPILOT DEMO", C_AI);
        hud_line(2, 1, 27, "TAKE OVER WHEN", C_GREY);
        hud_line(3, 15, 27, "IT MATTERS", C_GREY);
        return;
    }
    strcpy(tmp, "SCORE ");
    num(tmp + 6, w->score, 6);
    hud_line(0, 1, 25, tmp, C_WHITE);
    if (w->ai.on) {
        strcpy(tmp, "AUTO x");
        unum(tmp + 6, w->mult);
    } else strcpy(tmp, "MANUAL");
    hud_line(1, 1, 26, tmp, w->ai.on ? C_AI : C_ORANGE);
    strcpy(tmp, "AP ");
    p = num(tmp + 3, world_ap_pct(w), 3);
    strcpy(p, "% R");
    unum(p + 3, w->route + 1);
    hud_line(2, 1, 27, tmp, C_GREY);
    p = num(tmp, KMH(w->car.v), 3);
    strcpy(p, " KM/H");
    hud_line(3, 15, 25, tmp, C_WHITE);
    togo = ((int32_t)w->finish - seg_of(w->car.z)) * 8;
    if (togo < 0) togo = 0;
    p = unum(tmp, togo / 1000);
    *p++ = '.';
    p = unum(p, (togo % 1000) / 100);
    strcpy(p, " KM");
    hud_line(4, 15, 27, tmp, C_GREY);
    if (hud_txt[5][0]) {                            /* (redrawn: the bars too) */
        hud_txt[5][0] = 0;
        put_panel(15, 26, "COVER", C_GREY);
        bar_cover = bar_pos = 0xFF;
    }
    if ((uint8_t)w->cover != bar_cover) {
        bar_cover = (uint8_t)w->cover;
        bar(22, 26, 6, w->cover > 0 ? w->cover : 0, 100, w->cover < 30 ? C_RED : C_AI2);
    }
    {
        /* progress along the route: a marker on a line */
        uint8_t pos = (uint8_t)(((uint32_t)seg_of(w->car.z) * 48) / (w->finish ? w->finish : 1)), k;
        if (pos > 47) pos = 47;
        if (pos != bar_pos) {
            bar_pos = pos;
            for (k = 0; k < 6; k++)
                tm_put(22 + k, 27, (pos >> 3) == k ? T_BAR + 9 + (pos & 7) : T_BAR + 17, C_GREY);
        }
    }
}

/* the AI's picture of the road from above, into Layer 2 under the HUD's hole */
static void ai_view(const World *w)
{
    const Ai *a = &w->ai;
    const Car *car = &w->car;
#define X0 193
#define Y0 161
#define X1 254
#define Y1 190
    int16_t l = road_left(w) - SHOULDER, r = road_right(w) + SHOULDER, span = r - l;
    int16_t kx = (int16_t)(((X1 - X0 - 4) << 8) / span);       /* port: a multiply a point, not a division */
    int32_t cz = car->z >> FBITS;
    uint8_t i, n, nl = route_info[w->route].nl, xs[8], nx = 0;
#define AX(u) (X0 + 2 + (int16_t)(((int32_t)((u) - l) * kx) >> 8))
#define AY(dz) (Y1 - 4 - (int16_t)(((dz) * 1877) >> 17))          /* dz x (Y1 - Y0 - 8) / (12 x SEG_LEN) */
    l2p_begin();
    l2p_top = 0;
    l2p_bot = 192;
    l2p_rect(Y0 - 1, Y1 + 2, X0 - 1, X1 + 1, PEN_BLACK);
    if (a->blinded) {
        uint32_t h = (uint32_t)w->frame * 2654435761UL;
        for (i = 0; i < 40; i++) {
            h = h * 1103515245UL + 12345UL;
            l2p_span(Y0 + (uint8_t)((h >> 20) % (Y1 - Y0)), X0 + (uint8_t)((h >> 8) % (X1 - X0)),
                     X0 + (uint8_t)((h >> 8) % (X1 - X0)), (h & 1) ? PEN_WHITE : PEN_GREY);
        }
        l2p_end();
        return;
    }
    xs[nx++] = AX(road_left(w));
    xs[nx++] = AX(road_right(w));
    if (route_info[w->route].no) xs[nx++] = AX(0);
    for (n = 1; n < nl; n++) xs[nx++] = AX(n * LANE_W);
    for (i = Y0; i < Y1; i += 3) {
        l2p_span(i, xs[0], xs[0], PEN_GREY);
        l2p_span(i, xs[1], xs[1], PEN_GREY);
        if (nx > nl + 1) l2p_span(i, xs[2], xs[2], PEN_YELLOW);
        if (i % 6 == 0)
            for (n = nx - nl + 1; n < nx; n++) l2p_span(i, xs[n], xs[n], PEN_PANEL);
    }
    if (a->on)
        for (i = 0; i < 12; i++) {
            int32_t dz = (((int32_t)(a->plan_from + i)) << SEG_BITS) - cz;
            int16_t px = AX(a->plan[i] >> FBITS), py = AY(dz);
            if (dz < 0 || py <= Y0 || px <= X0 || px >= X1) continue;
            l2p_span((uint8_t)py, px, px, PEN_AI);
        }
    for (i = 0; i < a->nseen; i++) {
        const Seen *s = &a->seen[i];
        int32_t dz = (s->z >> FBITS) - cz;
        int16_t px = AX(s->d >> FBITS), py = AY(dz);
        if (py - 2 <= Y0 || py + 2 >= Y1 || px - 1 <= X0 || px + 1 >= X1) continue;
        if (s->phantom) l2p_rect(py - 3, py + 3, px - 4, px + 4, (w->frame & 4) ? PEN_RED : PEN_YELLOW);
        else l2p_rect(py - 2, py + 2, px - 1, px + 1, PEN_AI2);
    }
    {
        int16_t px = AX(car->d >> FBITS);
        if (px > X0 + 1 && px < X1 - 1) l2p_rect(Y1 - 7, Y1 - 2, px - 1, px + 1, PEN_RED);
    }
    l2p_end();
#undef AX
#undef AY
}

/* ---------- the frame's text ---------- */
void game_draw(void)
{
    const World *w = &world;
    uint8_t want;
    frames++;
    blink = (frames >> 4) & 1;

    if (entry) {                                /* the initials */
        uint8_t r = hs_edit_step(&ed);
        if (screen != 3) {
            blank_screen();
            centre_panel(8, "A NEW BEST DRIVER", C_YELLOW | C_DARK);
            centre_panel(12, "TYPE YOUR INITIALS", C_WHITE | C_DARK);
            centre_panel(14, "OR UP / DOWN AND FIRE", C_GREY | C_DARK);
            screen = 3;
            r |= HS_EDIT_CHANGED;
        }
        if (r & HS_EDIT_TAKEN) sfx(SFX_KEY);
        if (r & HS_EDIT_CHANGED) {
            char t[4];
            memcpy(t, ed.name, 3);
            t[3] = 0;
            put_panel(18, 18, t, C_AI | C_DARK);
            tm_fill(18, 19, 3, 1, T_SOLID, C_DARK);
            if (ed.pos < 3) tm_put(18 + ed.pos, 19, '^' - 32 + T_PANEL, C_YELLOW | C_DARK);
        }
        if (r & HS_EDIT_DONE) {
            ed.name[3] = 0;
            hs_insert(entry_rank, ed.name, w->score);
            zx_log("scairy: name=%s\n", ed.name);
            entry = 0;
            game_reset = 1;                     /* main.c: back to the title */
        }
        return;
    }

    want = help_page ? 10 + help_page : w->state == GS_UPDATE ? 2 : 1;
    if (want == 2) {
        update_screen(w);
        return;
    }
    if (want >= 10) {
        if (screen != want) {
            help_screen(help_page);
            screen = want;
        }
        return;
    }
    if (screen != 1 || w->view != view_was) {
        clear_view();
        hud_reset();
        screen = 1;
        view_was = w->view;
    }
    /* the view's text, drawn again when what it shows changes */
    {
        static uint8_t sig[8];
        uint8_t now[8];
        now[0] = w->state;
        now[1] = w->msg;
        now[2] = blink;
        now[3] = w->state == GS_PLAY && w->car.state == CS_AQUAPLANE && (w->frame & 8);
        now[4] = w->state == GS_PLAY && !w->ai.on && w->car.state == CS_DRIVING &&
                 w->car.v < FROM_KMH(5) && (w->frame & 16);
        now[5] = paused;
        now[6] = (w->state == GS_ARRIVED && w->state_timer < 210) | (w->state_timer == 0) << 1;
        now[7] = w->route;
        if (!memcmp(now, sig, 8)) goto hud_only;
        memcpy(sig, now, 8);
    }
    tm_fill(0, 4, 40, 20, 0, 0);
    if (w->state == GS_TITLE) title_text();
    else state_box(w);
    if (w->msg != MSG_NONE && w->state != GS_TITLE) {
        uint8_t col = (w->msg == MSG_TICKET || w->msg == MSG_TOW || w->msg == MSG_CANCELLED) ? C_YELLOW
                      : w->msg == MSG_GOOD_CATCH ? C_AI2 : C_AI;
        centre(ROW(14), msg_text[w->msg], col);
    }
    if (w->state == GS_PLAY && w->car.state == CS_AQUAPLANE && (w->frame & 8))
        centre(ROW(28), "AQUAPLANING", C_WHITE);
    if (w->state == GS_PLAY && !w->ai.on && w->car.state == CS_DRIVING && w->car.v < FROM_KMH(5) &&
        (w->frame & 16))
        centre(ROW(112), "SPACE: AUTOPILOT   Q: DRIVE", C_YELLOW);
    if (paused) centre(ROW(72), "PAUSED", C_WHITE);
hud_only:
    hud(w);
    ai_view(w);
}

/* the tilemap's tiles: the ROM's font (copied by main.c into page 39, mapped
   here at 0x6000 meanwhile) with a
   shadow on nothing and on the panel, panel and box edges, bars, the title's
   big letters (game.c says which tile is what) */
static uint8_t tile[32];

static void put_px(uint8_t x, uint8_t y, uint8_t pen)
{
    uint8_t *b = &tile[y * 4 + x / 2];
    if (x & 1) *b = (*b & 0xF0) | pen;
    else *b = (*b & 0x0F) | pen << 4;
}

void ui_font(void)
{
    const uint8_t *rom = (const uint8_t *)0x6000;
    static const uint8_t big_letters[] = "SCAIRYFUN";
    uint8_t c, x, y, k;
    ZXN_WRITE_MMU3(39);
    tm_init();
    memset(tile, 0, 32);
    tm_def(0, tile, 1);
    for (c = 0; c < 96; c++) {
        uint8_t g[8];
        memcpy(g, rom + c * 8, 8);              /* (tm_def maps bank 5 over 0x6000) */
        /* on the panel (pen 3), pen 1 */
        memset(tile, 0x33, 32);
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                if (g[y] & (0x80 >> x)) put_px(x, y, 1);
        tm_def(128 + c, tile, 1);
        /* on nothing: pen 1, a shadow in pen 2 down and right */
        memset(tile, 0, 32);
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                if (g[y] & (0x80 >> x)) {
                    if (x < 7 && y < 7) put_px(x + 1, y + 1, 2);
                }
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                if (g[y] & (0x80 >> x)) put_px(x, y, 1);
        tm_def(32 + c, tile, 1);
    }
    /* solid, box edges (top, bottom, left, right, corners), bars 0..8, progress */
    memset(tile, 0x33, 32);
    tm_def(1, tile, 1);
    for (k = 2; k < 10; k++) {
        memset(tile, 0x33, 32);
        for (x = 0; x < 8; x++) {
            if (k == 2 || k == 6 || k == 7) put_px(x, 1, 1);
            if (k == 3 || k == 8 || k == 9) put_px(x, 6, 1);
            if (k == 4 || k == 6 || k == 8) put_px(1, x, 1);
            if (k == 5 || k == 7 || k == 9) put_px(6, x, 1);
        }
        tm_def(k, tile, 1);
    }
    for (k = 0; k < 9; k++) {
        memset(tile, 0x33, 32);
        for (y = 1; y < 7; y++) for (x = 0; x < k; x++) put_px(x, y, 1);
        tm_def(10 + k, tile, 1);
    }
    for (k = 0; k < 9; k++) {                       /* 19..26: a marker at k on a line, 27: the line */
        memset(tile, 0x33, 32);
        for (x = 0; x < 8; x++) put_px(x, 4, 2);
        if (k < 8) for (y = 1; y < 7; y++) put_px(k, y, 1);
        tm_def(19 + k, tile, 1);
    }
    for (k = 0; k < 9; k++) {
        uint8_t g[8];
        memcpy(g, rom + (big_letters[k] - 32) * 8, 8);
        tm_glyph2x(220 + k * 4, g, 1, 2);
    }
    /* palettes: pen 1 the text's colour, 2 its shadow, 3 the panel (+8: black) */
    {
        static const uint16_t fg[8] = { RGB9(7, 7, 7), RGB9(1, 7, 7), RGB9(2, 7, 2), RGB9(7, 7, 1),
                                        RGB9(7, 3, 1), RGB9(7, 1, 1), RGB9(5, 5, 5), RGB9(1, 1, 1) };
        for (k = 0; k < 16; k++) {
            tm_pal(k, 1, fg[k & 7]);
            tm_pal(k, 2, RGB9(0, 0, 0));
            tm_pal(k, 3, k & 8 ? RGB9(0, 0, 0) : RGB9(1, 1, 1));
        }
    }
    ZXN_WRITE_MMU3(11);
}

void game_init(void)
{
    hs_init(2000);
    screen = 0xFF;
}
#endif
