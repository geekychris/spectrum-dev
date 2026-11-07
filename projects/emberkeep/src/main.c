/*
 * Emberkeep for the ZX Spectrum Next - port of amiga_mcp/examples/emberkeep.
 *
 * game.c holds the original's rules; this is its main.cpp loop at a fixed
 * 50 Hz (game_tick() once per frame that passed, the screen updated once)
 * with the Next front end: screen.c (Layer 2 chamber, hardware sprites, the
 * lantern as copper-driven clip windows, tilemap HUD), sound.c (TurboSound),
 * the title, the end screens and the high scores.
 *
 * Controls: Q/A/O/P, cursor keys, Sinclair or Kempston walk; SPACE (fire) or
 * ENTER begins and returns to the title.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "game.h"
#include "levels.h"
#include "gfx_data.h"
#include "ekscreen.h"
#include "sound.h"

#define ROM_FONT ((const uint8_t *)0x3D00)
#define BIG_TILE 176                    /* the title's 2x letters: 4 tiles each */

/* front-end screens (the original's modes, plus the initials) */
#define FE_TITLE  0
#define FE_PLAY   1                     /* GM_PLAYING and GM_LEVELWIN */
#define FE_END    2                     /* GM_WIN, GM_LOSE */
#define FE_NAME   3                     /* port: a high score's initials */

static uint8_t fe = 255, last_mode = 255;
static hs_editor ed;
static int8_t ed_place;
static uint32_t final_score;

/* for the test (test.py reads them by symbol) */
struct { uint16_t frames, ticks, draws; } ember_perf;

/* ------------------------------------------------------------ title */

static void big_text(uint8_t col, uint8_t row, const char *s, uint8_t pal)
{
    /* each letter once as 2 x 2 tiles (EMBERKEEP: E M B R K P) */
    static char made[8];
    uint8_t n = 0;
    for (; *s; s++, col += 2) {
        uint8_t i;
        for (i = 0; i < n && made[i] != *s; i++) {}
        if (i == n) {
            made[n++] = *s;
            tm_glyph2x(BIG_TILE + 4 * i, ROM_FONT + 8 * (*s - 32), 1, 3);
        }
        tm_put(col, row, BIG_TILE + 4 * i, pal);
        tm_put(col + 1, row, BIG_TILE + 4 * i + 1, pal);
        tm_put(col, row + 1, BIG_TILE + 4 * i + 2, pal);
        tm_put(col + 1, row + 1, BIG_TILE + 4 * i + 3, pal);
    }
}

static void draw_scores(uint8_t row)
{
    uint8_t i;
    tm_centre(row, "THE DEEPEST DELVERS", TP_YELLOW);
    for (i = 0; i < HS_ENTRIES; i++) {
        tm_num(13, row + 2 + i, i + 1, 1, TP_WHITE);
        tm_text(16, row + 2 + i, hs_table[i].name, TP_CYAN);
        tm_num(21, row + 2 + i, hs_table[i].score, 6, TP_WHITE);
    }
}

static void title_screen(void)
{
    uint8_t c;
    scr_lantern(0);
    scr_sprites(0);
    tm_clear();
    scr_l2_clear();
    for (c = 0; c < GRID_W; c++) {                      /* a band of the keep's wall */
        scr_tile(c, 2, (c & 1) ? TILE_WALL_A : TILE_WALL_B);
        scr_tile(c, 5, (c & 1) ? TILE_WALL_B : TILE_WALL_A);
    }
    scr_tile(2, 3, TILE_TORCH);
    scr_tile(17, 3, TILE_TORCH);
    scr_tile(5, 8, TILE_RUNE);
    scr_tile(9, 8, TILE_OIL);
    scr_tile(13, 8, TILE_PORTAL_OPEN);
    big_text(11, 7, "EMBERKEEP", TP_ORANGE);
    tm_centre(13, "A LANTERN IN THE DEEP DARK", TP_YELLOW);
    tm_text(9, 19, "RUNE", TP_YELLOW);
    tm_text(17, 19, "OIL", TP_ORANGE);
    tm_text(24, 19, "PORTAL", TP_GREEN);
    draw_scores(21);
    tm_centre(31, "QAOP CURSOR KEMPSTON MOVE  SPACE BEGIN", TP_GREEN);
    /* the delver and what stalks it, as sprites */
    spr_show(0, 64, 56, SPR_POSE_RIGHT, 0);
    spr_show(1, 40, 128, SPR_ENEMY_A, 0);
    spr_show(2, 264, 128, SPR_ENEMY_B, 0);
    spr_show(3, 240, 56, SPR_ENEMY_C, 0);
    snd_music(MUSIC_TITLE);
}

