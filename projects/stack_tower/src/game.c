/*
 * game.c - Stack Tower on the Spectrum Next: the original's game.c (the
 * shell round the rules: input with the turn's auto-repeat, the title with
 * its demo game, play, game over, the help pages, sound for the events)
 * and the main loop.
 *
 * The loop is the original's: the rules run in steps of 40 ms (every
 * second frame, 25 a second) however long drawing takes, and a frame is
 * drawn after them; drawing is double buffered and shown at the next
 * vertical blank. port: the Amiga's bridge hooks are a probe (st_probe)
 * and log lines the test reads; high scores and initials are new (the SDK's
 * table, one a mode).
 */
#include <string.h>
#include <arch/zxn.h>
#include <input.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "gfx_data.h"
#include "stack.h"

Stack stack;
StGame game;
hs_editor hs_ed;

#define TITLE_ART_STEPS  250    /* 10 s of the painting, */
#define TITLE_DEMO_STEPS 375    /* then 15 s of the demo game */
#define MAX_STEPS 6             /* catch-up cap (a slow frame keeps its game time) */

/* the original's input bits (game.c IN_*) */
#define K_LEFT   0x0001
#define K_RIGHT  0x0002
#define K_UP     0x0004         /* rotate clockwise */
#define K_DOWN   0x0008         /* soft drop */
#define K_FIRE   0x0010         /* hard drop / start */
#define K_CCW    0x0020
#define K_HOLD   0x0040
#define K_GHOST  0x0080
#define K_PAUSE  0x0100
#define K_ESC    0x0200
#define K_START  0x0400
#define K_HELP   0x0800

/* what the test reads (test.py), every frame */
typedef struct {
    uint8_t state, mode, over, won;
    int8_t ptype, prot, pcol, prow;
    uint8_t level, help, paused, fps;
    int32_t score;
    uint16_t pieces, lines;
    int8_t hold, rank;
    uint16_t view;
} Probe;
Probe st_probe;

static uint32_t next_seed = 1;
static int8_t das_dir;
static uint8_t das_timer, demo_t, last_state = 0xFF, painting;
static hs_entry hs_tabs[MODE_COUNT][HS_ENTRIES];

/* amiga3d's a3d_hash (the demo's choices) */
static uint32_t a3_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

void hs_use_mode(uint8_t mode)
{
    memcpy(hs_table, hs_tabs[mode], sizeof(hs_table));
}

static void flash(const char *s)
{
    stack.flash = s;
    stack.flash_timer = 40;
}

static void handle_events(void)
{
    static const char *const names[] = { "", "RING!", "DOUBLE!", "TRIPLE!", "QUAD!", "PENTA!", "MEGA!" };
    StEvent e;
    while (st_pop_event(&game, &e)) {
        if (stack.state == SS_TITLE) continue;              /* the demo is quiet */
        snd_event(e.type, e.a);
        if (e.type == STE_CLEAR) flash(names[e.a < 7 ? e.a : 6]);
        if (e.type == STE_LEVEL) flash("LEVEL UP");
        if (e.type != STE_MOVE && e.type != STE_ROTATE)
            zx_log("ev %s a=%d b=%d score=%ld rings=%d level=%d\n", st_event_names[e.type], e.a, e.b,
                   (long)game.score, game.lines, game.level);
    }
}

/* ---------- game flow ---------- */
static void start_game(uint8_t mode, int16_t level)
{
    st_new_game(&game, mode, next_seed++, level, stack.view >> 8);
    stack.state = SS_PLAY;
    stack.paused = 0;
    stack.help = 0;
    das_dir = 0;
    painting = 0;
    scr_show_painting(0);
    tm_blank();
    zx_log("start %s level=%d seed=%lu\n", st_mode_names[mode], level, (unsigned long)game.seed);
}

static void to_title(void)
{
    stack.state = SS_TITLE;
    st_new_game(&game, MODE_MARATHON, 777 + next_seed, 1, 0);
    stack.title_frames = 0;
    tower_invalidate();
    hs_use_mode(stack.mode);
    panel_invalidate();
    title_painting_reset();
}

/* the title's demo: a piece placed every few steps, more or less sensibly
   (game.c demo_step). port: where a placement lands comes from the
   columns' heights (the original drops each one row by row) */
static void demo_step(void)
{
    uint8_t h[ST_COLS], c, r, k, n;
    int16_t best = 0x7FFF;
    int8_t best_col = 0, best_rot = 0, cells[5][2];
    if (++demo_t < 6) return;
    demo_t = 0;
    if (game.over) { st_new_game(&game, MODE_MARATHON, a3_hash(game.seed), 1, 0); return; }
    for (c = 0; c < ST_COLS; c++) {
        h[c] = 0;
        for (r = ST_TOTAL; r > 0; r--) if (game.board[((r - 1) << 4) | c]) { h[c] = r; break; }
    }
    for (r = 0; r < 4; r++)
        for (c = 0; c < ST_COLS; c++) {
            Piece p = game.piece;
            int8_t land = -8;
            int16_t score = 0;
            p.rot = r;
            p.col = c;
            if (st_collides(&game, &p)) continue;
            p.row = 0;
            n = st_cells_of(&p, cells);
            for (k = 0; k < n; k++) {
                int8_t need = (int8_t)h[cells[k][0]] - cells[k][1];
                if (cells[k][1] < 0 && -cells[k][1] > need) need = -cells[k][1];
                if (need > land) land = need;
            }
            for (k = 0; k < n; k++) score += (cells[k][1] + land) * 4;
            score += (int16_t)(a3_hash((uint32_t)(c * 4 + r + game.pieces * 64)) & 3);
            if (score < best) { best = score; best_col = c; best_rot = r; }
        }
    game.piece.rot = best_rot;
    game.piece.col = best_col;
    game.last_col = best_col;
    st_hard_drop(&game);
}

