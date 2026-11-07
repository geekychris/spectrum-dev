/*
 * game.c - Harbour Crane's game shell for the Next (the original's game.c
 * and the text parts of render.c): the title with the autopilot working
 * behind it, play, game over, the help pages, sound, and here the high
 * scores.
 *
 * The rules run at the original's 25 ticks a second whatever the drawing
 * costs: each pass of the main loop runs a tick for every two frames that
 * went by (fixed timestep, at most 4), then draws the picture once.
 *
 * port: keys. The original's A/D, W/S and cursor keys, space and return
 * stay (cursor keys are 5-8 on the Spectrum, 0 fires); P pauses, B is the
 * autopilot, H the help (the original's Help/H), BREAK (its Esc) goes back
 * to the title. A Kempston joystick works too.
 * port: the high-score table and the initials (the original keeps a best
 * score only); a score must be above 0 to enter it.
 */
#include <string.h>
#include <stdio.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "input2.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "sound.h"
#include "mem.h"

static uint32_t next_seed = 1;
static uint8_t acc, prev_keys, hud_dirty = 1, fire_lock;
static unsigned play_in;
static in2_pad pad = {IN2_WASD | IN2_CURSOR | IN2_KEMPSTON1 | IN2_SPACE | IN2_ENTER, 0, 0};
static hs_editor ed;
static int8_t ed_place;
static char tmp[42];

static const char *const state_names[] = { "TITLE", "PLAY", "OVER", "ENTRY" };

static void set_state(uint8_t s)
{
    cr.state = s;
    hud_dirty = 1;
    zx_log("hc: state %s level=%d score=%ld loaded=%d lost=%d\n", state_names[s], game.level, game.score,
           game.total_loaded, game.lost);
}

static void flash(const char *s, uint8_t ink)
{
    strncpy(cr.flash, s, sizeof(cr.flash) - 1);
    cr.flash_timer = 40;
    cr.flash_ink = ink;
    hud_dirty = 1;
}

static void start_game(uint8_t level)
{
    hc_new_game(&game, next_seed++, level);
    cr.paused = 0;
    cr.help = 0;
    cr.autoplay = 0;
    fire_lock = 1;
    snd_music(0);
    set_state(CS_PLAY);
    zx_log("hc: start level %u seed %lu\n", level, game.seed);
}

static void to_title(void)
{
    hc_new_game(&game, 777 + next_seed, 1);
    cr.title_page = 0;
    cr.state_timer = 0;
    snd_music(1);
    set_state(CS_TITLE);
}

/* the original's long-game test (tests/test_cases.c): the autopilot from
   level 1 with seed 1234 for 15,000 ticks; its state hash is the original's
   tests/vectors.h (0x6ADC57B8). Runs at start-up while T is held (test.py) */
static void selftest(void)
{
    int k;
    HCEvent e;
    hc_new_game(&game, 1234, 1);
    for (k = 0; k < 15000 && !game.over && !game.won; k++) {
        hc_tick(&game, hc_bot(&game));
        while (hc_pop_event(&game, &e)) {}
    }
    zx_log("hc: selftest ticks=%d level=%d score=%ld loaded=%d lost=%d dents=%d hash=%08lX\n", k, game.level,
           game.score, game.total_loaded, game.lost, game.dents, hc_state_hash(&game));
}

void game_init(void)
{
    input_init();
    in2_init();
    frame_hook = in2_sample;                    /* presses latched between passes */
    in2_scan();
    if (in2_key(2, 0x10)) selftest();          /* T */
    cr.start_level = 1;
    hs_init(400);
    to_title();
}

