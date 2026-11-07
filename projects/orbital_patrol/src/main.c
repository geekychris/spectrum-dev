/*
 * Orbital Patrol for the Spectrum Next - port of amiga_mcp/examples/orbital_patrol.
 *
 * game.c is the original's game logic and flow.c its main loop's state
 * machine (both checked against the original built on the host, see
 * test.py); view.c draws, sound.c plays. This file is the original main.c's
 * front end: the title, the 50 Hz loop, game over, the high scores.
 *
 * Memory:
 *   0x0000-0x1FFF  page 46: the game's state (state.c), mapped once the
 *                  IM 2 interrupts are on (nothing calls the ROM after that)
 *   0x2000-0x3FFF  the ROM, and Layer 2's pages while view.c draws (its
 *                  font copied to RAM first)
 *   0x4000-0x48FF  the stars' tilemap (bank 5: the ULA is off)
 *   0x4A00-0xEFFF  this program
 *   0xF000-0xF1F3  the SDK's IM 2 table; 0xF200-0xFDFF the music (music.c,
 *                  loaded into page 44 by the .nex, copied up at start-up);
 *                  the stack below 0xFF58
 * Layer 2 is in banks 16-20 and the sprite patterns in bank 21.
 *
 * Each frame: the frame interrupt (the keys, the music and effects), then
 * view_commit() shows what was worked out last frame (sprites, scroll)
 * while the beam is in the border; the logic runs once per frame that
 * passed (fixed timestep, at most 4), then view_prepare().
 *
 * Controls (port: the original has arrows/WASD, SPACE, Z, H/X, RETURN):
 *   Q/A/O/P, Sinclair 1/2, Kempston   up, down, left (turn + thrust), right
 *   SPACE, M or fire                  shoot
 *   Z                                 smart bomb
 *   X or H                            hyperspace
 *   ENTER or fire                     start
 */
#include <stdint.h>
#include <string.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "flow.h"
#include "view.h"
#include "sound.h"

extern GameState gs;                     /* state.c: page 46 at 0x0000 */
static WORD startup_delay = 20;          /* the original: input ignored at first */
static uint8_t hyper_was;

/* the original's input bits from the SDK's (bomb and hyperspace on a new press) */
static WORD read_input(void)
{
    uint8_t b = input_read(), p = input_pressed(), h;
    WORD in = 0;
    if (b & IN_LEFT) in |= INPUT_LEFT;
    if (b & IN_RIGHT) in |= INPUT_RIGHT;
    if (b & IN_UP) in |= INPUT_UP;
    if (b & IN_DOWN) in |= INPUT_DOWN;
    if (b & IN_FIRE) in |= INPUT_FIRE | INPUT_START;     /* SPACE: fire and start, as the original */
    if (b & IN_START) in |= INPUT_START;
    if (p & IN_FIRE2) in |= INPUT_BOMB;
    h = key_down(IN_KEY_SCANCODE_x) | key_down(IN_KEY_SCANCODE_h);
    if (h && !hyper_was) in |= INPUT_HYPER;
    hyper_was = h;
    if (startup_delay > 0) {
        startup_delay--;
        in = 0;
    }
    return in;
}

static void isr_work(void)
{
    input_sample();
    sound_frame();
}

/* ------------------------------------------------------------ test hooks (test.py) */

/* every tick of a game: its input is recorded (when it changes: tick, input,
   two 16-bit words) in page 47 (8K, 2048 changes), and every 25 ticks the
   state is logged; tools/ref.c plays the same input through the original
   code on the host and must log the same */
#define TAPE_PAGE 47
static uint16_t tick, tape_n;
static WORD tape_last = -1;

static void tape_tick(WORD in)
{
    if (in != tape_last && tape_n < 2048) {
        uint16_t *t;
        ZXN_WRITE_REG(0x51, TAPE_PAGE);
        t = (uint16_t *)0x2000 + tape_n * 2;
        t[0] = tick;
        t[1] = (uint16_t)in;
        ZXN_WRITE_REG(0x51, 0xFF);
        tape_n++;
        tape_last = in;
    }
    tick++;
    if (tick % 25 == 0)
        zx_log("op: chk t=%u st=%d sc=%ld x=%ld y=%ld v=%ld e=%d h=%d\n", tick, gs.state, gs.score,
               gs.ship.wx, gs.ship.wy, gs.viewport_x, gs.enemies_alive, gs.humans_alive);
}

