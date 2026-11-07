/*
 * Block Breaker 3D for the Spectrum Next: memory map, start-up, input, the
 * game flow and the loop (the original's game.c).
 *
 *   0x0000-0x3FFF  bank 40: the rules (bb_core.c) while the logic runs;
 *                  bank 41: the renderer (render.c) while drawing
 *   0x4000-0x5FFF  bank 5's first half: the tilemap; a window (Layer 2
 *                  pages, sprite patterns, sound streams) while drawing
 *   0x6000-0x7FFF  page 84: the HUD (hud.c); page 85: the self-test; a
 *                  window (the depth plane, the clean tunnel) while drawing
 *   0x8000-0xEFFF  this, the SDK, the variables; 0xF000- the IM 2 table, the stack
 *   banks 20-24    Layer 2 (320 x 256): the tunnel and the bricks
 *   banks 25-29    the tunnel without bricks; 30-34 the depth plane
 *   bank 35        the sprite patterns; 36-37 the music and effects
 *   (memmap.h; this file runs with bank 40 mapped and maps bank 41 around
 *   calls into render.c)
 *
 * The rules (bb_core.c) run at the original's 25 ticks a second (one tick
 * every two frames, fixed timestep: up to four ticks catch up after a slow
 * frame); the picture is drawn every frame, halfway between the last two
 * ticks on the frame between them, so things move at 50 Hz.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include <input.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "breaker.h"
#include "sound_data.h"
#include "memmap.h"

#define L2_TUNNEL_BANK 20                /* gfx_data.h L2_BANK (that header is bank 41's) */

Breaker br;
BBGame game;
Before before;
hs_editor entry_ed;
static int8_t entry_place;
static uint8_t play_in, fire_lock, last_state = 0xFF, keys_prev;
static uint32_t next_seed = 1;

/* what test.py reads (by symbol) */
struct {
    uint8_t state, level, lives, stuck, between, over, won, autoplay;
    int16_t bx, by, bz, px, py;
    int32_t score;
    uint8_t left, fps, help, paused;
    uint16_t broken, paddle_hits, overruns, lines;
    uint8_t caps, parts, wide, plough;
    int16_t vx, vy, vz;                 /* 1/256 units a tick */
    uint8_t start_level, entry_pos;
} probe;

volatile uint8_t selftest_req;          /* test.py sets it: run the original's tests */
uint16_t selftest_pass, selftest_fail;

/* a3_rand (the original's LCG) */
static uint16_t a3_rand(uint32_t *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (uint16_t)((*state >> 16) & 0x7FFF);
}

/* a3_hash */
static uint32_t a3_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

/* render.c (bank 41) from here (bank 40 mapped) */
static void draw_dirty(int16_t x0, int16_t y0, int16_t x1, int16_t y1) { MAP_DRAW(); scene_dirty(x0, y0, x1, y1); MAP_LOGIC(); }
static void draw_box(uint8_t edge) { MAP_DRAW(); scene_box(60, 90, 260, 170, edge); MAP_LOGIC(); }
static void draw_blank(void) { MAP_DRAW(); scene_blank(); MAP_LOGIC(); }
static void draw_scene(uint8_t half) { MAP_DRAW(); scene_draw(&game, half); MAP_LOGIC(); }

static void flash(const char *s)
{
    strncpy(br.flash, s, sizeof(br.flash) - 1);
    br.flash_timer = 40;
}

/* ------------------------------------------------------------ bits of bricks */

static void burst(int cell, int type)
{
    int32_t x0, y0, z0;
    uint8_t k, made = 0;
    bb_cell_box(cell, &x0, &y0, &z0);
    for (k = 0; k < MAX_PARTS && made < 6; k++) {
        Part *p = &br.part[k];
        if (p->life > 0) continue;
        p->x = (int16_t)((x0 + BB_CW / 2) * 16);
        p->y = (int16_t)((y0 + BB_CH / 2) * 16);
        p->z = (int16_t)(z0 * 16);
        p->vx = (int16_t)(a3_rand(&next_seed) % 129) - 64;
        p->vy = (int16_t)(a3_rand(&next_seed) % 129) - 40;
        p->vz = -(int16_t)(a3_rand(&next_seed) % 64) - 24;
        p->life = 14 + made;
        p->type = (uint8_t)type;
        p->shade = made & 1;
        made++;
    }
}

