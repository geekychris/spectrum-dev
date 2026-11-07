/*
 * game.c - Drone Courier's shell (the original's game.c and the text of
 * its render.c) on the Next: input, the title with the autopilot flying
 * behind it, play, game over, the help pages, sound for the events, and
 * the HUD. The rules are the original's (dc_core.c, in bank 20); the 3D
 * view is view.c; the text and gauges hud.c.
 *
 * port: the bridge's variables and hooks are the log (zx_log) and a probe
 * struct test.py reads; the high-score table and the initials (the SDK's)
 * are new: the original keeps a best score only.
 */
#include <string.h>
#include <stdio.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "dc.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "sound.h"

Courier co;
DCGame game;
GameProbe dc_probe;                     /* test.py reads it by symbol */
static uint32_t next_seed = 1;
static uint8_t fire_lock, hum_timer, last_state = 255;
uint8_t screen;                  /* what the tilemap is laid out for */
static uint16_t title_timer;
static uint8_t title_page = 255;        /* the page the tilemap shows (255: none yet) */

/* the original's pens -> hud.c inks */
#define K_RED    TX_RED                /* 26 */
#define K_YELLOW TX_YELLOW             /* 27 */
#define K_GREEN  TX_GREEN              /* 28 */
#define K_CYAN   TX_CYAN               /* 29 */

static void flash(const char *s, uint8_t ink)
{
    size_t n = strlen(s);
    if (n > sizeof(co.flash) - 1) n = sizeof(co.flash) - 1;
    memcpy(co.flash, s, n);
    co.flash[n] = 0;
    co.flash_timer = 40;
    co.flash_pen = ink;
}

/* ------------------------------------------------------------ events */

static const char *const ev_names[DCE_COUNT] = {
    "NONE", "PICKUP", "DELIVER", "BUMP", "BROKEN", "HARD", "LATE", "GUST", "FLAT", "DAY", "OVER", "WIN"
};

static void handle_events(void)
{
    DCEvent e;
    uint8_t quiet = co.state == CS_TITLE;
    while (dc_pop_event(&game, &e)) {
        int t = e.type;
        if (quiet) continue;
        switch (t) {
        case DCE_PICKUP: snd_play(SFX_PICKUP); flash(e.a ? "FRAGILE PARCEL - GENTLY!" : "PARCEL ABOARD", e.a ? K_RED : K_CYAN); break;
        case DCE_DELIVER: snd_play(SFX_DELIVER); flash("DELIVERED", K_GREEN); break;
        case DCE_BUMP: snd_play(SFX_BUMP); break;
        case DCE_HARD: snd_play(SFX_BUMP); flash("TOO HARD - TRY AGAIN", K_YELLOW); break;
        case DCE_BROKEN: snd_play(SFX_BROKEN); flash("PARCEL BROKEN", K_RED); break;
        case DCE_LATE: snd_play(SFX_LATE); flash("TOO LATE", K_RED); break;
        case DCE_GUST: snd_play(SFX_GUST); break;
        case DCE_FLAT: flash("BATTERY FLAT", K_RED); break;
        case DCE_DAY: snd_play(SFX_DAY); break;
        case DCE_OVER: snd_play(SFX_OVER); break;
        case DCE_WIN: snd_play(SFX_WIN); break;
        }
        if (t != DCE_BUMP && t != DCE_GUST)
            zx_log("dc: ev %s a=%d b=%d score=%ld day=%d done=%d\n", ev_names[t < DCE_COUNT ? t : 0], e.a, e.b,
                   (long)game.score, game.day, game.jobs_done);
    }
}

/* ------------------------------------------------------------ game flow */

static void start_game(uint8_t day)
{
    dc_new_game(&game, next_seed++, day);
    co.state = CS_PLAY;
    co.paused = 0;
    co.help = 0;
    co.autoplay = 0;
    co.cam_yaw = (uint16_t)game.yaw;
    fire_lock = 1;
    zx_log("dc: start day=%d seed=%ld jobs=%d\n", (int)day, (long)game.seed, game.jobs);
}

static void to_title(void)
{
    co.state = CS_TITLE;
    dc_new_game(&game, 777 + next_seed, 1);
    co.cam_yaw = (uint16_t)game.yaw;
    title_timer = 0;
}

/* ------------------------------------------------------------ the HUD */