/* ------------------------------------------------------------ logging (test.py reads it) */

static const char *const state_names[9] = {
    "title", "playing", "dying", "respawning", "level_start", "level_clear", "gameover", "hiscore", "scores",
};

static void log_changes(void)
{
    static WORD last_state = -1, last_lives = -1, last_level = -1, last_humans = -1, last_bombs = -1;
    static LONG last_score = -1;
    if (gs.state != last_state) {
        zx_log("op: state=%s level=%d score=%ld\n", state_names[gs.state], gs.level, gs.score);
        last_state = gs.state;
    }
    if (gs.score != last_score) { zx_log("op: score=%ld\n", gs.score); last_score = gs.score; }
    if (gs.lives != last_lives) { zx_log("op: lives=%d\n", gs.lives); last_lives = gs.lives; }
    if (gs.level != last_level) { zx_log("op: level=%d\n", gs.level); last_level = gs.level; }
    if (gs.humans_alive != last_humans) { zx_log("op: humans=%d\n", gs.humans_alive); last_humans = gs.humans_alive; }
    if (gs.ship.smart_bombs != last_bombs) { zx_log("op: bombs=%d\n", gs.ship.smart_bombs); last_bombs = gs.ship.smart_bombs; }
}

/* ------------------------------------------------------------ screens */

static char line[32];

static char *num(char *p, LONG v, uint8_t digits)
{
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = '0' + (char)(v % 10); v /= 10; } while (v && n < 10);
    while (n < digits) tmp[n++] = '0';
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

static void title_screen(void)
{
    game_init(&gs, 1);                   /* the original's title: a fresh state behind it */
    gs.state = STATE_TITLE;
    gs.hiscore = hs_table[0].score;
    view_static_screen(&gs);
    view_text(96, 40, "ORBITAL PATROL", C_WHITE);
    view_text(68, 60, "SPECTRUM NEXT EDITION", C_CYAN);   /* port: "AMIGA EDITION" */
    view_text(30, 95, "QAOP/JOYSTICK    MOVE", C_TERR_LT);  /* port: the Spectrum's keys */
    view_text(30, 110, "SPACE/FIRE       SHOOT", C_TERR_LT);
    view_text(30, 125, "Z                SMART BOMB", C_TERR_LT);
    view_text(30, 140, "X OR H           HYPERSPACE", C_TERR_LT);
    view_text(20, 160, "SHOOT DIVERS TO SAVE HUMANS!", C_STAR_DIM);
    strcpy(line, "HI SCORE  ");
    num(line + 10, gs.hiscore, 7);
    view_text(80, 220, line, C_CYAN);
    zx_log("op: title\n");
}

static void title_blink(void)
{
    uint8_t on = (gs.frame / 15) & 1;      /* the original's blinking prompt */
    view_fill(80, 185, 160, 8, C_SKY);
    if (on) view_text(80, 185, "PRESS FIRE TO START", C_YELLOW);
}

static void gameover_overlay(void)
{
    view_text(96, 90, "GAME OVER", C_RED);
    strcpy(line, "SCORE  ");
    num(line + 7, gs.score, 7);
    view_text(96, 120, line, C_WHITE);
    strcpy(line, "WAVE  ");
    num(line + 6, gs.level, 1);
    view_text(120, 150, line, C_CYAN);
}

/* port: the SDK's table (5 entries, three letters) instead of the original's
   ten 8-letter names saved to disk */
static void hiscore_entry(int8_t place)
{
    hs_editor ed;
    uint8_t r = HS_EDIT_CHANGED;
    view_static_screen(&gs);
    view_text(72, 80, "NEW HIGH SCORE!", C_YELLOW);
    num(line, gs.score, 7);
    view_text(112, 110, line, C_WHITE);
    view_text(80, 140, "ENTER YOUR NAME", C_CYAN);
    view_text(44, 200, "TYPE OR UP/DN + FIRE", C_TERR_LT);
    zx_log("op: hiscore rank=%d\n", place);
    hs_edit_begin(&ed);
    while (!(r & HS_EDIT_DONE)) {
        if (r & HS_EDIT_CHANGED) {
            view_fill(148, 165, 24, 10, C_SKY);
            view_text(148, 165, ed.name, C_WHITE);
            if (ed.pos < 3) view_fill(148 + ed.pos * 8, 174, 8, 2, C_WHITE);
        }
        frame_wait();
        view_commit();
        input_read();
        r = hs_edit_step(&ed);
    }
    hs_insert(place, ed.name, (uint32_t)gs.score);
    zx_log("op: name=%s\n", ed.name);
}