static void parts_step(void)
{
    uint8_t k;
    for (k = 0; k < MAX_PARTS; k++) {
        Part *p = &br.part[k];
        if (p->life <= 0) continue;
        p->x += p->vx; p->y += p->vy; p->z += p->vz;
        p->vy -= 8;
        p->life--;
    }
}

/* ------------------------------------------------------------ events */

static void handle_events(void)
{
    BBEvent e;
    uint8_t quiet = br.state == BS_TITLE;       /* the demo is quiet */
    while (bb_pop_event(&game, &e)) {
        if (e.type == BBE_BREAK) burst((int)e.a, e.b);
        if (quiet) continue;
        switch (e.type) {
        case BBE_WALL: snd_play(SFX_WALL); break;
        case BBE_PADDLE: snd_play(SFX_PADDLE); break;
        case BBE_BREAK: snd_play(SFX_BREAK); break;
        case BBE_HIT: snd_play(SFX_HIT); break;
        case BBE_STEEL: snd_play(SFX_STEEL); break;
        case BBE_LOST: snd_play(SFX_LOST); if (game.lives > 0) flash("BALL LOST"); break;
        case BBE_SERVE: snd_play(SFX_SERVE); break;
        case BBE_CAPSULE: {
            static const char *const msg[CAP_COUNT] = { "WIDE PADDLE", "SLOW BALL", "EXTRA LIFE", "PLOUGH!" };
            snd_play(SFX_CAPSULE);
            flash(msg[(uint8_t)e.a]);
            break;
        }
        case BBE_CLEAR: snd_play(SFX_CLEAR); break;
        case BBE_OVER: snd_play(SFX_OVER); break;
        case BBE_WIN: snd_play(SFX_WIN); break;
        }
        if (e.type != BBE_WALL && e.type != BBE_PADDLE && e.type != BBE_HIT && e.type != BBE_STEEL && e.type != BBE_BREAK)
            zx_log("bb: EV %s a=%ld score=%ld level=%d lives=%d\n", bb_event_names[e.type], (long)e.a,
                   (long)game.score, game.level, game.lives);
    }
}

/* ------------------------------------------------------------ the flow */

static void start_game(uint8_t level)
{
    bb_new_game(&game, next_seed++, level);
    br.state = BS_PLAY;
    br.paused = 0;
    br.help = 0;
    br.autoplay = 0;
    fire_lock = 1;
    snd_music(2);
    zx_log("bb: START level %d seed %ld\n", level, (long)game.seed);
}

static void to_title(void)
{
    if (br.state == BS_OVER || br.state == BS_ENTRY) draw_dirty(60, 90, 261, 171);    /* the box goes */
    br.state = BS_TITLE;
    br.paused = 0;
    bb_new_game(&game, 777 + next_seed, 1);
    snd_music(1);
}

static void save_before(void)
{
    uint8_t k;
    before.bx = game.bx; before.by = game.by; before.bz = game.bz;
    before.px = game.px; before.py = game.py;
    for (k = 0; k < BB_MAX_CAPS; k++) {
        before.cx[k] = game.cap[k].x; before.cy[k] = game.cap[k].y; before.cz[k] = game.cap[k].z;
    }
}