/* text that changes: rewritten only when it does (slots of a cache) */
enum { L_DAY, L_FAILS, L_SCORE, L_AUTO, L_DIST, L_TASK, L_ALT, L_WIND, L_FLASH, L_PAUSE, L_DAYS1, L_DAYS2,
       L_OVER1, L_OVER2, L_OVER3, L_SLOTS };
static char shown[L_SLOTS][34];
static uint8_t shown_ink[L_SLOTS], shown_col[L_SLOTS], shown_row[L_SLOTS];

static void put(uint8_t slot, uint8_t col, uint8_t row, const char *s, uint8_t ink)
{
    uint8_t old = (uint8_t)strlen(shown[slot]), n = (uint8_t)strlen(s);
    if (col == 255) col = 20 - (n + 1) / 2;          /* centred */
    if (!strcmp(shown[slot], s) && shown_ink[slot] == ink && shown_col[slot] == col && shown_row[slot] == row) return;
    if (old) hud_blank(shown_col[slot], shown_row[slot], old);
    if (n) hud_text(col, row, s, ink);
    memcpy(shown[slot], s, n + 1);
    shown_ink[slot] = ink;
    shown_col[slot] = col;
    shown_row[slot] = row;
}

static void hud_forget(void);

static void forget(void)
{
    uint8_t k;
    hud_forget();
    for (k = 0; k < L_SLOTS; k++) shown[k][0] = 0;
}

void layout(uint8_t scr)
{
    if (scr == screen) return;
    hud_clear();
    forget();
    hud_on_panel = 0;
    screen = scr;
    title_page = 255;
    if (scr == SCR_PLAY) {
        hud_text(0, 30, "BATT", TX_GREY);
    }
}

char tmp[40];

/* render.c hud(): at the original's places (8-pixel rows here) */
/* port: each item is redone only when what it shows changes (the HUD is
   on the tilemap and sprites, which keep it) */
static int32_t last_score, last_clock, last_alt;
static uint8_t last_day, last_done, last_jobs, last_fails, last_batt, last_clock_ink, hud_turn;

static void hud_forget(void)
{
    last_score = last_clock = last_alt = -1;
    last_day = last_fails = last_batt = last_clock_ink = 255;
}

/* render.c hud(): at the original's places (8-pixel rows here) */
static void hud_play(const DCGame *g)
{
    int b = dc_goal(g);
    fix dx = dc_block_x(b % DC_N) - g->x, dz = dc_block_z(b / DC_N) - g->z;
    uint8_t carry = g->carrying != 0, w, ink;
    int32_t v;
    if (g->day != last_day || g->jobs_done != last_done || g->jobs != last_jobs) {
        sprintf(tmp, "DAY %d  %d/%d", g->day, g->jobs_done, g->jobs);
        put(L_DAY, 0, 0, tmp, K_YELLOW);
        last_day = (uint8_t)g->day;
        last_done = (uint8_t)g->jobs_done;
        last_jobs = (uint8_t)g->jobs;
    }
    if (g->fails != last_fails) {
        tmp[0] = 0;
        for (w = 0; w < (uint8_t)g->fails && w < 8; w++) strcat(tmp, "X");
        put(L_FAILS, 0, 1, tmp, K_RED);
        last_fails = (uint8_t)g->fails;
    }
    if (g->score != last_score) {
        sprintf(tmp, "%ld", (long)g->score);
        put(L_SCORE, 39 - (uint8_t)strlen(tmp), 0, tmp, TX_GREY);
        last_score = g->score;
    }
    put(L_AUTO, 35, 2, co.autoplay ? "AUTO" : "", K_GREEN);
    /* the big clock: red in the last 10 seconds, flashing */
    v = g->job_time / 25;
    ink = g->job_time < 250 && (g->tick & 8) ? TX_RED : TX_WHITE;
    if (v != last_clock || ink != last_clock_ink) {
        sprintf(tmp, "%ld", (long)v);
        hud_big(BIG_CLOCK, 4, 26, tmp, ink, 2);
        last_clock = v;
        last_clock_ink = ink;
    }
    /* the arrow to the next pad, turned with the view; its distance every
       other picture */
    hud_arrow((uint16_t)(a3_atan2(dx >> 8, dz >> 8) - co.cam_yaw), carry ? TX_CUST : TX_DEPOT);
    if (++hud_turn & 1) {
        sprintf(tmp, "%ld", (long)(fhypot(dx, dz) >> 16));
        put(L_DIST, 255, 5, tmp, TX_WHITE);
    }
    put(L_TASK, 15, 6, carry ? (g->fragile ? "FRAGILE: DELIVER" : "DELIVER") : "COLLECT", carry ? TX_CUST : TX_DEPOT);
    /* the battery, the height, the wind */
    w = (uint8_t)((g->battery >> 16) * 80 / 100);
    if (w != last_batt) {
        hud_battery(w);
        last_batt = w;
    }
    if ((g->y >> 16) != last_alt) {
        last_alt = g->y >> 16;
        sprintf(tmp, "ALT %ld", (long)last_alt);
        put(L_ALT, 17, 30, tmp, TX_GREY);
    }
    if (g->wind_x || g->wind_z || g->gust_x) {
        put(L_WIND, 28, 30, "WIND", TX_GREY);
        hud_wind((int16_t)((g->wind_x + g->gust_x) >> 9), (int16_t)((g->wind_z + g->gust_z) >> 9));
    } else {
        put(L_WIND, 28, 30, "", TX_GREY);
        hud_wind_hide();
    }
}

