/*
 * game.c - Marble Tilt's game shell for the Next (the original's game.c and
 * the text parts of render.c): the title with the autopilot rolling behind
 * it, play, game over, the help pages, sound, and here the high scores.
 *
 * The rules run at the original's 25 ticks a second whatever the drawing
 * costs: each pass of the main loop runs a tick for every two frames that
 * went by (fixed timestep), then the picture catches up: the marble sprite,
 * the board if its tilt or gates changed (view.c), the text.
 *
 * port: keys. The original's P (pause) is right on QAOP, so ENTER pauses;
 * H is the help (as the original's Help/H), B the autopilot, BREAK (the
 * original's Esc) goes back to the title. Fire or ENTER starts.
 * port: the high-score table and the initials (the original keeps a best
 * score only).
 */
#include <string.h>
#include <stdio.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "sound.h"

Tilt tl;
MTGame game;
static uint32_t next_seed = 1;
static uint8_t acc, prev_keys, hud_dirty = 1;
static unsigned game_in;                 /* the tilt keys, as the rules' input bits */
static hs_editor ed;
static int8_t ed_place;
static char tmp[40];

/* what the HUD shows now (redrawn when it changes) */
static int16_t shown_secs = -1, shown_clock = -1;
static int32_t shown_score = -1;
static int16_t shown_falls = -1;
static uint8_t shown_gates = 2, shown_auto = 2, shown_msg = 0xFF, shown_blink = 2;

static const char *const state_names[] = { "TITLE", "PLAY", "OVER", "ENTRY" };

static void set_state(uint8_t s)
{
    tl.state = s;
    hud_dirty = 1;
    zx_log("mt: state %s level=%u score=%ld falls=%u\n", state_names[s], game.level, game.score, game.falls);
}

static void flash(const char *s, uint8_t ink)
{
    strncpy(tl.flash, s, sizeof(tl.flash) - 1);
    tl.flash_timer = 30;
    tl.flash_ink = ink;
    hud_dirty = 1;
}

static void start_game(uint8_t level)
{
    mt_new_game(&game, next_seed++, level);
    tl.paused = 0;
    tl.help = 0;
    tl.autoplay = 0;
    snd_music(MUS_PLAY);
    set_state(TS_PLAY);
    zx_log("mt: start level %u %s\n", level, mt_level_names[level - 1]);
}

static void to_title(void)
{
    mt_new_game(&game, 777 + next_seed, 1);
    tl.title_page = 0;
    tl.state_timer = 0;
    snd_music(MUS_TITLE);
    set_state(TS_TITLE);
}

void game_init(void)
{
    tl.start_level = 1;
    hs_init(400);
    to_title();
}

/* the original's handle_events(): sounds and messages (quiet behind the title) */
static void events(void)
{
    MTEvent e;
    uint8_t quiet = tl.state == TS_TITLE;
    while (mt_pop_event(&game, &e)) {
        if (quiet) continue;
        switch (e.type) {
        case MTE_WALL: snd_play(SFX_WALL); break;
        case MTE_BUMP: snd_play(SFX_BUMP); break;
        case MTE_FALL: snd_play(SFX_FALL); flash("FELL THROUGH -3 S", TX_RED); break;
        case MTE_RESPAWN: snd_play(SFX_RESPAWN); break;
        case MTE_CHECKPOINT: snd_play(SFX_CP); flash("CHECKPOINT", TX_CYAN); break;
        case MTE_GATE: snd_play(SFX_GATE); break;
        case MTE_EXIT: snd_play(SFX_EXIT); break;
        case MTE_TIMEOUT: flash("TIME UP", TX_RED); break;
        case MTE_OVER: snd_play(SFX_OVER); break;
        case MTE_WIN: snd_play(SFX_WIN); break;
        }
        if (e.type != MTE_WALL && e.type != MTE_GATE)
            zx_log("mt: ev %s a=%d b=%d score=%ld level=%u time=%ld\n", mt_event_names[e.type], e.a, e.b, game.score,
                   game.level, game.time_left / 25);
    }
}

/* ---- input ---- */
#define K_HELP 1
#define K_AUTO 2