static void game_over(void)
{
    stack.state = SS_OVER;
    stack.state_timer = 40;
    hs_use_mode(game.mode);
    stack.rank = hs_rank((uint32_t)game.score);
    zx_log("over score=%ld rings=%d pieces=%d rank=%d\n", (long)game.score, game.lines, game.pieces, stack.rank);
}

/* ---------- input ---------- */
static uint16_t sdk_bits(uint8_t in)
{
    uint16_t k = 0;
    if (in & IN_LEFT) k |= K_LEFT;
    if (in & IN_RIGHT) k |= K_RIGHT;
    if (in & IN_UP) k |= K_UP;
    if (in & IN_DOWN) k |= K_DOWN;
    if (in & IN_FIRE) k |= K_FIRE;
    if (in & IN_FIRE2) k |= K_CCW;                          /* Z, the Kempston's second button */
    if (in & IN_START) k |= K_START | K_PAUSE;              /* ENTER: start, pause */
    if (in & IN_QUIT) k |= K_ESC;                           /* BREAK */
    return k;
}

/* held keys; *taps: the SDK's latched presses (a tap shorter than a slow
   frame) */
static uint16_t keys_now(uint16_t *taps)
{
    uint16_t k = sdk_bits(input_read());
    *taps = sdk_bits(input_pressed());
    if (key_down(IN_KEY_SCANCODE_x)) k |= K_UP;
    if (key_down(IN_KEY_SCANCODE_c)) k |= K_HOLD;
    if (key_down(IN_KEY_SCANCODE_g)) k |= K_GHOST;
    if (key_down(IN_KEY_SCANCODE_h)) k |= K_HELP;
    return k;
}

static void set_help(uint8_t page)
{
    stack.help = page;
    help_draw(page);
    if (!page) title_painting_reset();
    zx_log("help %u\n", page);
}

/* the original's game_input(): edges, and the held turn keys */
static void game_input(uint16_t in, uint16_t edge)
{
    if (stack.state == SS_NAME) return;                    /* the letters are initials */
    if (edge & K_HELP) { set_help((stack.help + 1) % 3); return; }
    if (stack.help) {
        if (edge & (K_FIRE | K_START | K_ESC)) set_help(0);
        return;
    }
    if (edge & K_ESC) {
        if (stack.state != SS_TITLE) { zx_log("quit\n"); to_title(); }
        return;
    }
    switch (stack.state) {
    case SS_TITLE:
        if (edge & K_LEFT) { stack.mode = (stack.mode + MODE_COUNT - 1) % MODE_COUNT; hs_use_mode(stack.mode); panel_invalidate(); }
        if (edge & K_RIGHT) { stack.mode = (stack.mode + 1) % MODE_COUNT; hs_use_mode(stack.mode); panel_invalidate(); }
        if ((edge & K_UP) && stack.start_level < 10) stack.start_level++;
        if ((edge & K_DOWN) && stack.start_level > 1) stack.start_level--;
        if (edge & (K_FIRE | K_START)) start_game(stack.mode, stack.start_level);
        break;
    case SS_PLAY:
        if (edge & K_PAUSE) { stack.paused = !stack.paused; zx_log("pause %u\n", stack.paused); }
        if (stack.paused) break;
        if (edge & K_GHOST) stack.ghost = !stack.ghost;
        if (edge & K_UP) st_rotate(&game, 1);
        if (edge & K_CCW) st_rotate(&game, -1);
        if (edge & K_HOLD) st_hold(&game);
        if (edge & K_FIRE) st_hard_drop(&game);
        st_soft_drop(&game, (in & K_DOWN) != 0);
        /* turning: one column on the press; held, it repeats (game_update) */
        {
            int8_t dir = (in & K_LEFT) ? -1 : (in & K_RIGHT) ? 1 : 0;
            if (dir && (edge & (K_LEFT | K_RIGHT))) { st_move(&game, dir); das_dir = dir; das_timer = 7; }
            else if (dir != das_dir) das_dir = 0;
        }
        break;
    case SS_OVER:
        if (stack.state_timer == 0 && (edge & (K_FIRE | K_START))) {
            if (stack.rank >= 0) {
                stack.state = SS_NAME;
                hs_edit_begin(&hs_ed);
                zx_log("hiscore rank=%d\n", stack.rank);
            } else to_title();
        }
        break;
    }
}