/* render.c render_game(): the day's end, game over, paused, the messages */
static void hud_game(const DCGame *g)
{
    hud_play(g);
    if (g->between) {
        hud_big(BIG_BANNER, 80, 80, "DAY'S DONE", TX_GREEN, 2);
        if (g->day < DC_DAYS) sprintf(tmp, "DAY %d NEXT", g->day + 1);
        else tmp[0] = 0;
        put(L_DAYS1, 255, 13, tmp, TX_WHITE);
    } else if (co.state != CS_OVER) {
        hud_big(BIG_BANNER, 0, 0, "", 0, 2);
        put(L_DAYS1, 255, 13, "", TX_WHITE);
    }
    if (co.state == CS_OVER) {
        if (!shown[L_OVER2][0]) {
            hud_panel(6, 10, 28, 10);
            hud_on_panel = 1;
        }
        hud_big(BIG_BANNER, g->won ? 72 : 88, 88, g->won ? "WEEK'S DONE" : "GAME OVER", g->won ? TX_GREEN : TX_RED, 2);
        sprintf(tmp, "DAY %d  %d FAILED", g->day, g->fails);
        put(L_OVER1, 255, 14, g->battery == 0 ? "THE BATTERY WENT FLAT" : tmp, K_YELLOW);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        put(L_OVER2, 255, 16, tmp, TX_WHITE);
        put(L_OVER3, 255, 18, co.state_timer == 0 ? "PRESS FIRE" : "", K_YELLOW);
        hud_on_panel = 0;
    }
    put(L_PAUSE, 255, 22, co.paused == 1 ? "PAUSED" : "", TX_WHITE);
    put(L_FLASH, 255, 9, co.flash_timer > 0 ? co.flash : "", co.flash_pen);
}

/* render.c render_title(): the big title over the autopilot's flight, the
   panel; port: in turn with the high scores */
static void hud_title(const DCGame *g)
{
    uint8_t page = (title_timer / 250) & 1, k;
    if (page != title_page) {
        title_page = page;
        hud_big(BIG_TITLE1, 64, 24, "DRONE", TX_YELLOW, 4);
        hud_big(BIG_TITLE2, 32, 60, "COURIER", TX_CUST, 4);
        hud_panel(5, 24, 30, 8);
        forget();
        hud_on_panel = 1;
        if (page) {
            hud_ctext(25, "BEST COURIERS", K_YELLOW);
            for (k = 0; k < HS_ENTRIES; k++) {
                sprintf(tmp, "%d. %s  %6ld", k + 1, hs_table[k].name, (long)hs_table[k].score);
                hud_ctext(26 + k, tmp, TX_WHITE);
            }
        }
        hud_on_panel = 0;
    }
    if (!page) {
        hud_on_panel = 1;
        sprintf(tmp, "START ON DAY %d (UP/DOWN)", co.start_day);
        put(L_DAY, 255, 25, tmp, K_GREEN);
        put(L_TASK, 255, 27, "PRESS FIRE TO FLY", (g->tick / 12) & 1 ? K_YELLOW : TX_WHITE);
        sprintf(tmp, "BEST %ld   H: HOW TO PLAY", (long)hs_table[0].score);
        put(L_SCORE, 255, 29, tmp, TX_GREY);
        hud_on_panel = 0;
    }
}