/* one tick of 40 ms (the original's game_update loop body) */
static void tick(void)
{
    save_before();
    if (br.help || br.paused) return;
    if (br.state == BS_TITLE) {
        if (game.over || game.won) bb_new_game(&game, a3_hash(game.seed), 1 + (int)(game.seed % 3));
        bb_tick(&game, bb_bot(&game));
    } else if (br.state == BS_PLAY) {
        bb_tick(&game, br.autoplay ? bb_bot(&game) : play_in);
    }
    handle_events();
    parts_step();
    if (br.flash_timer > 0) br.flash_timer--;
    if (br.state_timer > 0) br.state_timer--;
    if (br.state == BS_PLAY && (game.over || game.won)) {
        br.state = BS_OVER;
        br.state_timer = 40;
        if (game.score > br.best) br.best = game.score;
        draw_box(game.won ? 17 : 11);                           /* render.c's g_box, green or red */
        zx_log("bb: GAMEOVER score=%ld level=%d won=%d broken=%d\n", (long)game.score, game.level, game.won, game.broken);
    }
    if (br.state == BS_OVER && br.state_timer == 0) {
        entry_place = hs_rank((uint32_t)game.score);
        if (entry_place >= 0) {             /* port: the initials, with the SDK's editor */
            br.state = BS_ENTRY;
            hs_edit_begin(&entry_ed);
            zx_log("bb: HISCORE rank=%d\n", entry_place);
        }
    }
}

/* the keys of a frame (the original's game_input); port: the Spectrum's
   keys (QAOP / cursors / Sinclair / Kempston, SPACE fire, ENTER pause,
   B autopilot, H help, BREAK back to the title) */
