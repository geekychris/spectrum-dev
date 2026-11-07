/*
 * Bullion Dash for the ZX Spectrum 48K - port of amiga_mcp/examples/bullion_dash.
 *
 * player.c, enemy.c and level.c are the original's rules (port changes
 * marked "port:"); this file is the original main.c's state machine run at a
 * fixed 50 Hz (logic `ticks` times a frame, the screen drawn once), with the
 * Spectrum front end: render.c, beeper effects (AY on a 128K), the title
 * screen and high scores, and the level editor (editor.c).
 *
 * Controls: Q/A/O/P, cursor keys, Sinclair or Kempston; FIRE (SPACE, M, 0)
 * held with left/right digs. On the title O/P choose the starting level,
 * E opens the editor. BREAK (CAPS SHIFT + SPACE) gives up a life.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "game.h"
#include "level.h"
#include "play.h"
#include "editor.h"
#include "render.h"
#include "art.h"
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"

GameState gs;
uint8_t sfx_pending;

static int16_t transition_frames;
static int16_t start_level = 1;     /* port: the title chooses the first level */
static uint8_t test_play;           /* playing the editor's level */
static uint8_t need_room;           /* the play screen must be redrawn */

/* ------------------------------------------------------------ sound */

static void sound(uint8_t s)
{
    if (ay_present) {
        switch (s) {
        case SFX_DIG:   ay_noise(2, 12, 13); ay_sfx(1, 900, 12, 2, 40); break;
        case SFX_GOLD:  ay_sfx(0, 180, 14, 1, -6); break;
        case SFX_TRAP:  ay_sfx(1, 400, 13, 1, 20); break;
        case SFX_DEATH: ay_sfx(0, 300, 15, 1, 30); break;
        case SFX_LEVEL: ay_sfx(0, 250, 15, 1, -8); break;
        case SFX_OVER:  ay_sfx(0, 500, 15, 1, 25); break;
        }
        return;
    }
    switch (s) {                    /* beeper: short, blocking */
    case SFX_DIG:   beep_noise(180); break;
    case SFX_GOLD:  beep(40, 30); beep(28, 45); break;
    case SFX_TRAP:  beep_sweep(60, 120, 10, 2); break;
    case SFX_DEATH: beep_sweep(40, 200, 8, 3); break;
    case SFX_LEVEL: beep(80, 15); beep(60, 20); beep(45, 25); beep(30, 35); beep(22, 50); break;
    case SFX_OVER:  beep(40, 25); beep(60, 20); beep(90, 15); beep(130, 12); break;
    }
}

/* ------------------------------------------------------------ title */

static void title_level(void)
{
    text_num(16, 22, start_level, 3);
}

static void title_screen(void)
{
    uint8_t c;
    screen_clear(PAPER_BLACK | INK_WHITE, INK_BLUE);
    attr_fill(0, 0, 32, 2, PAPER_BLACK | INK_YELLOW | BRIGHT);
    text_big(4, 0, "BULLION DASH");
    for (c = 4; c < 28; c++) tile_draw(c, 2, tile_gfx + 8 * TILE_BRICK);
    attr_fill(4, 2, 24, 1, PAPER_BLACK | INK_RED);
    attr_fill(0, 3, 32, 1, PAPER_BLACK | INK_CYAN | BRIGHT);
    text_center(3, "for the ZX Spectrum");
    attr_fill(0, 6, 32, 5, PAPER_BLACK | INK_GREEN | BRIGHT);
    zx_print_at(6, 6, "QAOP/JOY   MOVE");
    zx_print_at(6, 7, "FIRE+DIR   DIG");
    zx_print_at(6, 8, "E          EDITOR");
    zx_print_at(6, 9, "BREAK      GIVE UP");
    tile_draw(6, 11, tile_gfx + 8 * TILE_BRICK);
    tile_draw(14, 11, tile_gfx + 8 * TILE_LADDER);
    tile_draw(23, 11, tile_gfx + 8 * TILE_GOLD);
    attr_fill(0, 11, 32, 1, PAPER_BLACK | INK_WHITE);
    attr_set(6, 11, tile_attr[TILE_BRICK]);
    attr_set(14, 11, tile_attr[TILE_LADDER]);
    attr_set(23, 11, tile_attr[TILE_GOLD]);
    zx_print_at(8, 11, "Brick");
    zx_print_at(16, 11, "Ladder");
    zx_print_at(25, 11, "Gold");
    attr_fill(0, 13, 32, 1, PAPER_BLACK | INK_YELLOW | BRIGHT);
    text_center(13, "HIGH SCORES");
    attr_fill(0, 14, 32, 5, PAPER_BLACK | INK_WHITE | BRIGHT);
    hs_draw(10, 14);
    attr_fill(0, 22, 32, 1, PAPER_BLACK | INK_CYAN | BRIGHT);
    zx_print_at(2, 22, "START LEVEL <     > O/P");
    title_level();
    attr_fill(0, 23, 32, 1, PAPER_BLACK | INK_WHITE);
    text_center(23, "KEYS SINCLAIR KEMPSTON");
}

