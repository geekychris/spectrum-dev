/*
 * Ace Pilot for the Spectrum Next - port of amiga_mcp/examples/ace_pilot.
 *
 * game.c is the original's rules (50 ticks a second); render.c draws the
 * view on Layer 2; front.c the title, the HUD (tilemap), game over and the
 * sounds; bot.c the autopilot. This file is the original's main.c frame:
 * input, the fixed-timestep loop, the double buffer, one or two players
 * (two: the view split in two, as the original's).
 *
 * Memory:
 *   0x0000-0x3FFF  bank 16 (pages 32, 33): render.c, mapped while drawing;
 *                  bank 17 (pages 34, 35): game.c, while the logic runs;
 *                  the ROM otherwise
 *   0x4000-0x5FFF  bank 5: the tilemap (the ULA is off)
 *   0x6000-0x7FFF  Layer 2's pages while drawing (l2line)
 *   0x8000-0xEFFF  this, front.c, bot.c, the SDK, the shared tables
 *   0xF000-        the IM 2 table, the stack
 *   banks 9-14     Layer 2 (two 256 x 192 buffers)
 * Code in a bank only calls main memory.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "input2.h"
#include "game.h"
#include "ace_input.h"
#include "render.h"
#include "front.h"
#include "bot.h"
#include "l2line.h"

#define DRAW() do { ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33); } while (0)
#define LOGIC() do { ZXN_WRITE_MMU0(34); ZXN_WRITE_MMU1(35); } while (0)
#define ROM()  do { ZXN_WRITE_MMU0(0xFF); ZXN_WRITE_MMU1(0xFF); } while (0)

GameWorld world;                        /* test.py reads it by symbol */
static uint8_t game_state = STATE_TITLE;
static uint8_t autopilot;
static in2_pad pad1, pad2;
static uint32_t ticks_played;           /* since the game started (the hash's clock) */
static uint8_t last_keys;
static uint8_t drawn;                   /* a picture waits in the back buffer */
static uint16_t hash_ctr;

/* extra keys (in2's last scan): half-row, mask */
#define K_W     in2_key(2, 0x02)
#define K_S     in2_key(1, 0x02)
#define K_U     in2_key(5, 0x08)
#define K_H     in2_key(6, 0x10)
#define K_ENTER in2_key(6, 0x01)
#define K_C     in2_key(0, 0x08)
#define K_B     in2_key(7, 0x10)
#define K_1     in2_key(3, 0x01)
#define K_2     in2_key(3, 0x02)
#define K_M     in2_key(7, 0x04)
#define K_BREAK (in2_key(0, 0x01) && in2_key(7, 0x01))

static UWORD pad_flags(const in2_pad *p)
{
    UWORD f = 0;
    if (p->held & IN_LEFT) f |= INPUT_LEFT;
    if (p->held & IN_RIGHT) f |= INPUT_RIGHT;
    if (p->held & IN_UP) f |= INPUT_UP;
    if (p->held & IN_DOWN) f |= INPUT_DOWN;
    if (p->held & IN_FIRE) f |= INPUT_FIRE;
    return f;
}

static void set_players(uint8_t n)
{
    g_tune.num_players = n;
    /* one player: every control; two: player 1 Q A O P / Sinclair 2 /
       Kempston 1, player 2 Sinclair 1 / Kempston 2 / I J K L */
    pad1.src = n == 1 ? IN2_QAOP | IN2_CURSOR | IN2_SINCLAIR2 | IN2_KEMPSTON1 | IN2_SPACE
                      : IN2_QAOP | IN2_SINCLAIR2 | IN2_KEMPSTON1 | IN2_SPACE;
    pad2.src = IN2_SINCLAIR1 | IN2_KEMPSTON2 | IN2_IJKL;
}

static void to_title(void)
{
    drawn = 0;
    game_state = STATE_TITLE;
    autopilot = 0;
    front_title((uint8_t)g_tune.num_players);
    zx_log("ace: title players=%d\n", (int)g_tune.num_players);
}

static void start_game(uint8_t bot)
{
    LOGIC();
    game_init(&world);
    ROM();
    autopilot = bot;
    ticks_played = 0;
    hash_ctr = 0;
    drawn = 0;
    game_state = STATE_PLAYING;
    front_play();
    zx_log("ace: start players=%d auto=%u\n", (int)g_tune.num_players, autopilot);
}