/* ------------------------------------------------------------ input */

static uint8_t in_now, in_prev;
#define K_PAUSE 0x01
#define K_AUTO  0x02
#define K_HELP  0x04
#define K_ESC   0x08
static uint8_t keys_now, keys_prev;
static uint8_t play_in;

/* game_input(): edges for the menus, held keys for flying */
static void game_input(void)
{
    uint8_t in = input_read(), edge, kedge;
    uint8_t pressed = input_pressed();
    char letter = input_letter_pressed();    /* latched: a tap during a slow frame still counts */
    keys_now = (in & IN_START ? K_PAUSE : 0) | (in & IN_QUIT ? K_ESC : 0);
    kedge = (keys_now & ~keys_prev) | (pressed & IN_START ? K_PAUSE : 0) | (letter == 'B' ? K_AUTO : 0) |
            (letter == 'H' ? K_HELP : 0);
    keys_prev = keys_now;
    edge = (in & ~in_prev) | pressed;
    in_prev = in;
    in_now = in;
    if (kedge & K_HELP) { co.help = (co.help + 1) % 3; return; }
    if (co.help) {
        if ((edge & IN_FIRE) || (kedge & K_ESC)) co.help = 0;
        return;
    }
    if (kedge & K_ESC) {
        if (co.state != CS_TITLE) to_title();
        return;
    }
    play_in = 0;
    switch (co.state) {
    case CS_TITLE:
        if ((edge & IN_UP) && co.start_day < DC_DAYS) co.start_day++;
        if ((edge & IN_DOWN) && co.start_day > 1) co.start_day--;
        if ((edge & IN_FIRE) || (kedge & K_PAUSE)) start_game(co.start_day);
        break;
    case CS_PLAY:
        if (kedge & K_PAUSE) co.paused = !co.paused;
        if (kedge & K_AUTO) co.autoplay = !co.autoplay;
        play_in = (in & IN_LEFT ? DC_IN_LEFT : 0) | (in & IN_RIGHT ? DC_IN_RIGHT : 0) | (in & IN_UP ? DC_IN_UP : 0) |
                  (in & IN_DOWN ? DC_IN_DOWN : 0) | ((in & IN_FIRE) && !fire_lock ? DC_IN_CLIMB : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case CS_OVER:
        if (co.state_timer == 0 && ((edge & IN_FIRE) || (kedge & K_PAUSE))) co.state = CS_DONE;
        break;
    }
}

/* ------------------------------------------------------------ the loop */

static const char *const state_name[4] = {"TITLE", "FLY", "OVER", "DONE"};
static uint16_t raster(void) { return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F); }

static void probe(void)
{
    dc_probe.state = co.state;
    dc_probe.day = (uint8_t)game.day;
    dc_probe.jobs = (uint8_t)game.jobs;
    dc_probe.jobs_done = (uint8_t)game.jobs_done;
    dc_probe.fails = (uint8_t)game.fails;
    dc_probe.carrying = (uint8_t)game.carrying;
    dc_probe.landed = (uint8_t)game.landed;
    dc_probe.autoplay = co.autoplay;
    dc_probe.help = co.help;
    dc_probe.paused = co.paused;
    dc_probe.between = (uint8_t)game.between;
    dc_probe.over = (uint8_t)(game.over | (game.won << 1));
    dc_probe.score = game.score;
    dc_probe.job_time = game.job_time;
    dc_probe.tick = game.tick;
    dc_probe.x = (int16_t)(game.x >> 16);
    dc_probe.y = (int16_t)(game.y >> 16);
    dc_probe.z = (int16_t)(game.z >> 16);
    dc_probe.battery = (uint8_t)(game.battery >> 16);
    dc_probe.goal = (uint8_t)dc_goal(&game);
}