static void hiscore_view(void)
{
    uint8_t i;
    view_static_screen(&gs);
    view_text(116, 20, "HIGH SCORES", C_WHITE);
    for (i = 0; i < HS_ENTRIES; i++) {
        char *p = line;
        *p++ = '1' + i;
        *p++ = '.';
        *p++ = ' ';
        strcpy(p, hs_table[i].name);
        p += 3;
        *p++ = ' ';
        *p++ = ' ';
        num(p, hs_table[i].score, 7);
        view_text(100, 50 + i * 14, line, i == 0 ? C_YELLOW : C_CYAN);
    }
    gs.state = STATE_HISCORE_VIEW;
    gs.state_timer = 300;
}

/* ------------------------------------------------------------ the loop */

static uint16_t fps_frames, fps_draws;

int main(void)
{
    static char bonus[16];
    frame_init();                        /* IM 2: nothing goes to the ROM's 0x0038 now */
    ZXN_WRITE_REG(0x50, 46);             /* the game's state at 0x0000 */
    view_init();                         /* (copies the ROM font into page 46 first) */
    ZXN_WRITE_REG(0x51, 44);             /* the music: page 44 -> 0xF200 */
    memcpy((void *)0xF200, (const void *)0x3200, 0xFE00 - 0xF200);
    ZXN_WRITE_REG(0x51, 0xFF);
    input_init();
    input_latch(1);
    hs_init(10000);                      /* 50000 40000 30000 20000 10000 */
    sound_init();
    frame_hook = isr_work;
    title_screen();
    zx_log("op: ready kempston=%u\n", input_kempston);

    for (;;) {
        uint8_t ticks = frame_wait(), t;
        WORD prev = gs.state;
        WORD in;
        view_commit();
        if (ticks > 4) ticks = 4;
        in = read_input();
        fps_frames += ticks;

        for (t = 0; t < ticks; t++) {
            switch (gs.state) {
            case STATE_TITLE:
                if (gs.frame > 30 && (in & (INPUT_START | INPUT_FIRE))) {
                    flow_start(&gs);
                    view_play_screen(&gs);
                    tick = tape_n = 0;
                    tape_last = -1;
                }
                else gs.frame++;
                break;
            case STATE_HISCORE_VIEW:
                gs.state_timer--;
                if (gs.state_timer <= 0 || (in & (INPUT_START | INPUT_FIRE))) {
                    title_screen();
                    startup_delay = 20;
                }
                break;
            default:
                tape_tick(in);
                flow_step(&gs, in);
                sound_events(&gs);
                break;
            }
            in &= ~(INPUT_BOMB | INPUT_HYPER);    /* a press counts once */
            log_changes();
            if (gs.state != prev) break;
        }

        /* the drawing for the state (the original's draw_* calls) */
        switch (gs.state) {
        case STATE_TITLE:
            if (prev == STATE_TITLE) title_blink();
            break;
        case STATE_LEVEL_START:
        case STATE_PLAYING:
        case STATE_DYING:
        case STATE_RESPAWNING:
        case STATE_LEVEL_CLEAR:
            if (gs.state == STATE_LEVEL_START) view_message(gs.level, "GET READY");
            else if (gs.state == STATE_LEVEL_CLEAR) {
                if (prev != STATE_LEVEL_CLEAR) {
                    strcpy(bonus, "BONUS ");
                    num(bonus + 6, (LONG)gs.humans_alive * SCORE_WAVE_BONUS, 1);
                }
                view_message(gs.level, bonus);
            }
            else view_message(0, 0);
            view_prepare(&gs);
            fps_draws++;
            break;
        case STATE_GAMEOVER:
            if (prev != STATE_GAMEOVER) {
                view_message(0, 0);
                view_prepare(&gs);
                gameover_overlay();
                zx_log("op: gameover score=%ld level=%d\n", gs.score, gs.level);
            }
            break;
        case STATE_HISCORE_ENTRY: {
            int8_t place = hs_rank((uint32_t)gs.score);
            if (place >= 0) hiscore_entry(place);
            hiscore_view();
            break;
        }
        }
        if (fps_frames >= 250) {
            zx_log("op: fps=%u cols=%u sprites=%u\n", fps_draws / 5, view_cols_drawn, view_sprites_used);
            fps_frames = fps_draws = 0;
        }
    }
}