static void input(void)
{
    uint8_t held = input_read(), edge = input_pressed(), keys = 0, kedge;
    if (key_down(IN_KEY_SCANCODE_h)) keys |= K_HELP;
    if (key_down(IN_KEY_SCANCODE_b)) keys |= K_AUTO;
    kedge = keys & ~prev_keys;
    prev_keys = keys;
    if (tl.state == TS_ENTRY) return;            /* the initials editor reads the keys itself */
    if (kedge & K_HELP) {
        tl.help = (tl.help + 1) % 3;
        hud_dirty = 1;
        return;
    }
    if (tl.help) {
        if (edge & (IN_FIRE | IN_START | IN_QUIT)) { tl.help = 0; hud_dirty = 1; }
        return;
    }
    if ((edge & IN_QUIT) && tl.state != TS_TITLE) {
        to_title();
        return;
    }
    game_in = 0;
    switch (tl.state) {
    case TS_TITLE:
        if ((edge & (IN_UP | IN_RIGHT)) && tl.start_level < MT_LEVELS) { tl.start_level++; hud_dirty = 1; }
        if ((edge & (IN_DOWN | IN_LEFT)) && tl.start_level > 1) { tl.start_level--; hud_dirty = 1; }
        if (edge & (IN_FIRE | IN_START)) start_game(tl.start_level);
        break;
    case TS_PLAY:
        if (edge & IN_START) { tl.paused = !tl.paused; hud_dirty = 1; }
        if (kedge & K_AUTO) {
            tl.autoplay = !tl.autoplay;
            hud_dirty = 1;
            zx_log("mt: autoplay %u\n", tl.autoplay);
        }
        game_in = (held & IN_LEFT ? MT_IN_LEFT : 0) | (held & IN_RIGHT ? MT_IN_RIGHT : 0) |
                  (held & IN_UP ? MT_IN_UP : 0) | (held & IN_DOWN ? MT_IN_DOWN : 0);
        break;
    case TS_OVER:
        if (tl.state_timer == 0 && (edge & (IN_FIRE | IN_START))) {
            ed_place = hs_rank((uint32_t)game.score);
            if (ed_place >= 0) {
                hs_edit_begin(&ed);
                set_state(TS_ENTRY);
            } else to_title();
        }
        break;
    }
}

/* ---- the rules, 25 ticks a second (the original's game_update) ---- */
static void tick(void)
{
    if (tl.help || tl.paused || tl.state == TS_ENTRY) return;
    if (tl.state == TS_TITLE) {
        if (game.over || game.won || game.between == 1)
            mt_new_game(&game, game.seed + 1, 1 + (int)(game.seed % MT_LEVELS));
        mt_tick(&game, mt_bot(&game));
        if (++tl.state_timer >= 150) {           /* port: the title and the high scores in turn */
            tl.state_timer = 0;
            tl.title_page ^= 1;
            hud_dirty = 1;
        }
    } else if (tl.state == TS_PLAY) {
        mt_tick(&game, tl.autoplay ? mt_bot(&game) : game_in);
    }
    events();
    if (tl.flash_timer > 0 && --tl.flash_timer == 0) hud_dirty = 1;
    if (tl.state == TS_OVER && tl.state_timer > 0 && --tl.state_timer == 0) hud_dirty = 1;
    if (tl.state == TS_PLAY && (game.over || game.won)) {
        tl.state_timer = 40;
        snd_music(MUS_NONE);
        set_state(TS_OVER);
    }
}

/* ---- the text (render.c's hud(), render_game(), render_title(), render_help()) ---- */
static const char *const help_lines[2][12] = {
    { "TILT THE BOARD TO ROLL THE MARBLE TO",
      "THE GREEN EXIT BEFORE THE CLOCK RUNS",
      "OUT. IT KEEPS ROLLING: TILT BACK TO",
      "SLOW IT DOWN.",
      "",
      " HOLES    FALL IN: BACK TO THE LAST",
      "          CHECKPOINT (BLUE), -3 S",
      " RED POSTS   BUMPERS: THEY KICK IT",
      " RED BLOCKS  GATES: OPEN 3 S, SHUT 3 S",
      " TAN       SAND: IT DRAGS",
      "",
      "EIGHT LEVELS. SCORE: 10 A SECOND LEFT." },
    { "QAOP / CURSOR / JOYSTICK  TILT",
      "UP               ROLLS IT AWAY",
      "DOWN             ROLLS IT TOWARDS YOU",
      "ENTER            PAUSE",
      "B                AUTOPILOT",
      "H                THESE PAGES",
      "BREAK            TO THE TITLE",
      "",
      "THE GAUGE AT THE TOP RIGHT SHOWS",
      "THE TILT. EACH LEVEL DONE SCORES",
      "100 x THE LEVEL AND 10 FOR EACH",
      "SECOND LEFT." },
};

