/*
 * main.c - Orb Hunter on the Spectrum Next: the original's main.c (the
 * title, play, the item screen, game over) around its rules (game.c).
 *
 * The original runs game_update() once a 50 Hz frame and counts frames in
 * gs.frame twice (game_update and the loop); here the rules run a step for
 * every frame that passed (frame_wait), so the game keeps its speed when a
 * frame takes longer (a room being drawn), and the picture follows.
 *
 * Memory: 0x0000-0x3FFF bank 30, the rules (game.c, over the ROM, which
 * isn't needed); 0x4000-0x7FFF bank 5 (the ULA's, unused), whose two pages
 * are windows for data (tiles, the world, the music, Layer 2); 0x8000-
 * the rest. The data's banks are in banks.h.
 * port: high scores with initials (the SDK's table) after game over; the
 * Amiga's bridge hooks are a probe (orb_probe) and log lines for the test.
 */
#include <string.h>
#include <arch/zxn.h>
#include <input.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "game.h"
#include "orb.h"
#include "banks.h"

GameState gs;
uint8_t show_frame;

/* what the test reads every frame (test.py) */
typedef struct {
    uint8_t state, room_x, room_y, alive;
    int16_t hx, hy, health, max_energy;
    int32_t score, items;
    uint8_t enemies, rolled, facing, on_ground;
    uint8_t invuln, fps, rank, missiles;
} Probe;
Probe orb_probe;

enum { S_TITLE, S_GAME, S_ITEM, S_OVER, S_NAME };      /* the shell's states */
static uint8_t shell = S_TITLE, title_frame;
static int8_t rank;
static hs_editor ed;
#define MAX_STEPS 8                     /* catch-up cap: a room takes a few frames to draw */

/* the original's input: left, right, up (jump, aim up), down (roll), fire */
#define I_LEFT  0x01
#define I_RIGHT 0x02
#define I_UP    0x04
#define I_DOWN  0x08
#define I_FIRE  0x10
#define I_START 0x20

static uint8_t keys(uint8_t *pressed)
{
    uint8_t in = input_read(), pr = input_pressed(), k = 0, p = 0;
    if (in & IN_LEFT) k |= I_LEFT;
    if (in & IN_RIGHT) k |= I_RIGHT;
    if (in & (IN_UP | IN_FIRE2)) k |= I_UP;                 /* Q, Z, cursor up, Kempston up */
    if (in & IN_DOWN) k |= I_DOWN;
    if (in & IN_FIRE) k |= I_FIRE;
    if (in & IN_START) k |= I_START;
    if (pr & IN_FIRE) p |= I_FIRE;
    if (pr & IN_START) p |= I_START;
    if (pr & (IN_UP | IN_FIRE2)) p |= I_UP;
    *pressed = p;
    return k | p;                                         /* a tap in a slow frame counts as held once */
}

static void to_title(void)
{
    shell = S_TITLE;
    title_frame = 0;
    draw_title();
    zx_log("state title\n");
}

static void start_game(void)
{
    game_init(&gs);
    show_game();
    draw_room_full();
    shell = S_GAME;
    zx_log("start room=%d,%d\n", (int)gs.room_x, (int)gs.room_y);
}

/* what changed in a step, for the log */
static void report(int32_t score, LONG health, LONG rx, LONG ry, WORD state, LONG items)
{
    if (gs.score != score) zx_log("score %ld\n", (long)gs.score);
    if (gs.hunter.health < health) zx_log("hit health=%ld\n", (long)gs.hunter.health);
    if (gs.room_x != rx || gs.room_y != ry) zx_log("room %ld,%ld\n", (long)gs.room_x, (long)gs.room_y);
    if (gs.items_collected != items) zx_log("item %s\n", gs.item_name);
    if (gs.state != state && gs.state == STATE_DEAD) zx_log("dead score=%ld\n", (long)gs.score);
}

static void probe(uint8_t fps)
{
    Probe *p = &orb_probe;
    uint8_t i, n = 0;
    for (i = 0; i < MAX_ENEMIES; i++) n += gs.enemies[i].active ? 1 : 0;
    p->state = shell == S_GAME ? (uint8_t)gs.state : 0x10 + shell;
    p->room_x = (uint8_t)gs.room_x;
    p->room_y = (uint8_t)gs.room_y;
    p->alive = (uint8_t)gs.hunter.alive;
    p->hx = FIX_INT(gs.hunter.x);
    p->hy = FIX_INT(gs.hunter.y);
    p->health = (int16_t)gs.hunter.health;
    p->max_energy = (int16_t)gs.hunter.max_energy;
    p->score = gs.score;
    p->items = gs.items_collected;
    p->enemies = n;
    p->rolled = (uint8_t)gs.hunter.rolled;
    p->facing = (uint8_t)gs.hunter.facing;
    p->on_ground = (uint8_t)gs.hunter.on_ground;
    p->invuln = (uint8_t)gs.hunter.invuln_timer;
    p->fps = fps;
    p->rank = (uint8_t)rank;
    p->missiles = (uint8_t)gs.hunter.missiles;
}