/* ------------------------------------------------------------ end screens */

static void end_screen(void)
{
    scr_lantern(0);
    scr_sprites(0);
    tm_clear();
    scr_l2_clear();
    if (gs.mode == GM_WIN) {
        l2w_fill(0, 80, 320, 30, 7);                    /* pen 7: green */
        tm_centre(11, "THE DARK IS BEATEN", TP_WHITE);
    } else {
        l2w_fill(0, 80, 320, 30, 9);                    /* pen 9: bright red */
        tm_centre(11, "GAME OVER", TP_WHITE);
    }
    tm_text(11, 19, "FINAL SCORE", TP_WHITE);
    tm_num(23, 19, gs.score, 6, TP_WHITE);
    snd_music(MUSIC_OFF);
    snd_play(gs.mode == GM_WIN ? SND_WIN : SND_OVER);
}

/* ------------------------------------------------------------ initials */

static void name_screen(void)
{
    tm_clear();
    scr_l2_clear();
    big_text(17, 6, "NEW", TP_ORANGE);
    tm_centre(10, "A NEW DEEPEST DELVER", TP_YELLOW);
    tm_text(14, 13, "SCORE", TP_WHITE);
    tm_num(20, 13, final_score, 6, TP_WHITE);
    tm_centre(17, "TYPE YOUR INITIALS", TP_WHITE);
    tm_centre(18, "(OR UP/DOWN + FIRE)", TP_GREY);
    hs_edit_begin(&ed);
    zx_log("ember: hiscore rank=%d\n", (int)ed_place);
}

static void name_draw(void)
{
    uint8_t i;
    char one[2] = { 0, 0 };
    for (i = 0; i < 3; i++) {
        one[0] = ed.name[i];
        tm_text(18 + 2 * i, 21, one, i == ed.pos ? TP_YELLOW : TP_WHITE);
    }
}

/* ------------------------------------------------------------ the loop */

static void log_state(void)
{
    static int16_t lives = -1, level = -1, runes = -1;
    static uint32_t score = 0xFFFFFFFF;
    if (gs.mode != last_mode) {
        static const char *const names[5] = {"title", "playing", "win", "lose", "levelwin"};
        zx_log("ember: mode=%s level=%d lives=%d score=%ld\n", names[gs.mode], gs.level_index,
               gs.lives, (long)gs.score);
        last_mode = gs.mode;
    }
    if (gs.score != score) { zx_log("ember: score=%ld\n", (long)gs.score); score = gs.score; }
    if (gs.lives != lives) { zx_log("ember: lives=%d\n", gs.lives); lives = gs.lives; }
    if (gs.level_index != level) { zx_log("ember: level=%d\n", gs.level_index); level = gs.level_index; }
    if (gs.runes_remaining != runes) { zx_log("ember: runes=%d\n", gs.runes_remaining); runes = gs.runes_remaining; }
}

/* the effects of a tick's events */
static void sounds(uint8_t ch)
{
    if (ch & CHANGE_PICKUP_RUNE) snd_play(SND_RUNE);
    if (ch & CHANGE_PICKUP_OIL) snd_play(SND_OIL);
    if (ch & CHANGE_EXIT) snd_play(SND_EXIT);
    if (ch & CHANGE_DEATH) {
        snd_play(SND_DEATH);
        zx_log("ember: death at %d,%d lantern=%d\n", gs.player.x, gs.player.y, gs.lantern);
    }
    /* port: a tick of warning while the lantern is low */
    if (gs.mode == GM_PLAYING && gs.lantern < LANTERN_MAX / 4 && gs.tick % 50 == 0) snd_play(SND_LOW);
}

static void isr_work(void)
{
    input_sample();
}