/* the original's handle_events(): sounds and messages (quiet behind the title) */
static void events(void)
{
    HCEvent e;
    uint8_t quiet = cr.state == CS_TITLE;
    while (hc_pop_event(&game, &e)) {
        if (e.type == HCE_LOST || (e.type == HCE_SPLASH && e.a == 1)) {
            cr.splash_timer = 20;
            cr.splash_x = (int16_t)(game.fx >> 16) + 24;
        }
        if (quiet) continue;
        switch (e.type) {
        case HCE_GRAB: snd_play(SFX_GRAB); break;
        case HCE_RELEASE: snd_play(SFX_RELEASE); break;
        case HCE_LAND: snd_play(SFX_LAND); break;
        case HCE_DENT: snd_play(SFX_DENT); flash("DENTED!", TX_RED); break;
        case HCE_LOST: snd_play(SFX_LOST); flash("LOST IN THE HARBOUR", TX_RED); break;
        case HCE_BUMP: snd_play(SFX_BUMP); break;
        case HCE_SPLASH: snd_play(SFX_SPLASH); break;
        case HCE_CLEAR: snd_play(SFX_CLEAR); break;
        case HCE_CAPSIZE: flash("SHE'S GOING OVER!", TX_RED); break;
        case HCE_TIMEOUT: flash("TIME UP", TX_RED); break;
        case HCE_OVER: snd_play(SFX_OVER); break;
        case HCE_WIN: snd_play(SFX_WIN); break;
        case HCE_GUST: snd_play(SFX_GUST); flash("GUST!", TX_CYAN); break;
        }
        if (e.type != HCE_BUMP && e.type != HCE_SPLASH && e.type != HCE_GUST)
            zx_log("hc: ev %s a=%d b=%d score=%ld level=%d loaded=%d lost=%d\n", hc_event_names[e.type], e.a, e.b,
                   game.score, game.level, game.loaded, game.lost);
    }
}

/* ---- input (the original's game_input) ---- */
#define K_HELP  1
#define K_AUTO  2
#define K_PAUSE 4
#define K_ESC   8

static void input(void)
{
    uint8_t held, edge, keys = 0, kedge;
    in2_scan_latched();
    in2_update(&pad);
    held = pad.held;
    edge = pad.pressed;
    if (in2_key(6, 0x10)) keys |= K_HELP;              /* H */
    if (in2_key(7, 0x10)) keys |= K_AUTO;              /* B */
    if (in2_key(5, 0x01)) keys |= K_PAUSE;             /* P */
    if (in2_key(0, 0x01) && in2_key(7, 0x01)) {        /* BREAK: CAPS SHIFT + SPACE */
        keys |= K_ESC;
        held &= ~IN_FIRE;
        edge &= ~IN_FIRE;
    }
    kedge = keys & ~prev_keys;
    prev_keys = keys;
    if (cr.state == CS_ENTRY) return;            /* the initials editor reads the keys itself */
    if (kedge & K_HELP) {
        cr.help = (cr.help + 1) % 3;
        hud_dirty = 1;
        zx_log("hc: help %u\n", cr.help);
        return;
    }
    if (cr.help) {
        if ((edge & (IN_FIRE | IN_START)) || (kedge & K_ESC)) { cr.help = 0; hud_dirty = 1; }
        return;
    }
    if ((kedge & K_ESC) && cr.state != CS_TITLE) {
        to_title();
        return;
    }
    play_in = 0;
    switch (cr.state) {
    case CS_TITLE:
        if ((edge & IN_UP) && cr.start_level < HC_LEVELS) { cr.start_level++; hud_dirty = 1; }
        if ((edge & IN_DOWN) && cr.start_level > 1) { cr.start_level--; hud_dirty = 1; }
        if (edge & (IN_UP | IN_DOWN)) zx_log("hc: start_level %u\n", cr.start_level);
        if (edge & (IN_FIRE | IN_START)) start_game(cr.start_level);
        break;
    case CS_PLAY:
        if (kedge & K_PAUSE) {
            cr.paused = !cr.paused;
            hud_dirty = 1;
            zx_log("hc: pause %u\n", cr.paused);
        }
        if (kedge & K_AUTO) {
            cr.autoplay = !cr.autoplay;
            hud_dirty = 1;
            zx_log("hc: autoplay %u\n", cr.autoplay);
        }
        play_in = (held & IN_LEFT ? HC_IN_LEFT : 0) | (held & IN_RIGHT ? HC_IN_RIGHT : 0) |
                  (held & IN_UP ? HC_IN_UP : 0) | (held & IN_DOWN ? HC_IN_DOWN : 0) |
                  ((held & IN_FIRE) && !fire_lock ? HC_IN_FIRE : 0);
        if (!(held & IN_FIRE)) fire_lock = 0;
        break;
    case CS_OVER:
        if (cr.state_timer == 0 && (edge & (IN_FIRE | IN_START))) {
            ed_place = game.score > 0 ? hs_rank((uint32_t)game.score) : -1;
            if (ed_place >= 0) {
                hs_edit_begin(&ed);
                set_state(CS_ENTRY);
            } else to_title();
        }
        break;
    }
}