/* ------------------------------------------------------------ logging */

static const char *const state_names[6] = {"title", "playing", "dying", "level_done", "gameover", "editor"};

static void log_state(void)
{
    static int16_t last_state = -1, last_lives = -1, last_level = -1, last_gold = -1;
    static uint32_t last_score = 0xFFFFFFFF;
    if (gs.state != last_state) {
        zx_log("bullion: state=%s level=%d lives=%d score=%ld\n", state_names[gs.state],
               gs.level_num, gs.lives, (long)gs.score);
        last_state = gs.state;
    }
    if (gs.score != last_score) { zx_log("bullion: score=%ld\n", (long)gs.score); last_score = gs.score; }
    if (gs.lives != last_lives) { zx_log("bullion: lives=%d\n", gs.lives); last_lives = gs.lives; }
    if (gs.level_num != last_level) { zx_log("bullion: level=%d\n", gs.level_num); last_level = gs.level_num; }
    if (gs.gold_collected != last_gold) {
        zx_log("bullion: gold=%d/%d\n", gs.gold_collected, gs.gold_total);
        last_gold = gs.gold_collected;
    }
}

/* ------------------------------------------------------------ the game (the original's main loop) */

static void start_level_play(void)
{
    level_load(gs.level_num);
    gs.state = STATE_PLAYING;
    need_room = 1;
}

static void die(const char *why)
{
    zx_log("bullion: death %s at %d,%d\n", why, (int)gs.player.gx, (int)gs.player.gy);
    gs.state = STATE_DYING;
    gs.player.state = PS_DEAD;
    transition_frames = 30;
    sound(SFX_DEATH);
}

static void back_to_editor(void)
{
    test_play = 0;
    gs.state = STATE_EDITOR;
    editor_resume();
}

/* one 50 Hz frame of the original's loop; held/pressed = IN_* bits */
static void game_tick(uint8_t held, uint8_t pressed, char letter)
{
    switch (gs.state) {
    case STATE_TITLE:
        if (pressed & IN_FIRE) {
            gs.score = 0;
            gs.lives = 5;
            gs.level_num = start_level;
            start_level_play();
            zx_log("bullion: game started at level %d\n", start_level);
        } else if (letter == 'E') {
            gs.state = STATE_EDITOR;
            editor_init();
            zx_log("bullion: editor\n");
        } else if (pressed & (IN_LEFT | IN_RIGHT)) {     /* port: level select */
            start_level += (pressed & IN_LEFT) ? -1 : 1;
            if (start_level < 1) start_level = MAX_LEVELS;
            if (start_level > MAX_LEVELS) start_level = 1;
            title_level();
        }
        break;

    case STATE_PLAYING:
        switch (play_tick(held, pressed & IN_QUIT)) {
        case PLAY_CAUGHT:  die("caught"); break;
        case PLAY_BURIED:  die("buried"); break;
        case PLAY_GAVE_UP: die("gave up"); break;
        case PLAY_WON:
            gs.state = STATE_LEVEL_DONE;
            transition_frames = 60;
            sound(SFX_LEVEL);
            zx_log("bullion: level %d complete\n", gs.level_num);
            break;
        }
        break;

    case STATE_DYING:
        if (--transition_frames > 0) break;
        if (test_play) { back_to_editor(); break; }
        gs.lives--;
        if (gs.lives <= 0) {
            gs.state = STATE_GAMEOVER;
            transition_frames = 30;
            sound(SFX_OVER);
            zx_log("bullion: gameover score=%ld\n", (long)gs.score);
        } else {
            start_level_play();     /* reload the current level */
        }
        break;

    case STATE_LEVEL_DONE:
        if (--transition_frames > 0) break;
        if (test_play) { back_to_editor(); break; }
        gs.score += 1000;
        gs.level_num++;
        if (gs.level_num > MAX_LEVELS) gs.level_num = 1;
        start_level_play();
        break;

    case STATE_GAMEOVER:
        if (transition_frames > 0)
            transition_frames--;
        else if (pressed & IN_FIRE)
            gs.state = STATE_TITLE;
        break;

    case STATE_EDITOR:
        switch (editor_update(held, pressed, letter)) {
        case EDITOR_TEST:
            test_play = 1;
            gs.score = 0;
            gs.lives = 1;
            gs.state = STATE_PLAYING;
            need_room = 1;
            zx_log("bullion: test play\n");
            break;
        case EDITOR_EXIT:
            gs.state = STATE_TITLE;
            break;
        }
        break;
    }
}