int main(void)
{
    uint8_t changes = 0, room = 0;

    next_init();
    scr_init();
    snd_init();
    input_init();
    frame_init();
    frame_hook = isr_work;
    input_latch(1);
    zx_border(INK_BLACK);
    hs_init(400);                       /* port: the original has no table; 2000 .. 400 */
    game_init();
    zx_log("ember: ready kempston=%u turbo=%u\n", input_kempston, next_reg_read(7) & 3);

    for (;;) {
        uint8_t ticks, t, in, pressed, input;

        ticks = frame_wait();
        if (ticks > 4) ticks = 4;
        ember_perf.frames += ticks;

        /* at the top of the frame: the lantern's copper list and the sprites,
           from the last frame's state, together */
        if (fe == FE_PLAY) {
            scr_lantern(gs.mode == GM_PLAYING || gs.mode == GM_LEVELWIN);
            scr_sprites(1);
        }
        for (t = 0; t < ticks; t++) snd_tick();

        in = input_read();
        pressed = input_pressed();

        if (fe == FE_NAME) {
            uint8_t r = hs_edit_step(&ed);
            if (r & HS_EDIT_CHANGED) name_draw();
            if (r & HS_EDIT_DONE) {
                hs_insert(ed_place, ed.name, final_score);
                zx_log("ember: name=%s\n", ed.name);
                fe = 255;               /* the title */
            }
            if (fe == FE_NAME) continue;
        }

        /* the original's input: walking held, START on its rising edge */
        input = 0;
        if (in & IN_LEFT) input |= INPUT_LEFT;
        if (in & IN_RIGHT) input |= INPUT_RIGHT;
        if (in & IN_UP) input |= INPUT_UP;
        if (in & IN_DOWN) input |= INPUT_DOWN;
        if (pressed & (IN_FIRE | IN_START)) input |= INPUT_START;

        game_changes = 0;
        for (t = 0; t < ticks; t++) {
            uint8_t before = gs.mode;
            game_tick(input);
            input &= ~INPUT_START;      /* an edge counts once */
            if (gs.mode == GM_PLAYING) ember_perf.ticks++;
            sounds(game_changes);
            changes |= game_changes;
            game_changes = 0;
            log_state();
            if (gs.mode != before) break;
        }

        /* the screen for the mode */
        if (gs.mode == GM_TITLE) {
            if (fe == FE_END) {
                ed_place = hs_rank(final_score);
                if (ed_place >= 0) {
                    fe = FE_NAME;
                    name_screen();
                    name_draw();
                    continue;
                }
            }
            if (fe != FE_TITLE) {
                fe = FE_TITLE;
                title_screen();
                zx_log("ember: title\n");
            }
            if ((gs.tick % 15) == 0)
                tm_centre(29, (gs.tick / 15) & 1 ? "PRESS  SPACE  TO  BEGIN" : "                       ", TP_RED);
        } else if (gs.mode == GM_WIN || gs.mode == GM_LOSE) {
            if (fe != FE_END) {
                fe = FE_END;
                final_score = gs.score;
                end_screen();
            }
            if (gs.state_timer == 0 && (gs.tick % 15) == 0)
                tm_centre(26, (gs.tick / 15) & 1 ? "PRESS SPACE TO RETURN" : "                     ", TP_YELLOW);
        } else {
            if (fe != FE_PLAY) {
                fe = FE_PLAY;
                tm_clear();
                snd_music(MUSIC_DEEP);
                changes |= CHANGE_LEVEL;
            }
            if (changes & CHANGE_LEVEL) {
                scr_room();
                scr_hud(1);
                room = gs.level_index;
                scr_lantern_reset();
                tm_fill(0, 10, TM_COLS, 3, 0, 0);       /* the chamber banner, if any */
            } else if (changes & CHANGE_TILE) {
                scr_cell(changed_col, changed_row);
            }
            if (gs.mode == GM_LEVELWIN && gs.level_index != room) {
                /* port: the next chamber's name while the original pauses */
                room = gs.level_index;
                tm_centre(11, "THE PORTAL OPENS", TP_GREEN);
                tm_centre(12, level_names[gs.level_index], TP_YELLOW);
            }
            scr_portal();
            scr_hud(0);
            if (gs.mode == GM_PLAYING) ember_perf.draws++;
        }
        changes = 0;
    }
}