/* ---- the rules, 25 ticks a second (the original's game_update) ---- */
static uint32_t demo_hash(uint32_t s)                /* port: for the original's a3_hash */
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static void tick(void)
{
    if (cr.help || cr.paused || cr.state == CS_ENTRY) return;
    if (cr.state == CS_TITLE) {
        if (game.over || game.won) hc_new_game(&game, demo_hash(game.seed), 1 + (int)(game.seed % 2));
        hc_tick(&game, hc_bot(&game));
        if (++cr.state_timer >= 200) {           /* port: the title and the high scores in turn */
            cr.state_timer = 0;
            cr.title_page ^= 1;
            hud_dirty = 1;
        }
    } else if (cr.state == CS_PLAY) {
        hc_tick(&game, cr.autoplay ? hc_bot(&game) : play_in);
    }
    events();
    if (cr.flash_timer > 0 && --cr.flash_timer == 0) hud_dirty = 1;
    if (cr.splash_timer > 0) cr.splash_timer--;
    if (cr.state == CS_OVER && cr.state_timer > 0 && --cr.state_timer == 0) hud_dirty = 1;
    if (cr.state == CS_PLAY && (game.over || game.won)) {
        cr.state_timer = 40;
        snd_music(0);
        set_state(CS_OVER);
    }
}

/* ---- the text (render.c's hud(), render_game(), render_title(), render_help()) ---- */
static const char *const help_lines[2][12] = {
    { "LIFT EACH CONTAINER OFF THE QUAY AND",
      "LAND IT ON THE SHIP. WHAT HANGS ON THE",
      "HOOK SWINGS: START AND STOP SMOOTHLY,",
      "AND LET THE SWING DIE BEFORE YOU LAND.",
      "LET GO LOW: DROPPED FROM HIGH IT DENTS.",
      "LAND SQUARE IN A SLOT; LONG ONES NEED",
      "TWO STACKS THE SAME HEIGHT. FOUR HIGH.",
      "",
      "KEEP HER LEVEL: HEAVY BOXES ON ONE SIDE",
      "LIST THE SHIP, HIGH STACKS ROLL HER",
      "MORE. TOO FAR AND SHE CAPSIZES.",
      "GREEN 1T  BLUE 2T  ORANGE 3T  RED 4T" },
    { "A D / 5 8 / JOY  MOVE THE TROLLEY",
      "W S / 7 6 / JOY  WINCH IN / OUT",
      "SPACE / 0 / FIRE GRAB / LET GO",
      "P                PAUSE",
      "B                AUTOPILOT",
      "H                THESE PAGES",
      "BREAK            TO THE TITLE",
      "",
      "50 x WEIGHT A CONTAINER, -40 DENTED,",
      "-100 LOST (3 LOST ENDS THE GAME).",
      "BONUS FOR TIME LEFT, AND 200 FOR",
      "SENDING HER OFF ON AN EVEN KEEL." },
};