static void draw_views(void)
{
    l2_clear(0);                        /* maps slots 0-1 itself: before DRAW() */
    DRAW();
    if (g_tune.num_players >= 2) {
        render_view(0, 0, 255, 94);
        render_scene(&world, 0);
        render_view(0, 97, 255, 191);
        render_scene(&world, 1);
        render_view(0, 0, 255, 191);
        ROM();
        l2l_ytop = 0;
        l2l_ybot = 191;
        l2l_line(0, 95, 255, 95, 0x92);  /* the original's divider */
        l2l_line(0, 96, 255, 96, 0x92);
    } else {
        render_scene(&world, 0);
        ROM();
    }
    front_overlay(&world);
}

/* the game's state for the test, every 1000 ticks and at game over */
static void log_hash(void)
{
    zx_log("ace: hash t=%lu h=%04x w=%d s=%ld,%ld l=%d,%d\n", (unsigned long)ticks_played, ace_hash(&world),
           (int)world.wave, (long)world.players[0].score, (long)world.players[1].score,
           (int)world.players[0].lives, (int)world.players[1].lives);
}

int main(void)
{
    uint16_t perf_frames = 0, perf_draws = 0;
    uint8_t keys, edge;
    next_init();
    l2_init();
    in2_init();
    frame_init();
    front_sound_init();
    front_init();
    DRAW();
    render_view(0, 0, 255, 191);        /* banked: map it first */
    ROM();
    set_players(1);
    to_title();
    zx_log("ace: ready\n");

    for (;;) {
        uint8_t ticks = frame_wait(), t;
        if (ticks > 12) ticks = 12;     /* the original's speed down to ~4 fps */
        if (drawn) { l2_flip(); drawn = 0; }
        in2_scan();
        in2_update(&pad1);
        in2_update(&pad2);
        keys = (uint8_t)((K_C ? 1 : 0) | (K_B ? 2 : 0) | (K_1 ? 4 : 0) | (K_2 ? 8 : 0) | (K_BREAK ? 16 : 0) | (K_M ? 32 : 0));
        edge = keys & ~last_keys;
        last_keys = keys;

        if (edge & 32) {
            music_want ^= 1;
            zx_log("ace: music=%u\n", music_want);
        }
        if (game_state == STATE_TITLE) {
            if (edge & 1) {
                g_tune.display_mode = 1 - g_tune.display_mode;
                zx_log("ace: display=%d\n", (int)g_tune.display_mode);
            }
            if (edge & 12) {
                set_players(edge & 4 ? 1 : 2);
                front_title_players((uint8_t)g_tune.num_players);
                zx_log("ace: players=%d\n", (int)g_tune.num_players);
            }
            if (edge & 2) start_game(1);
            else if ((pad1.pressed & IN_FIRE) && !(keys & 16)) start_game(0);
            continue;
        }

        if (edge & 16) { to_title(); continue; }
        if (edge & 2) { autopilot ^= 1; zx_log("ace: autopilot=%u\n", autopilot); }
        if (edge & 1) g_tune.display_mode = 1 - g_tune.display_mode;
        if (world.state == STATE_GAMEOVER && world.state_timer <= 0
            && ((pad1.pressed & IN_FIRE) || autopilot)) {
            to_title();
            continue;
        }

        {
            UWORD in1 = pad_flags(&pad1), in2 = pad_flags(&pad2);
            if (K_W) in1 |= INPUT_THROT_UP;
            if (K_S) in1 |= INPUT_THROT_DN;
            if (g_tune.num_players >= 2) {
                if (K_ENTER) in2 |= INPUT_FIRE;
                if (K_U) in2 |= INPUT_THROT_UP;
                if (K_H) in2 |= INPUT_THROT_DN;
            } else in2 = 0;
            for (t = 0; t < ticks; t++) {
                uint8_t was = (uint8_t)world.state;
                if (autopilot) {
                    in1 = ace_bot(&world, 0);
                    in2 = g_tune.num_players >= 2 ? ace_bot(&world, 1) : 0;
                }
                LOGIC();
                game_update(&world, in1, in2);
                ROM();
                ticks_played++;
                if (++hash_ctr == 1000) { hash_ctr = 0; log_hash(); }
                if (world.state != was) {
                    zx_log("ace: state=%d t=%lu\n", (int)world.state, (unsigned long)ticks_played);
                    if (world.state == STATE_GAMEOVER) log_hash();
                }
            }
        }
        draw_views();
        front_hud(&world, autopilot);
        drawn = 1;
        perf_draws++;
        perf_frames += ticks;
        if (perf_frames >= 250) {
            zx_log("ace: perf fps10=%u\n", (unsigned)((uint32_t)perf_draws * 500 / perf_frames));
            perf_frames = perf_draws = 0;
        }
    }
}