static void input(void)
{
    uint8_t in = input_read(), edge = input_pressed(), keys = 0, kedge;
    if (key_down(IN_KEY_SCANCODE_h)) keys |= 1;
    if (key_down(IN_KEY_SCANCODE_b)) keys |= 2;
    kedge = keys & ~keys_prev;
    keys_prev = keys;
    play_in = 0;

    if (br.state == BS_ENTRY) {
        uint8_t r = hs_edit_step(&entry_ed);
        if (r & HS_EDIT_TAKEN) snd_play(SFX_KEY);
        if (r & HS_EDIT_DONE) {
            hs_insert(entry_place, entry_ed.name, (uint32_t)game.score);
            zx_log("bb: NAME %s\n", entry_ed.name);
            to_title();
            hud_title_page = 1;             /* show the table */
        }
        return;
    }
    if (kedge & 1) {
        if (!br.help) draw_blank();
        br.help = (br.help + 1) % 3;
        return;
    }
    if (br.help) {
        if (edge & (IN_FIRE | IN_START | IN_QUIT)) br.help = 0;
        return;
    }
    if (edge & IN_QUIT) {
        if (br.state != BS_TITLE) to_title();
        return;
    }
    switch (br.state) {
    case BS_TITLE:
        if ((edge & IN_UP) && br.start_level < BB_LEVELS) br.start_level++;
        if ((edge & IN_DOWN) && br.start_level > 1) br.start_level--;
        if (edge & (IN_FIRE | IN_START)) start_game(br.start_level);
        break;
    case BS_PLAY:
        if (edge & IN_START) br.paused = !br.paused;
        if (kedge & 2) br.autoplay = !br.autoplay;
        play_in = (in & IN_LEFT ? BB_IN_LEFT : 0) | (in & IN_RIGHT ? BB_IN_RIGHT : 0) | (in & IN_UP ? BB_IN_UP : 0) |
                  (in & IN_DOWN ? BB_IN_DOWN : 0) | (in & IN_FIRE && !fire_lock ? BB_IN_FIRE : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case BS_OVER:
        if (br.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
}

/* ------------------------------------------------------------ self-test */

extern int run_tests(void (*check)(int cond, const char *name));

static void st_check(int cond, const char *name)
{
    if (cond) selftest_pass++;
    else {
        selftest_fail++;
        zx_log("bb: TEST FAIL %s\n", name);
    }
}

static void selftest(void)
{
    selftest_pass = selftest_fail = 0;
    zx_log("bb: SELFTEST start\n");
    ZXN_WRITE_MMU3(TEST_PAGE);
    run_tests(st_check);
    ZXN_WRITE_MMU3(HUD_PAGE);
    zx_log("bb: SELFTEST %u passed, %u failed\n", selftest_pass, selftest_fail);
    selftest_req = 0;
}

/* ------------------------------------------------------------ the loop */

static void update_probe(uint8_t fps)
{
    probe.state = br.state; probe.level = (uint8_t)game.level; probe.lives = (uint8_t)game.lives;
    probe.stuck = (uint8_t)game.stuck; probe.between = game.between != 0; probe.over = (uint8_t)game.over;
    probe.won = (uint8_t)game.won; probe.autoplay = br.autoplay;
    probe.bx = (int16_t)(game.bx / BB_FX); probe.by = (int16_t)(game.by / BB_FX); probe.bz = (int16_t)(game.bz / BB_FX);
    probe.px = (int16_t)(game.px / BB_FX); probe.py = (int16_t)(game.py / BB_FX);
    probe.score = game.score; probe.left = (uint8_t)game.left; probe.fps = fps;
    probe.help = br.help; probe.paused = br.paused;
    probe.broken = (uint16_t)game.broken; probe.paddle_hits = (uint16_t)game.paddle_hits;
    probe.overruns = frame_overruns; probe.lines = scene_lines;
    probe.wide = game.wide_t != 0; probe.plough = game.plough_t != 0;
    probe.vx = (int16_t)game.vx; probe.vy = (int16_t)game.vy; probe.vz = (int16_t)game.vz;
    probe.start_level = br.start_level; probe.entry_pos = entry_ed.pos;
    {
        uint8_t k, n = 0;
        for (k = 0; k < BB_MAX_CAPS; k++) n += game.cap[k].active != 0;
        probe.caps = n;
        for (k = 0, n = 0; k < MAX_PARTS; k++) n += br.part[k].life > 0;
        probe.parts = n;
    }
}

int main(void)
{
    uint8_t acc = 0, fps = 0, draws = 0;
    uint16_t fps_frames = 0;
    next_init();
    ZXN_WRITE_MMU3(HUD_PAGE);
    hud_init();                         /* copies the ROM font: the ROM is still in */
    dma_copy_banks(25, L2_TUNNEL_BANK, 5);   /* the clean tunnel (these map slots 0 and 1) */
    dma_fill_banks(30, 0xFF, 5);        /* the depth plane: nothing drawn */
    MAP_DRAW();
    scene_init();
    MAP_LOGIC();
    input_init();
    snd_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    hs_init(500);
    br.start_level = 1;
    to_title();
    zx_log("bb: ready kempston=%u\n", input_kempston);

    for (;;) {
        uint8_t n = frame_wait(), k;
        for (k = 0; k < n; k++) snd_tick();
        fps_frames += n;
        if (selftest_req && br.state == BS_TITLE) {
            selftest();
            acc = 0;
            n = frame_wait();
            continue;
        }
        input();
        acc += n;
        if (acc > 8) acc = 8;
        while (acc >= 2) {
            acc -= 2;
            tick();
        }
        if (!br.help) draw_scene(acc);
        hud_frame(&game);
        draws++;
        if (fps_frames >= 250) {            /* the frames drawn a second */
            fps = (uint8_t)((uint16_t)draws * 50 / fps_frames);
            zx_log("bb: fps=%u lines=%u state=%u overruns=%u ay=%u,%u,%u,%u\n", fps, scene_lines, br.state, frame_overruns,
                   snd_reg(0, 7), snd_reg(0, 9), snd_reg(1, 7), snd_reg(1, 8));
            draws = 0;
            fps_frames = 0;
            if (br.state == BS_TITLE) hud_title_page ^= 1;
        }
        if (br.state != last_state) {
            static const char *const st[] = { "TITLE", "PLAY", "OVER", "ENTRY" };
            zx_log("bb: STATE %s score=%ld\n", st[br.state], (long)game.score);
            last_state = br.state;
        }
        update_probe(fps);
    }
    return 0;
}