/* the initials (SS_NAME): the SDK's editor, typed letters or up/down + fire */
static void name_input(void)
{
    uint8_t r = hs_edit_step(&hs_ed);
    if (r & HS_EDIT_TAKEN) snd_event(STE_ROTATE, 0);
    if (r & HS_EDIT_DONE) {
        hs_insert(stack.rank, hs_ed.name, (uint32_t)game.score);
        memcpy(hs_tabs[game.mode], hs_table, sizeof(hs_table));
        zx_log("name=%s score=%ld\n", hs_ed.name, (long)game.score);
        stack.mode = game.mode;
        to_title();
    }
}

/* one 40 ms step of the original's game_update() */
static void game_step(void)
{
    if (stack.help || stack.paused) return;
    if (stack.state == SS_TITLE) {
        demo_step();
        if (++stack.title_frames >= TITLE_ART_STEPS + TITLE_DEMO_STEPS) stack.title_frames = 0;
    }
    if (stack.state == SS_PLAY && das_dir && --das_timer == 0) {     /* held: repeat after 0.28 s */
        st_move(&game, das_dir);
        das_timer = 1;
    }
    st_tick(&game, 40);
    handle_events();
    if (stack.flash_timer) stack.flash_timer--;
    if (stack.state_timer) stack.state_timer--;
    if (stack.state == SS_PLAY && (game.over || game.won)) game_over();
    /* the tower turns towards the piece's column, the short way round */
    {
        int16_t d = (int16_t)((((uint16_t)game.piece.col << 8) - stack.view + 8 * 256) & (16 * 256 - 1)) - 8 * 256;
        int16_t m = d / 3;
        stack.view = (stack.view + (m ? m : d)) & (16 * 256 - 1);
    }
}

static void probe(uint8_t fps)
{
    Probe *p = &st_probe;
    p->state = stack.state;
    p->mode = game.mode;
    p->over = game.over;
    p->won = game.won;
    p->ptype = game.piece.type;
    p->prot = game.piece.rot;
    p->pcol = game.piece.col;
    p->prow = game.piece.row;
    p->level = (uint8_t)game.level;
    p->help = stack.help;
    p->paused = stack.paused;
    p->fps = fps;
    p->score = game.score;
    p->pieces = game.pieces;
    p->lines = game.lines;
    p->hold = game.hold;
    p->rank = stack.rank;
    p->view = stack.view;
}

int main(void)
{
    uint16_t in, last = 0, edge, perf_frames = 0, perf_drawn = 0, perf_steps = 0, perf_bricks = 0;
    uint8_t ticks, acc = 0, steps, drawn = 0, fps = 0, m;

    next_init();
    scr_init();
    snd_init();
    input_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    for (m = 0; m < MODE_COUNT; m++) {
        hs_init(m == MODE_DIG ? 400 : 1000);
        memcpy(hs_tabs[m], hs_table, sizeof(hs_table));
    }
    stack.ghost = 1;
    stack.start_level = 1;
    to_title();
    snd_music(1);
    zx_log("stack: ready kempston=%u turbo=%u\n", input_kempston, next_reg_read(7) & 3);

    for (;;) {
        ticks = frame_wait();
        if (drawn) {                            /* the frame drawn last time */
            scr_flip();
            tower_copper();
        }
        for (m = 0; m < ticks; m++) snd_tick();
        perf_frames += ticks;

        in = keys_now(&edge);
        edge |= in & ~last;
        last = in;
        if (stack.state == SS_NAME) name_input();
        else game_input(in, edge);

        acc += ticks;
        steps = acc >> 1;
        acc &= 1;
        if (steps > MAX_STEPS) steps = MAX_STEPS;
        perf_steps += steps;
        while (steps--) game_step();

        if (stack.state != last_state) {
            static const char *const st[] = { "title", "play", "over", "name" };
            zx_log("state %s score=%ld\n", st[stack.state], (long)game.score);
            last_state = stack.state;
        }

        /* the title: the painting, then the demo game */
        {
            uint8_t want = stack.state == SS_TITLE && !stack.help && stack.title_frames < TITLE_ART_STEPS;
            if (want != painting) {
                painting = want;
                tower_guide_off();
                scr_show_painting(want);
                if (want) title_painting_reset();
                else tm_blank();
            }
            if (painting) title_painting_text();
        }
        if (painting || stack.help) big_words(0);

        drawn = 0;
        if (!painting && !stack.help) {
            tower_draw(stack.state != SS_OVER && stack.state != SS_NAME && !game.over,
                       stack.state == SS_PLAY && !game.over);
            view_text();
            panel_draw();
            big_words(1);
            drawn = 1;
            perf_drawn++;
            perf_bricks += tower_bricks;
        }
        if (perf_frames >= 100) {
            fps = (uint8_t)(perf_drawn / 2);
            zx_log("perf fps=%u steps=%u frames=%u bricks=%u redraws=%u state=%u\n", fps, perf_steps, perf_frames,
                   perf_drawn ? perf_bricks / perf_drawn : 0, tower_redraws, stack.state);
            perf_frames = perf_drawn = perf_steps = perf_bricks = tower_redraws = 0;
        }
        probe(fps);
    }
}