static void draw_help(void)
{
    uint8_t k;
    hud_clear();
    hud_gauge_hide();
    hud_panel(0, 0, 40, 32, TX_GREY);
    hud_on_panel = 1;
    hud_big_begin();
    hud_big(2, cr.help == 1 ? "HOW TO PLAY" : "CONTROLS", TX_YELLOW);
    for (k = 0; k < 12; k++) hud_text(1, 6 + k * 2, help_lines[cr.help - 1][k], TX_WHITE);
    hud_text(1, 30, cr.help == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", TX_GREY);
    hud_text(17, 30, "H: NEXT  FIRE: PLAY", TX_YELLOW);
    hud_on_panel = 0;
}

static void draw_hiscores(uint8_t row)
{
    uint8_t k;
    hud_panel(8, row, 24, 9, TX_GREY);
    hud_on_panel = 1;
    hud_ctext(row + 1, "BEST CRANE DRIVERS", TX_YELLOW);
    for (k = 0; k < HS_ENTRIES; k++) {
        sprintf(tmp, "%u  %s  %6lu", k + 1, hs_table[k].name, hs_table[k].score);
        hud_ctext(row + 3 + k, tmp, k == 0 ? TX_GREEN : TX_WHITE);
    }
    hud_on_panel = 0;
}

static void draw_title(uint8_t blink)
{
    hud_clear();
    hud_gauge_hide();
    hud_big_begin();
    hud_big(5, "HARBOUR", TX_YELLOW);
    hud_big(8, "CRANE", TX_CRANE);
    if (cr.title_page) {
        draw_hiscores(20);
        return;
    }
    hud_panel(5, 24, 30, 7, TX_GREY);
    hud_on_panel = 1;
    sprintf(tmp, "START AT LEVEL %u (UP/DOWN)", cr.start_level);
    hud_ctext(25, tmp, TX_GREEN);
    hud_ctext(27, "PRESS FIRE TO PLAY", blink ? TX_YELLOW : TX_WHITE);
    sprintf(tmp, "BEST %lu   H: HOW TO PLAY", hs_table[0].score);
    hud_ctext(29, tmp, TX_GREY);
    hud_on_panel = 0;
}

static void draw_entry(void)
{
    uint8_t k;
    char c[2] = { 0, 0 };
    hud_clear();
    hud_gauge_hide();
    hud_panel(6, 9, 28, 12, TX_GREY);
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

/* what the play screen shows now (lines redrawn when their values change) */
static int32_t shown_score = -1;
static int16_t shown_time = -1, shown_loaded = -1, shown_lost = -1;
static uint8_t shown_auto = 2, shown_msg = 0xFF, shown_blink = 2, shown_level;

static void draw_play(uint8_t blink)
{
    int16_t secs = (int16_t)(game.time_left / 25);
    uint8_t clock_ink = game.time_left < 750 && (game.tick & 8) ? TX_RED : TX_WHITE;
    uint8_t msg = game.between ? 1 : (cr.state == CS_PLAY && game.tick < 75) ? 2 : cr.paused == 1 ? 3 : 0;
    int16_t k;
    if (hud_dirty) {
        hud_clear();
        hud_gauge_bar();
        hud_text(0, 29, "LIST", TX_GREY);
        shown_score = -1;
        shown_time = shown_loaded = shown_lost = -1;
        shown_auto = shown_msg = 2;
        shown_level = 0;
        if (cr.flash_timer > 0) hud_ctext(5, cr.flash, cr.flash_ink);
    }
    /* the top line: SCORE, LEVEL, loaded/containers, lost, the clock */
    if (game.score != shown_score) {
        shown_score = game.score;
        sprintf(tmp, "SCORE %-6ld", game.score);
        hud_text(0, 1, tmp, TX_WHITE);
    }
    if (game.level != shown_level) {
        shown_level = (uint8_t)game.level;
        sprintf(tmp, "LEVEL %d", game.level);
        hud_text(14, 1, tmp, TX_YELLOW);
    }
    if (game.loaded * 16 + game.nbox != shown_loaded) {
        shown_loaded = game.loaded * 16 + game.nbox;
        sprintf(tmp, "%d/%d ", game.loaded, game.nbox);
        hud_text(23, 1, tmp, TX_GREEN);
    }
    if (game.lost != shown_lost) {
        shown_lost = game.lost;
        for (k = 0; k < HC_MAX_LOST; k++) hud_text(29 + k, 1, k < game.lost ? "X" : " ", TX_RED);
    }
    if (secs * 2 + (clock_ink == TX_RED) != shown_time) {
        shown_time = secs * 2 + (clock_ink == TX_RED);
        sprintf(tmp, "%d:%02d", secs / 60, secs % 60);
        hud_text(34, 1, tmp, clock_ink);
    }
    if (cr.autoplay != shown_auto) {
        shown_auto = cr.autoplay;
        hud_text(34, 29, cr.autoplay ? "AUTO" : "    ", TX_GREEN);
    }
    if (game.wind + game.gust) hud_text(20, 29, "WIND", TX_GREY);
    else hud_text(20, 29, "    ", TX_GREY);
    if (msg != shown_msg) {
        shown_msg = msg;
        tm_fill(0, 8, 40, 6, 0, 0);
        hud_big_begin();
        if (msg == 1) {
            hud_big(8, "SHIP LOADED", TX_GREEN);
            if (game.level < HC_LEVELS) {
                sprintf(tmp, "LEVEL %d NEXT", game.level + 1);
                hud_ctext(12, tmp, TX_WHITE);
            }
        } else if (msg == 2) {
            sprintf(tmp, "LEVEL %d: LOAD %d CONTAINERS", game.level, game.nbox);
            hud_ctext(9, tmp, TX_YELLOW);
        } else if (msg == 3) hud_ctext(13, "PAUSED", TX_WHITE);
    }
    if (cr.state == CS_OVER && (hud_dirty || blink != shown_blink)) {
        const char *why = game.won ? "ALL SHIPS LOADED" : game.capsized ? "SHE CAPSIZED" :
                          game.lost >= HC_MAX_LOST ? "THREE LOST" : "TIME UP";
        shown_blink = blink;
        hud_panel(6, 9, 28, 10, game.won ? TX_GREEN : TX_RED);
        hud_on_panel = 1;
        hud_big_begin();
        hud_big(10, game.won ? "YOU WIN" : "GAME OVER", game.won ? TX_GREEN : TX_RED);
        hud_ctext(13, why, TX_YELLOW);
        sprintf(tmp, "SCORE %ld", game.score);
        hud_ctext(15, tmp, TX_WHITE);
        if (cr.state_timer == 0) hud_ctext(17, "PRESS FIRE", TX_YELLOW);
        hud_on_panel = 0;
    }
}

static void draw_text(void)
{
    uint8_t blink = (uint8_t)((game.tick / 12) & 1);
    if (cr.help) {
        if (hud_dirty) draw_help();
    } else if (cr.state == CS_TITLE) {
        if (hud_dirty || blink != shown_blink) {
            shown_blink = blink;
            draw_title(blink);
        }
    } else if (cr.state == CS_ENTRY) {
        if (hud_dirty) draw_entry();
    } else {
        draw_play(blink);
        hud_gauge(game.heel, game.list, (int16_t)((game.wind + game.gust) / 64));
    }
    hud_dirty = 0;
}

/* ---- the state the test's bot reads (test.py) ---- */
uint8_t hc_probe[25];                    /* see probe() and tools/bot.py */

static void probe(void)
{
    uint8_t *p = hc_probe;
    int16_t v;
    *p++ = cr.state;
    v = (int16_t)(game.tx >> 16); *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8);
    v = (int16_t)(hc_hook_x(&game) >> 16); *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8);
    v = (int16_t)(hc_hook_y(&game) >> 16); *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8);
    *p++ = (uint8_t)game.carrying;
    *p++ = (uint8_t)game.falling;
    *p++ = (uint8_t)game.next;
    *p++ = (uint8_t)game.nbox;
    *p++ = (uint8_t)game.loaded;
    *p++ = (uint8_t)game.lost;
    *p++ = (uint8_t)game.level;
    *p++ = (uint8_t)game.over;
    *p++ = (uint8_t)game.won;
    *p++ = cr.autoplay;
    *p++ = (uint8_t)game.score; *p++ = (uint8_t)(game.score >> 8);
    *p++ = (uint8_t)(game.score >> 16); *p++ = (uint8_t)(game.score >> 24);
    v = (int16_t)(game.tv >> 8); *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8);   /* the trolley's speed, 1/256 */
    *p++ = (uint8_t)game.between;
    *p = cr.start_level;
}

