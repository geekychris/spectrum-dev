/*
 * Pea Shooter Blast for the Spectrum Next: start-up, input, the game flow and
 * the loop (the original's main.c). The rules are the original's game.c and
 * levels.c, unchanged (exec/types.h gives them the Amiga's type sizes).
 *
 * The original runs one game_update a frame at 50 Hz; so does this, with a
 * fixed timestep: after a slow frame the logic catches up (up to 4 steps),
 * so the game keeps the original's speed. The picture is drawn once a loop.
 *
 *   0x0000-0x3FFF  bank 40: the rules (game.c), over the ROM; the tiles'
 *                  page while columns are drawn
 *   0x4000-0x5FFF  page 82: levels.c and the level maps; bank 5 (the
 *                  tilemap) and sound streams while they are written / read
 *   0x6000-0x7FFF  page 84: the HUD, the fonts, the palettes, the test
 *                  probe; Layer 2 columns and sprite patterns while drawn;
 *                  page 86 (the state hash) during the scripted run
 *   0x8000-0xEFFF  this, the view, the SDK; 0xF000- the IM 2 table, the stack
 * (memmap.h). Code in a page calls only main memory and the other pages,
 * which are mapped whenever it runs.
 *   banks 20-24    Layer 2 (320 x 256), the playfield ring
 *   bank 34        the tiles (column-major); 35 the sprite patterns;
 *   bank 36        the effects' AY streams
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include <input.h>
#include "nextgfx.h"
#include "zxlog.h"
#include "psb.h"
#include "view.h"
#include "sound_data.h"
#include "state_hash.h"
#include "memmap.h"
#include "port_fixes.h"

extern void update_probe(const GameState *gs, uint8_t mode, uint8_t fps);

GameState gs;
hs_editor entry_ed;
int32_t entry_score;
static int8_t entry_place;
static uint8_t mode = MODE_TITLE, last_state = 0xFF;

volatile uint8_t hash_req;              /* test.py sets it: run the scripted game, log its hash */

/* ------------------------------------------------------------ effects (game.h) */

void sfx_shoot(void) { snd_play(SFX_SHOOT); }
void sfx_hit(void) { snd_play(SFX_HIT); }
void sfx_explode(void) { snd_play(SFX_EXPLODE); }
void sfx_powerup(void) { snd_play(SFX_POWERUP); }
void sfx_jump(void) { snd_play(SFX_JUMP); }
void sfx_player_hit(void) { snd_play(SFX_PLAYER_HIT); }

/* ------------------------------------------------------------ flow */

static void to_title(void)
{
    mode = MODE_TITLE;
    gs.state = STATE_TITLE;
    view_sprites_off();
    view_title();
    zx_log("psb: TITLE\n");
}

static void start_game(void)
{
    game_init(&gs);
    port_fixes(&gs);
    mode = MODE_GAME;
    zx_log("psb: START\n");
}

/* the game said STATE_TITLE (game over shown for 3 s): port: the initials
   first if the score is in the table */
static void game_finished(void)
{
    entry_place = hs_rank((uint32_t)gs.score);
    entry_score = gs.score;
    if (entry_place >= 0) {
        mode = MODE_ENTRY;
        view_sprites_off();
        view_blank();
        hs_edit_begin(&entry_ed);
        zx_log("psb: HISCORE rank=%d\n", entry_place);
    } else to_title();
}

static void log_state(void)
{
    static const char *const st[] = { "TITLE", "PLAYING", "DEAD", "GAMEOVER", "LEVELCLEAR" };
    if (gs.state == last_state) return;
    last_state = (uint8_t)gs.state;
    zx_log("psb: STATE %s level=%d lives=%d score=%ld\n", st[gs.state], gs.level, gs.lives, (long)gs.score);
}