int main(void)
{
    uint8_t ticks, in, pressed, fps = 0, drawn = 0, k;
    uint16_t perf_frames = 0, perf_steps = 0;
    LONG room_shown_x = -1, room_shown_y = -1;

    next_init();
    ZXN_WRITE_MMU0(CODE_BANK * 2);              /* the rules */
    ZXN_WRITE_MMU1(CODE_BANK * 2 + 1);
    draw_init();
    snd_init();
    input_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    hs_init(50);                                /* 250, 200, 150, 100, 50 */
    snd_music(1);
    to_title();
    zx_log("orb: ready kempston=%u turbo=%u\n", input_kempston, next_reg_read(7) & 3);

    for (;;) {
        ticks = frame_wait();
        show_frame++;
        for (k = 0; k < ticks; k++) snd_tick();
        perf_frames += ticks;
        if (ticks > MAX_STEPS) ticks = MAX_STEPS;
        in = keys(&pressed);

        switch (shell) {
        case S_TITLE:                           /* draw_title: the prompt flashes every 20 frames */
            {
                uint8_t was = title_frame >= 20;
                title_frame += ticks;
                if (title_frame >= 40) title_frame -= 40;
                if ((title_frame >= 20) != was) draw_title_prompt(title_frame >= 20);
            }
            if (pressed & (I_FIRE | I_START)) {
                start_game();
                room_shown_x = gs.room_x;
                room_shown_y = gs.room_y;
            }
            break;

        case S_GAME:
            for (; ticks; ticks--) {
                int32_t score = gs.score;
                LONG health = gs.hunter.health, rx = gs.room_x, ry = gs.room_y, items = gs.items_collected;
                WORD state = gs.state;
                game_update(&gs, (in & I_LEFT) != 0, (in & I_RIGHT) != 0, (in & I_UP) != 0, (in & I_DOWN) != 0,
                            (in & I_UP) != 0, (in & I_FIRE) != 0);
                gs.frame++;                     /* main.c counts the frame too */
                perf_steps++;
                report(score, health, rx, ry, state, items);
                if (gs.state == STATE_ITEM_GET || gs.state == STATE_GAMEOVER) break;
            }
            if (gs.state == STATE_GAMEOVER) {
                shell = S_OVER;
                rank = hs_rank((uint32_t)gs.score);
                draw_gameover(2);
                zx_log("gameover score=%ld rank=%d\n", (long)gs.score, rank);
                break;
            }
            if (gs.room_x != room_shown_x || gs.room_y != room_shown_y) {
                draw_room_full();
                room_shown_x = gs.room_x;
                room_shown_y = gs.room_y;
            } else {
                draw_room_update();
            }
            draw_hud();
            draw_sprites();
            drawn++;
            if (gs.state == STATE_ITEM_GET) {
                draw_item_get();
                shell = S_ITEM;
            }
            break;

        case S_ITEM:                            /* the original: fire (held or not) carries on */
            gs.frame += ticks;
            if (in & (I_FIRE | I_START)) {
                gs.state = STATE_PLAYING;
                shell = S_GAME;
                draw_room_update();             /* the box goes */
            }
            break;

        case S_OVER:                            /* "PRESS FIRE TO CONTINUE" flashing */
            gs.frame += ticks;
            draw_gameover(((gs.frame / 20) & 1) ? 1 : 0);
            /* port: a new press (the original takes fire held from the game) */
            if (pressed & (I_FIRE | I_START)) {
                if (rank >= 0) {
                    shell = S_NAME;
                    hs_edit_begin(&ed);
                    draw_name_entry(ed.name, ed.pos, rank);
                    zx_log("hiscore rank=%d\n", rank);
                } else {
                    to_title();
                }
            }
            break;

        case S_NAME:
            k = hs_edit_step(&ed);
            if (k & HS_EDIT_TAKEN) sfx_hit();
            if (k & HS_EDIT_CHANGED) draw_name_entry(ed.name, ed.pos > 2 ? 2 : ed.pos, rank);
            if (k & HS_EDIT_DONE) {
                hs_insert(rank, ed.name, (uint32_t)gs.score);
                zx_log("name=%s score=%ld\n", ed.name, (long)gs.score);
                rank = -1;
                to_title();
            }
            break;
        }

        if (perf_frames >= 100) {
            fps = drawn / 2;
            zx_log("perf fps=%u steps=%u frames=%u shell=%u\n", fps, perf_steps, perf_frames, shell);
            zx_log("ay effects=%u music=%u\n", snd_sounding[0], snd_sounding[1]);
            snd_sounding[0] = snd_sounding[1] = 0;
            perf_frames = perf_steps = 0;
            drawn = 0;
        }
        probe(fps);
    }
}