/* ---- the main loop's pass ---- */
static uint16_t fps_frames, fps_draws, fps_ticks;
static uint8_t polys;

void game_frame(uint8_t frames)
{
    uint8_t steps;
    input();
    if (cr.state == CS_ENTRY) {
        uint8_t r;
        input_read();                            /* the SDK's editor reads its own keys */
        r = hs_edit_step(&ed);
        if (r & HS_EDIT_TAKEN) snd_play(SFX_GRAB);
        if (r & HS_EDIT_CHANGED) hud_dirty = 1;
        if (r & HS_EDIT_DONE) {
            hs_insert(ed_place, ed.name, (uint32_t)game.score);
            zx_log("hc: hiscore %s %ld place=%d\n", ed.name, game.score, ed_place + 1);
            to_title();
            cr.title_page = 1;
        }
    }
    acc += frames;
    if (acc > 8) acc = 8;                        /* a long stall isn't caught up */
    steps = acc >> 1;
    acc &= 1;
    fps_ticks += steps;
    while (steps--) tick();
    probe();
    draw_text();
    if (!cr.help) {
        VIEW();
        view_draw(&game, cr.splash_timer, cr.splash_x);
        polys = view_polys;
        RULES();
        fps_draws++;
    }
    fps_frames += frames;
    while (frames--) snd_tick();
    if (fps_frames >= 100) {
        zx_log("hc: perf frames=%u draws=%u ticks=%u polys=%u ay=%u,%u,%u,%u\n", fps_frames, fps_draws, fps_ticks,
               polys, snd_reg(0, 7), snd_reg(0, 8) & 15, snd_reg(1, 7), snd_reg(1, 8) & 15);
        fps_frames = fps_draws = fps_ticks = 0;
    }
}