/* one 50 Hz step (the original's loop body: title, or game_update) */
static void step(uint8_t in)
{
    if (mode == MODE_TITLE) {
        gs.frame++;
        if (in & IN_FIRE) start_game();
        return;
    }
    if (mode != MODE_GAME) return;
    game_update(&gs, (in & IN_LEFT) ? 1 : 0, (in & IN_RIGHT) ? 1 : 0, (in & IN_UP) ? 1 : 0,
                (in & IN_DOWN) ? 1 : 0, (in & IN_FIRE) ? 1 : 0);
    log_state();
    if (gs.state == STATE_TITLE) game_finished();
}

/* ------------------------------------------------------------ the scripted game */

/* the same game as tools/host_trace.c: game_init, then 3000 frames of
   input from script_input(), then the state's hash */
static void hash_run(void)
{
    uint16_t f;
    snd_music(0);
    ZXN_WRITE_MMU3(HASH_PAGE);
    game_init(&gs);
    port_fixes(&gs);
    for (f = 0; f < SCRIPT_FRAMES && gs.state != STATE_TITLE; f++) {
        uint8_t in = script_input(f);
        game_update(&gs, in & 1, (in >> 1) & 1, (in >> 2) & 1, (in >> 3) & 1, (in >> 4) & 1);
    }
    {
        uint32_t h = state_hash(&gs);
        ZXN_WRITE_MMU3(UI_PAGE);
        zx_log("psb: HASH %lx frames=%u level=%d score=%ld lives=%d\n", (unsigned long)h, f, gs.level,
               (long)gs.score, gs.lives);
    }
    hash_req = 0;
    snd_music(1);
    to_title();
}

/* ------------------------------------------------------------ the loop */

int main(void)
{
    uint8_t fps = 0, draws = 0;
    uint16_t fps_frames = 0;
    ZXN_WRITE_MMU0(RULES_PAGE);         /* the rules (bank 40) over the ROM */
    ZXN_WRITE_MMU1(RULES_PAGE + 1);
    ZXN_WRITE_MMU2(LEVELS_PAGE);
    ZXN_WRITE_MMU3(UI_PAGE);
    next_init();
    hud_init();
    view_init();
    input_init();
    snd_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    hs_init(100);                       /* 500 400 300 200 100: a level is worth a few hundred */
    game_init(&gs);                     /* as the original: the levels, then the title */
    port_fixes(&gs);
    to_title();
    snd_music(1);
    zx_log("psb: ready kempston=%u\n", input_kempston);

    for (;;) {
        uint8_t n = frame_wait(), k, in;
        for (k = 0; k < n; k++) snd_tick();
        fps_frames += n;
        if (hash_req && mode == MODE_TITLE) { hash_run(); continue; }
        in = input_read();
        if (mode == MODE_ENTRY) {
            uint8_t r = hs_edit_step(&entry_ed);
            if (r & HS_EDIT_TAKEN) snd_play(SFX_KEY);
            if (r & HS_EDIT_DONE) {
                hs_insert(entry_place, entry_ed.name, (uint32_t)entry_score);
                zx_log("psb: NAME %s\n", entry_ed.name);
                hud_title_page = 1;
                to_title();
            }
        } else if ((input_pressed() & IN_QUIT) && mode == MODE_GAME) {
            to_title();                 /* port: BREAK for the original's Esc */
        } else {
            if (n > 4) n = 4;
            while (n--) step(in);
        }
        if (mode == MODE_GAME) {
            view_playfield((uint8_t)gs.level, (int16_t)gs.scroll.pixel_x);
            view_sprites(&gs);
        }
        hud_frame(&gs, mode);
        draws++;
        if (fps_frames >= 250) {
            fps = (uint8_t)((uint16_t)draws * 50 / fps_frames);
            zx_log("psb: fps=%u mode=%u overruns=%u ay=%u,%u,%u\n", fps, mode, frame_overruns,
                   snd_reg(0, 7), snd_reg(0, 8), snd_reg(1, 8));
            draws = 0;
            fps_frames = 0;
            if (mode == MODE_TITLE) hud_title_page ^= 1;
        }
        update_probe(&gs, mode, fps);
    }
    return 0;
}