void game_main(void)
{
    uint16_t perf_bot = 0, perf_logic = 0, perf_hud = 0, perf_frames = 0, perf_draws = 0, perf_ticks = 0, perf_lines = 0, perf_polys = 0, perf_towers = 0;
    uint8_t frames_left = 0;
    int32_t last_score = -1;
    co.start_day = 1;
    to_title();
    zx_log("dc: ready kempston=%u\n", (unsigned)input_kempston);
    for (;;) {
        uint8_t frames = frame_wait(), ticks, k;
        uint16_t t0 = raster(), t1;
        clk_t c1;
        l2_flip();                       /* the picture drawn last time (in the vertical blank) */
        if (co.state == CS_OVER) hud_drone_hide();   /* (the box is over it, as on the original) */
        else hud_drone();                /* and its drone */
        perf_frames += frames;
        for (k = 0; k < frames; k++) snd_tick();
        game_input();
        /* the original's game_update(): 25 ticks a second, at most 4 at once */
        frames_left += frames;
        ticks = frames_left / 2;
        frames_left &= 1;
        if (ticks > 4) ticks = 4;
        clk_mark(&c1);
        while (ticks--) {
            if (co.help || co.paused) continue;
            if (co.state == CS_TITLE) {
                if (game.over || game.won || game.between == 1) dc_new_game(&game, game.seed + 1, 1);
                dc_tick(&game, dc_bot(&game));
                title_timer++;
            } else if (co.state == CS_PLAY) {
                if (co.autoplay) {
                    clk_t cb;
                    uint8_t bin;
                    clk_mark(&cb);
                    bin = (uint8_t)dc_bot(&game);
                    perf_bot += clk_lines(&cb);
                    dc_tick(&game, bin);
                } else dc_tick(&game, play_in);
                if (!game.landed && --hum_timer == 0) { snd_play(SFX_HUM); hum_timer = 8; }
            }
            handle_events();
            co.cam_yaw = (uint16_t)(co.cam_yaw + (int16_t)((int16_t)((uint16_t)game.yaw - co.cam_yaw) / 5));
            if (co.flash_timer > 0) co.flash_timer--;
            if (co.state_timer > 0) co.state_timer--;
            if (co.state == CS_PLAY && (game.over || game.won)) {
                co.state = CS_OVER;
                co.state_timer = 40;
            }
            perf_ticks++;
        }
        perf_logic += clk_lines(&c1);
        if (co.state == CS_DONE) {
            /* port: the high-score entry, then the title */
            snd_music(0);
            initials();
            to_title();
        }
        if (co.state != last_state) {
            zx_log("dc: state=%s score=%ld day=%d done=%d fails=%d\n", state_name[co.state], (long)game.score,
                   game.day, game.jobs_done, game.fails);
            last_state = co.state;
            snd_music(co.state == CS_TITLE ? 1 : co.state == CS_PLAY ? 2 : 0);
        }
        /* the picture */
        clk_mark(&c1);
        if (co.help) {
            hud_help((uint8_t)co.help);
            view_clear();
        } else if (co.state == CS_TITLE) {
            layout(SCR_TITLE);
            view_draw(&game, co.cam_yaw);
            hud_title(&game);
        } else {
            layout(SCR_PLAY);
            view_draw(&game, co.cam_yaw);
            hud_game(&game);
        }
        perf_draws++;
        perf_hud += clk_lines(&c1);
        perf_polys += view_polys;
        perf_towers += view_towers;
        if (game.score != last_score && co.state != CS_TITLE) {
            zx_log("dc: score=%ld\n", (long)game.score);
            last_score = game.score;
        }
        probe();
        t1 = raster();
        perf_lines += t1 >= t0 ? t1 - t0 : t1 + 312 - t0;
        if (perf_frames >= 100) {
            zx_log("dc: perf draws=%u frames=%u ticks=%u polys=%u towers=%u state=%s\n", perf_draws, perf_frames,
                   perf_ticks, perf_polys / perf_draws, perf_towers / perf_draws, state_name[co.state]);
            zx_log("dc: lines logic=%u build=%u fill=%u hud=%u bot=%u\n", perf_logic / perf_draws, view_t_build / perf_draws,
                   view_t_fill / perf_draws, (perf_hud - view_t_build - view_t_fill) / perf_draws, perf_bot / perf_draws);
            zx_log("dc: ay mix0=%u vol0=%u mix2=%u vol2a=%u vol2c=%u\n", (unsigned)snd_reg(0, 7), (unsigned)snd_reg(0, 8),
                   (unsigned)snd_reg(2, 7), (unsigned)snd_reg(2, 8), (unsigned)snd_reg(2, 10));
            perf_frames = perf_draws = perf_ticks = perf_lines = perf_polys = perf_towers = 0;
            perf_logic = perf_hud = view_t_build = view_t_fill = perf_bot = 0;
        }
    }
}