static void draw_help(void)
{
    uint8_t k;
    hud_clear();
    hud_gauge_hide();
    hud_panel(0, 0, 40, 32);
    hud_big_centre(BIG_BANNER, 12, tl.help == 1 ? "HOW TO PLAY" : "CONTROLS", TX_YELLOW);
    for (k = 0; k < 12; k++) hud_text(1, 6 + k * 2, help_lines[tl.help - 1][k], TX_WHITE);
    hud_text(1, 30, tl.help == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", TX_GREY);
    hud_text(18, 30, "H: NEXT  FIRE: PLAY", TX_YELLOW);
}

static void draw_hiscores(uint8_t row)
{
    uint8_t k;
    hud_panel(8, row, 24, 9);
    hud_ctext(row + 1, "BEST MARBLES", TX_YELLOW);
    for (k = 0; k < HS_ENTRIES; k++) {
        sprintf(tmp, "%u  %s  %6lu", k + 1, hs_table[k].name, hs_table[k].score);
        hud_ctext(row + 3 + k, tmp, k == 0 ? TX_GREEN : TX_WHITE);
    }
}

static void draw_title(uint8_t blink)
{
    hud_big(BIG_TITLE, 48, 12, "MARBLE", TX_YELLOW);
    hud_big(BIG_CLOCK, 96, 48, "TILT", TX_CYAN);
    if (tl.title_page) {
        draw_hiscores(20);
        return;
    }
    hud_panel(5, 25, 30, 7);
    sprintf(tmp, "LEVEL %u: %s", tl.start_level, mt_level_names[tl.start_level - 1]);
    hud_ctext(26, tmp, TX_GREEN);
    hud_ctext(28, "PRESS FIRE TO PLAY", blink ? TX_YELLOW : TX_WHITE);
    sprintf(tmp, "BEST %lu   H: HOW TO PLAY", hs_table[0].score);
    hud_ctext(30, tmp, TX_GREY);
}

static void draw_entry(void)
{
    uint8_t k;
    char c[2] = { 0, 0 };
    hud_clear();
    hud_panel(6, 9, 28, 12);
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
}

/* the play screen's lines that change: redrawn when their values do */
static void draw_play(uint8_t blink)
{
    int16_t secs = (int16_t)(game.time_left / 25);
    uint8_t msg = game.between ? 1 : (tl.state == TS_PLAY && game.level_ticks < 50) ? 2 : tl.paused ? 3 : 0;
    uint8_t clock_ink = game.time_left < 250 && (game.tick & 8) ? TX_RED : TX_WHITE;
    if (hud_dirty) {
        hud_clear();
        shown_secs = shown_clock = shown_falls = -1;
        shown_score = -1;
        shown_gates = shown_auto = shown_msg = 2;
        sprintf(tmp, "%u %s", game.level, mt_level_names[game.level - 1]);
        hud_text(0, 0, tmp, TX_YELLOW);
        if (tl.flash_timer > 0) hud_ctext(7, tl.flash, tl.flash_ink);
    }
    if (secs * 8 + clock_ink != shown_clock) {
        shown_clock = secs * 8 + clock_ink;
        sprintf(tmp, "%d", secs);
        hud_big(BIG_CLOCK, 4, 10, tmp, clock_ink);
    }
    if (game.score != shown_score) {
        shown_score = game.score;
        sprintf(tmp, "SCORE %ld", game.score);
        hud_text(0, 31, tmp, TX_GREY);
    }
    if (game.falls != shown_falls) {
        shown_falls = game.falls;
        if (game.falls) {
            sprintf(tmp, "FALLS %d", game.falls);
            hud_text(15, 31, tmp, TX_GREY);
        }
    }
    if ((uint8_t)game.gates_open != shown_gates) {
        shown_gates = (uint8_t)game.gates_open;
        hud_text(29, 31, game.gates_open ? "          " : "GATES SHUT", TX_RED);
    }
    if (tl.autoplay != shown_auto) {
        shown_auto = tl.autoplay;
        hud_text(29, 30, tl.autoplay ? "AUTO" : "    ", TX_GREEN);
    }
    if (msg != shown_msg) {
        shown_msg = msg;
        tm_fill(0, 15, 40, 2, 0, 0);
        hud_big(BIG_BANNER, 0, 0, "", 0);
        if (msg == 1) {
            hud_big_centre(BIG_BANNER, 90, "THROUGH!", TX_GREEN);
            if (game.level < MT_LEVELS) {
                sprintf(tmp, "NEXT: %s", mt_level_names[game.level]);
                hud_ctext(15, tmp, TX_WHITE);
            }
        } else if (msg == 2) {
            sprintf(tmp, "LEVEL %u: %s", game.level, mt_level_names[game.level - 1]);
            hud_ctext(15, tmp, TX_YELLOW);
        } else if (msg == 3) hud_ctext(16, "PAUSED", TX_WHITE);
    }
    if (tl.state == TS_OVER && (hud_dirty || blink != shown_blink)) {
        shown_blink = blink;
        hud_panel(6, 10, 28, 11);
        hud_big_centre(BIG_BANNER, 88, game.won ? "ALL THROUGH" : "TIME UP", game.won ? TX_GREEN : TX_RED);
        sprintf(tmp, "LEVEL %u  FALLS %d", game.level, game.falls);
        hud_ctext(15, tmp, TX_YELLOW);
        sprintf(tmp, "SCORE %ld", game.score);
        hud_ctext(17, tmp, TX_WHITE);
        if (tl.state_timer == 0) hud_ctext(19, "PRESS FIRE", TX_YELLOW);
    }
}

static void draw_text(void)
{
    uint8_t blink = (uint8_t)((game.tick / 12) & 1);
    if (tl.help) {
        if (hud_dirty) draw_help();
    } else if (tl.state == TS_TITLE) {
        if (hud_dirty) {
            hud_clear();
            shown_blink = 2;
        }
        if (hud_dirty || blink != shown_blink) {
            shown_blink = blink;
            draw_title(blink);
        }
    } else if (tl.state == TS_ENTRY) {
        if (hud_dirty) draw_entry();
    } else draw_play(blink);
    hud_dirty = 0;
}

/* ---- the main loop's pass ---- */
static uint16_t fps_frames, fps_loops, fps_ticks, fps_flips;

void game_frame(uint8_t frames)
{
    uint8_t steps;
    fps_flips += view_flip();                    /* a board that is ready */
    if (tl.help) view_hide_marble();
    else view_marble(&game);
    if (tl.state != TS_ENTRY && !tl.help) hud_gauge(game.roll, game.pitch);
    input();
    if (tl.state == TS_ENTRY) {
        uint8_t r = hs_edit_step(&ed);
        if (r & HS_EDIT_TAKEN) snd_play(SFX_KEY);
        if (r & HS_EDIT_CHANGED) hud_dirty = 1;
        if (r & HS_EDIT_DONE) {
            hs_insert(ed_place, ed.name, (uint32_t)game.score);
            zx_log("mt: hiscore %s %ld place=%d\n", ed.name, game.score, ed_place + 1);
            to_title();
            tl.title_page = 1;
        }
    }
    acc += frames;
    if (acc > 8) acc = 8;                        /* a long stall isn't caught up */
    steps = acc >> 1;
    acc &= 1;
    fps_ticks += steps;
    while (steps--) tick();
    if (!tl.help) view_update(&game);
    draw_text();
    if (!tl.help) view_work();                   /* the rest of the frame: drawing */
    /* the frame rate: passes of the loop and ticks shown, every 2 s */
    fps_loops++;
    fps_frames += frames;
    while (frames--) snd_tick();
    if (fps_frames >= 100) {
        zx_log("mt: perf loops=%u ticks=%u frames=%u flips=%u drawn=%u cached=%u\n", fps_loops, fps_ticks,
               fps_frames, fps_flips, view_draws, view_hits);
        fps_flips = 0;
        fps_frames = fps_loops = fps_ticks = 0;
    }
}