/* every 50 Hz interrupt: latch the keys (a tap shorter than a slow frame counts) */
static void isr_work(void)
{
    input_sample();
}

/* for the test bot (test.py reads it by symbol) */
struct { uint16_t ticks, draws; } bullion_perf;

int main(void)
{
    int16_t prev_state;

    gfx_init();
    input_init();
    input_fast();
    ay_init();
    hs_init(100);                   /* port: the original has no table; 500 400 300 200 100 */
    level_init();
    render_init();
    frame_init();
    input_latch(1);
    frame_hook = isr_work;
    gs.lives = 5;
    gs.level_num = 1;
    gs.state = STATE_TITLE;
    title_screen();
    zx_log("bullion: ready kempston=%u ay=%u\n", input_kempston, ay_present);

    for (;;) {
        uint8_t ticks, t, held, pressed;
        char letter;

        prev_state = gs.state;
        ticks = frame_wait();       /* fixed timestep: the logic runs at 50 Hz */
        if (ticks > 4) ticks = 4;

        input_read();
        held = input_last;
        pressed = input_pressed();
        letter = (gs.state == STATE_TITLE || gs.state == STATE_EDITOR) ? input_letter_pressed() : 0;

        for (t = 0; t < ticks; t++) {
            game_tick(held, pressed, letter);
            pressed = 0;            /* an edge counts once */
            letter = 0;
            gs.frame++;
            if (gs.state == STATE_PLAYING) bullion_perf.ticks++;
            log_state();
            if (gs.state != prev_state || need_room) break;
        }
        if (sfx_pending) {
            sound(sfx_pending);
            sfx_pending = 0;
        }
        if (ay_present) sound_tick();

        if (need_room) {
            need_room = 0;
            render_room(0);
        }
        switch (gs.state) {
        case STATE_TITLE:
            if (prev_state != STATE_TITLE) {
                int8_t place = prev_state == STATE_GAMEOVER ? hs_rank(gs.score) : -1;
                if (place >= 0) hs_enter(place, gs.score, "bullion");
                title_screen();
                zx_log("bullion: title\n");
            } else if ((gs.frame & 15) == 0) {
                attr_fill(0, 5, 32, 1, (gs.frame & 16) ? (PAPER_BLACK | INK_WHITE | BRIGHT) : PAPER_BLACK);
                text_center(5, "PRESS FIRE TO START");
            }
            break;
        case STATE_PLAYING:
            render_frame(1);
            render_status(0);
            bullion_perf.draws++;
            if ((bullion_perf.draws & 255) == 0)
                zx_log("bullion: perf ticks=%u draws=%u\n", bullion_perf.ticks, bullion_perf.draws);
            break;
        case STATE_DYING:
            render_frame((transition_frames & 3) >= 2);     /* the original's blink */
            render_status(0);
            break;
        case STATE_LEVEL_DONE:
            if (prev_state != STATE_LEVEL_DONE) {
                render_frame(1);
                render_status(0);
                render_box("LEVEL COMPLETE!", 0, "+1000 BONUS!", PAPER_BLACK | INK_YELLOW | BRIGHT);
            }
            break;
        case STATE_GAMEOVER:
            if (prev_state != STATE_GAMEOVER) {
                render_frame(0);
                render_status(0);
                render_box("GAME OVER", "SCORE 000000", "PRESS FIRE", PAPER_BLACK | INK_RED | BRIGHT);
                text_num(16, 9, gs.score, 6);
            }
            break;
        case STATE_EDITOR:
            editor_draw();
            break;
        }
    }
}
