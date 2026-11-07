// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * DOT_CHASE - Game logic
 * Spider AI, movement, collision, scoring
 *
 * port: the original game.c for the ZX Spectrum. Same maze, rules, AI,
 * speeds and timings; changes are marked "port:". Movement and the
 * spiders' direction choice are the SDK's gridmove (the original's
 * move_entity / choose_direction, moved there to be shared).
 */
#include <string.h>
#include "game.h"

/* port: the game state is one global (at a fixed address, see game.h), and
   the original's "gs->" addresses it directly: zsdcc makes much smaller and
   faster code for that than for access through a pointer. The functions keep
   the original's GameState parameter, unused. */
#define gs (&game)
#pragma disable_warning 85      /* the unused GameState parameters */

const UWORD bonus_scores[MAX_BONUS_TYPE] = {
    150, 250, 400, 600, 900, 1500, 2500
};

#define R(y) &game.maze[y][0]
UBYTE *const maze_row[MAZE_H] = {
    R(0), R(1), R(2), R(3), R(4), R(5), R(6), R(7), R(8), R(9), R(10),
    R(11), R(12), R(13), R(14), R(15), R(16), R(17), R(18), R(19), R(20),
    R(21), R(22), R(23), R(24), R(25), R(26), R(27), R(28), R(29), R(30),
};
#undef R

/*
 * The hedge maze: 28x31
 * 1=hedge, 2=seed, 3=flower (power), 4=nest gate, 0=empty
 */
const UBYTE maze_template[MAZE_H][MAZE_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,2,1,2,1,1,1,1,2,1},
    {1,2,1,2,2,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,1,2,1},
    {1,2,1,2,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,2,1,2,1},
    {1,3,2,2,1,1,2,2,2,1,2,2,2,1,1,2,2,2,1,2,2,2,1,1,2,2,3,1},
    {1,1,1,2,1,1,1,1,2,1,2,1,1,1,1,1,1,2,1,2,1,1,1,1,2,1,1,1},
    {1,2,2,2,2,2,1,1,2,2,2,2,2,1,1,2,2,2,2,2,1,1,2,2,2,2,2,1},
    {1,2,1,1,1,2,1,1,1,1,1,1,2,1,1,2,1,1,1,1,1,1,2,1,1,1,2,1},
    {1,2,1,2,2,2,2,2,1,2,2,2,2,1,1,2,2,2,2,1,2,2,2,2,2,1,2,1},
    {1,2,1,2,1,1,1,2,1,2,1,1,1,1,1,1,1,1,2,1,2,1,1,1,2,1,2,1},
    {1,2,2,2,1,2,2,2,1,2,2,2,2,2,2,2,2,2,2,1,2,2,2,1,2,2,2,1},
    {1,1,1,2,1,2,1,1,1,2,1,1,1,4,4,1,1,1,2,1,1,1,2,1,2,1,1,1},
    {1,1,1,2,1,2,1,1,1,2,1,0,0,0,0,0,0,1,2,1,1,1,2,1,2,1,1,1},
    {2,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0,1,2,2,2,2,2,2,2,2,2,2},
    {1,1,1,2,1,2,1,1,1,2,1,0,0,0,0,0,0,1,2,1,1,1,2,1,2,1,1,1},
    {1,1,1,2,1,2,1,1,1,2,1,1,1,1,1,1,1,1,2,1,1,1,2,1,2,1,1,1},
    {1,2,2,2,1,2,2,2,2,2,1,2,2,2,2,2,2,1,2,2,2,2,2,1,2,2,2,1},
    {1,2,1,1,1,2,1,1,1,2,1,2,1,1,1,1,2,1,2,1,1,1,2,1,1,1,2,1},
    {1,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,1},
    {1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,2,1,2,1,1,1,1,1,2,1},
    {1,3,1,2,2,2,2,2,1,2,1,1,1,1,1,1,1,1,2,1,2,2,2,2,2,1,3,1},
    {1,2,1,2,1,1,1,2,1,2,1,1,1,1,1,1,1,1,2,1,2,1,1,1,2,1,2,1},
    {1,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,1},
    {1,1,1,2,1,2,1,1,1,1,1,2,1,1,1,1,2,1,1,1,1,1,2,1,2,1,1,1},
    {1,2,2,2,1,2,2,2,1,2,2,2,1,1,1,1,2,2,2,1,2,2,2,1,2,2,2,1},
    {1,2,1,1,1,1,1,2,1,2,1,2,2,1,1,2,2,1,2,1,2,1,1,1,1,1,2,1},
    {1,2,1,2,2,2,2,2,1,2,1,1,2,1,1,2,1,1,2,1,2,2,2,2,2,1,2,1},
    {1,2,1,2,1,1,1,1,1,2,1,1,2,1,1,2,1,1,2,1,1,1,1,1,2,1,2,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

/* The spiders' nest in the middle of the maze */
#define SPIDER_HOUSE_X  13
#define SPIDER_HOUSE_Y  14
#define SPIDER_DOOR_Y   12

/* Starting positions */
#define HERO_START_X    13
#define HERO_START_Y    23
#define HUNTER_START_X 13
#define HUNTER_START_Y 11
#define AMBUSH_START_X  13
#define AMBUSH_START_Y  14
#define FLANK_START_X   11
#define FLANK_START_Y   14
#define SHY_START_X  15
#define SHY_START_Y  14

/* Tunnel tiles (row 14) */
#define TUNNEL_Y       14

/* Direction vectors (port: the SDK's) */
#define dir_dx grid_dx
#define dir_dy grid_dy

/* Check if a tile is passable for ladybug */
static WORD tile_passable(UBYTE tile)
{
    return tile != T_WALL && tile != T_SPIDER_DOOR;
}

/* Check if a tile is passable for spiders */
static WORD tile_passable_spider(UBYTE tile, WORD state)
{
    if (tile == T_WALL) return 0;
    if (tile == T_SPIDER_DOOR && state == SPIDER_EATEN) return 1;
    if (tile == T_SPIDER_DOOR) return 0;
    return 1;
}

/* Get maze tile, with wrapping for tunnel */
static UBYTE get_tile(GameState *gs_, WORD tx, WORD ty)
{
    if (ty < 0 || ty >= MAZE_H) return T_WALL;
    if (tx < 0 || tx >= MAZE_W) {
        /* Tunnel row is passable */
        if (ty == TUNNEL_Y) return T_EMPTY;
        return T_WALL;
    }
    return maze_row[ty][tx];     /* port: row table */
}

/* Distance squared between two tiles (for spider targeting).
   port: 16 bits (every target is less than 100 tiles away) */
#define dist_sq grid_dist2

/* Opposite direction */
#define opposite_dir(d) GRID_OPPOSITE(d)

/* Convert tile to pixel */
#define tile_to_px(t) ((t) * TILE_SIZE)

/* Check if entity is aligned to tile center (port: grid_centered) */
#define is_centered(b) grid_centered(b)

void game_init(GameState *gs_)
{
    memset(gs, 0, sizeof(GameState));
    gs->lives = 3;
    gs->level = 1;
    gs->state = STATE_TITLE;
    gs->num_players = 1;
    gs->hiscore = 10000;     /* port: main.c sets it from the high-score table */
}

static void reset_maze(GameState *gs_)
{
    WORD x, y;
    gs->dots_total = 0;
    gs->dots_eaten = 0;
    gs->dots_eaten_global = 0;
    gs->ev_flags |= EV_LEVEL;    /* port */

    for (y = 0; y < MAZE_H; y++) {
        for (x = 0; x < MAZE_W; x++) {
            maze_row[y][x] = maze_template[y][x];
            if (maze_template[y][x] == T_DOT || maze_template[y][x] == T_POWER)
                gs->dots_total++;
        }
    }
}

static void place_spider(Spider *g, WORD tx, WORD ty, WORD in_house)
{
    grid_place(&g->b, tx, ty);
    g->dir = DIR_UP;
    g->next_dir = DIR_UP;
    g->state = SPIDER_SCATTER;
    g->home = in_house;
    g->fright_timer = 0;
}

void game_start_level(GameState *gs_)
{
    reset_maze(gs);
    game_start_life(gs);
    gs->mode = 0; /* scatter */
    gs->mode_timer = 0;
    gs->mode_phase = 0;
    gs->bonus_active = 0;
}

void game_start_life(GameState *gs_)
{
    /* Place ladybug */
    grid_place(&gs->hero.b, HERO_START_X, HERO_START_Y);
    gs->hero.dir = DIR_LEFT;
    gs->hero.next_dir = DIR_LEFT;
    gs->hero.anim_frame = 0;
    gs->hero.anim_timer = 0;
    gs->hero.leg_step = 1;

    /* Place spiders */
    place_spider(&gs->spiders[SPIDER_HUNTER], HUNTER_START_X, HUNTER_START_Y, 0);
    gs->spiders[SPIDER_HUNTER].dir = DIR_LEFT;
    gs->spiders[SPIDER_HUNTER].dot_counter = 0;

    place_spider(&gs->spiders[SPIDER_AMBUSH], AMBUSH_START_X, AMBUSH_START_Y, 1);
    gs->spiders[SPIDER_AMBUSH].dot_counter = 0;

    place_spider(&gs->spiders[SPIDER_FLANK], FLANK_START_X, FLANK_START_Y, 1);
    gs->spiders[SPIDER_FLANK].dot_counter = 30;

    place_spider(&gs->spiders[SPIDER_SHY], SHY_START_X, SHY_START_Y, 1);
    gs->spiders[SPIDER_SHY].dot_counter = 60;

    gs->fright_active = 0;
    gs->fright_timer = 0;
    gs->spiders_eaten = 0;
    gs->popup_active = 0;

    gs->state = STATE_READY;
    gs->state_timer = READY_TIME;
}

/* Move entity by speed, handling tile transitions.
   port: grid_move, which stops on a tile centre instead of stepping over it
   (the original's steps of 1-3 pixels skipped about one centre in five, and
   the ladybug or a spider then missed its turn and walked into the hedge) */
#define move_entity(b, dir, speed) grid_move(b, dir, speed, MAZE_W * TILE_SIZE)

/* Choose best direction toward target, never reversing.
   port: grid_choose (same order of preference and ties) with this test */
static UBYTE choose_eaten;
/* port: tile_passable_spider(get_tile(x, y), state), written out for speed */
static UBYTE spider_open(int16_t x, int16_t y)
{
    UBYTE t;
    if ((UWORD)y >= MAZE_H) return 0;
    if ((UWORD)x >= MAZE_W) return y == TUNNEL_Y;
    t = maze_row[y][x];
    return t != T_WALL && (t != T_SPIDER_DOOR || choose_eaten);
}

static WORD choose_direction(GameState *gs_, Spider *g, WORD target_x, WORD target_y)
{
    choose_eaten = g->state == SPIDER_EATEN;
    return grid_choose(g->b.tile_x, g->b.tile_y, (int8_t)g->dir, target_x, target_y, spider_open);
}

/* Spider leaves the spider house */
static void spider_leave_house(Spider *g)
{
    /* Move to door position */
    grid_place(&g->b, SPIDER_HOUSE_X, SPIDER_DOOR_Y);
    g->dir = DIR_LEFT;
    g->home = 0;
}

/* port: ((LONG)f * k + c) % m, C's remainder (the sign of the dividend),
   in 16 bits: f is the frame counter (a WORD, negative after 32767 frames),
   k, c, m small and positive; fm = |f| % m (worked out once a frame) */
static WORD lin_mod(WORD f, UBYTE fm, UBYTE k, UBYTE c, UBYTE m)
{
    UWORD a, t;
    if (f >= 0) return (WORD)((fm * k + c) % m);
    a = (UWORD)(-(LONG)f);                 /* 1..32768 */
    if (a <= 8 && a * k <= c) return (WORD)((c - a * k) % m);
    t = (fm * k + 2 * m - c) % m;          /* (a * k - c) mod m; c < 2m */
    return -(WORD)t;
}

static WORD fm_frame = -1;
static UBYTE fm_w, fm_h;                   /* |frame| % MAZE_W, % MAZE_H */

/* Update spider targeting */
static void update_spider_target(GameState *gs_, Spider *g, WORD id)
{
    if (g->state == SPIDER_SCATTER) {
        g->target_x = scatter_target[id][0];
        g->target_y = scatter_target[id][1];
        return;
    }

    if (g->state == SPIDER_EATEN) {
        /* Return to spider house */
        g->target_x = SPIDER_HOUSE_X;
        g->target_y = SPIDER_DOOR_Y;
        return;
    }

    if (g->state == SPIDER_FRIGHT) {
        /* Random-ish target: use frame counter for pseudo-random */
        /* port: (frame * k + c) % m as the Amiga's 32-bit int gives it */
        if (fm_frame != gs->frame) {
            UWORD a = gs->frame >= 0 ? (UWORD)gs->frame : (UWORD)(-(LONG)gs->frame);
            fm_frame = gs->frame;
            fm_w = a % MAZE_W;
            fm_h = a % MAZE_H;
        }
        g->target_x = lin_mod(gs->frame, fm_w, 7, id * 13, MAZE_W);
        g->target_y = lin_mod(gs->frame, fm_h, 11, id * 17, MAZE_H);
        return;
    }

    /* Chase mode: each spider has unique targeting */
    switch (id) {
    case SPIDER_HUNTER:
        /* Targets ladybug directly */
        g->target_x = gs->hero.b.tile_x;
        g->target_y = gs->hero.b.tile_y;
        break;

    case SPIDER_AMBUSH:
        /* Targets 4 tiles ahead of ladybug */
        g->target_x = gs->hero.b.tile_x + dir_dx[gs->hero.dir] * 4;
        g->target_y = gs->hero.b.tile_y + dir_dy[gs->hero.dir] * 4;
        break;

    case SPIDER_FLANK: {
        /* 2 tiles ahead of the ladybug, mirrored through the hunter */
        WORD ahead_x = gs->hero.b.tile_x + dir_dx[gs->hero.dir] * 2;
        WORD ahead_y = gs->hero.b.tile_y + dir_dy[gs->hero.dir] * 2;
        g->target_x = ahead_x + (ahead_x - gs->spiders[SPIDER_HUNTER].b.tile_x);
        g->target_y = ahead_y + (ahead_y - gs->spiders[SPIDER_HUNTER].b.tile_y);
        break;
    }

    case SPIDER_SHY: {
        /* Targets ladybug when far, scatter corner when close */
        LONG d = dist_sq(g->b.tile_x, g->b.tile_y,
                         gs->hero.b.tile_x, gs->hero.b.tile_y);
        if (d > 64) { /* > 8 tiles away */
            g->target_x = gs->hero.b.tile_x;
            g->target_y = gs->hero.b.tile_y;
        } else {
            g->target_x = scatter_target[SPIDER_SHY][0];
            g->target_y = scatter_target[SPIDER_SHY][1];
        }
        break;
    }
    }
}

/* At tile center: choose direction (the original update_spider's block) */
static void spider_centre(GameState *gs_, Spider *g, WORD id)
{
    /* Check if eaten spider reached home */
    if (g->state == SPIDER_EATEN) {
        if (g->b.tile_x == SPIDER_HOUSE_X &&
            (g->b.tile_y == SPIDER_DOOR_Y || g->b.tile_y == SPIDER_DOOR_Y + 1)) {
            g->state = gs->mode ? SPIDER_CHASE : SPIDER_SCATTER;
            g->b.pixel_y = tile_to_px(SPIDER_DOOR_Y);
            g->b.tile_y = SPIDER_DOOR_Y;
            g->b.sub_y = 0;
        }
    }

    update_spider_target(gs, g, id);
    g->dir = choose_direction(gs, g, g->target_x, g->target_y);
}

/* The original's update_spider. port: a macro for each spider's constant
   index, so that zsdcc addresses the spider's fields directly (through the
   pointer they cost it several times as much); the same steps */
#define update_spider(gs_, i) do { \
    Spider *g = &gs->spiders[i]; \
    /* Spider in house: wait for dot counter */ \
    if (g->home) { \
        if (gs->dots_eaten_global >= g->dot_counter) spider_leave_house(g); \
    } else { \
        WORD speed; \
        /* Determine speed */ \
        if (g->state == SPIDER_EATEN) speed = SPIDER_EATEN_SPD; \
        else if (g->state == SPIDER_FRIGHT) speed = SPIDER_FRIGHT_SPD; \
        else if (g->b.tile_y == TUNNEL_Y && (g->b.tile_x < 6 || g->b.tile_x >= 22)) \
            speed = SPIDER_TUNNEL_SPD;    /* in_tunnel */ \
        else speed = spider_speed;        /* level-dependent, below */ \
        if (is_centered(&g->b)) spider_centre(gs, g, i); \
        move_entity(&g->b, g->dir, speed); \
    } } while (0)

static void reverse_all_spiders(GameState *gs_)
{
    WORD i;
    for (i = 0; i < NUM_SPIDERS; i++) {
        if (!gs->spiders[i].home && gs->spiders[i].state != SPIDER_EATEN) {
            gs->spiders[i].dir = opposite_dir(gs->spiders[i].dir);
        }
    }
}

static void enter_fright_mode(GameState *gs_)
{
    WORD i;
    WORD fright_time = FRIGHT_TIME;

    /* Reduce fright time at higher levels */
    if (gs->level >= 3) fright_time = 250;
    if (gs->level >= 5) fright_time = 150;
    if (gs->level >= 8) fright_time = 50;
    if (gs->level >= 11) fright_time = 0; /* no fright at high levels */

    if (fright_time == 0) return;

    gs->fright_active = 1;
    gs->fright_timer = fright_time;
    gs->spiders_eaten = 0;

    for (i = 0; i < NUM_SPIDERS; i++) {
        if (gs->spiders[i].state != SPIDER_EATEN && !gs->spiders[i].home) {
            gs->spiders[i].state = SPIDER_FRIGHT;
            gs->spiders[i].fright_timer = fright_time;
            gs->spiders[i].dir = opposite_dir(gs->spiders[i].dir);
        }
    }
}

static void end_fright_mode(GameState *gs_)
{
    WORD i;
    gs->fright_active = 0;
    gs->fright_timer = 0;

    for (i = 0; i < NUM_SPIDERS; i++) {
        if (gs->spiders[i].state == SPIDER_FRIGHT) {
            gs->spiders[i].state = gs->mode ? SPIDER_CHASE : SPIDER_SCATTER;
            gs->spiders[i].fright_timer = 0;
        }
    }
}

/* Scatter/Chase mode timer */
static const WORD mode_durations[] = {     /* port: WORD */
    SCATTER1_TIME, CHASE1_TIME,
    SCATTER2_TIME, CHASE2_TIME,
    SCATTER3_TIME, CHASE3_TIME,
    SCATTER4_TIME, 0 /* permanent chase */
};
#define NUM_MODE_PHASES 8

static void update_mode(GameState *gs_)
{
    WORD i;

    if (gs->fright_active) return;

    gs->mode_timer++;

    if (gs->mode_phase < NUM_MODE_PHASES - 1) {
        if (gs->mode_timer >= mode_durations[gs->mode_phase]) {
            gs->mode_timer = 0;
            gs->mode_phase++;
            gs->mode = gs->mode_phase & 1; /* even=scatter, odd=chase */

            /* Reverse spider directions on mode change */
            reverse_all_spiders(gs);

            /* Update spider states */
            for (i = 0; i < NUM_SPIDERS; i++) {
                if (gs->spiders[i].state != SPIDER_EATEN &&
                    gs->spiders[i].state != SPIDER_FRIGHT &&
                    !gs->spiders[i].home) {
                    gs->spiders[i].state = gs->mode ? SPIDER_CHASE : SPIDER_SCATTER;
                }
            }
        }
    }
}

static void update_hero(GameState *gs_, InputState *input)
{
    WORD speed = HERO_SPEED;
    WORD next_tx, next_ty;
    UBYTE next_tile;

    /* Buffer direction input */
    if (input->dir != DIR_NONE) {
        gs->hero.next_dir = input->dir;
    }

    /* A little faster while the spiders are scared */
    if (gs->fright_active) speed += 16;

    /* At tile center: try to turn */
    if (is_centered(&gs->hero.b)) {
        /* Try buffered direction first */
        next_tx = gs->hero.b.tile_x + dir_dx[gs->hero.next_dir];
        next_ty = gs->hero.b.tile_y + dir_dy[gs->hero.next_dir];
        next_tile = get_tile(gs, next_tx, next_ty);

        if (tile_passable(next_tile)) {
            gs->hero.dir = gs->hero.next_dir;
        } else {
            /* Try to continue current direction */
            next_tx = gs->hero.b.tile_x + dir_dx[gs->hero.dir];
            next_ty = gs->hero.b.tile_y + dir_dy[gs->hero.dir];
            next_tile = get_tile(gs, next_tx, next_ty);

            if (!tile_passable(next_tile)) {
                /* Stop: wall ahead */
                return;
            }
        }

        /* Eat dots */
        {
            UBYTE *cell = &maze_row[gs->hero.b.tile_y][gs->hero.b.tile_x];
            UBYTE cur = *cell;
            gs->eat_x = gs->hero.b.tile_x;       /* port: for the front end */
            gs->eat_y = gs->hero.b.tile_y;
            if (cur == T_DOT) {
                *cell = T_EMPTY;
                gs->score += SCORE_DOT;
                gs->dots_eaten++;
                gs->dots_eaten_global++;
                gs->ev_flags |= EV_SEED;
            } else if (cur == T_POWER) {
                *cell = T_EMPTY;
                gs->score += SCORE_POWER;
                gs->dots_eaten++;
                gs->dots_eaten_global++;
                gs->ev_flags |= EV_POWER;
                enter_fright_mode(gs);
            }
        }

        /* Bonus trigger: at 70 and 170 dots */
        if (!gs->bonus_active &&
            (gs->dots_eaten == 70 || gs->dots_eaten == 170)) {
            gs->bonus_active = 1;
            gs->bonus_timer = 500; /* ~10 seconds */
            gs->bonus_type = (gs->level - 1) % MAX_BONUS_TYPE;
            gs->bonus_x = tile_to_px(13);
            gs->bonus_y = tile_to_px(17);
        }

        /* Check level complete */
        if (gs->dots_eaten >= gs->dots_total) {
            gs->state = STATE_LEVEL_DONE;
            gs->state_timer = LEVEL_DONE_TIME;
            return;
        }

        /* Extra life */
        if (!gs->extra_life_given && gs->score >= EXTRA_LIFE_SCORE) {
            gs->extra_life_given = 1;
            gs->lives++;
            gs->ev_flags |= EV_EXTRA;
        }
    }

    /* Animate legs */
    gs->hero.anim_timer++;
    if (gs->hero.anim_timer >= 3) {
        gs->hero.anim_timer = 0;
        gs->hero.leg_step = !gs->hero.leg_step;
    }

    move_entity(&gs->hero.b, gs->hero.dir, speed);
}

/* a spider touches the ladybug: 1 if the ladybug dies */
static UBYTE spider_touch(GameState *gs_, Spider *g)
{
    if (g->state == SPIDER_FRIGHT) {
        /* Eat spider */
        g->state = SPIDER_EATEN;
        g->fright_timer = 0;
        gs->spiders_eaten++;

        /* Score: 150, 300, 600, 1200 */
        {
            LONG spider_score = SCORE_SPIDER1;
            WORD e;
            for (e = 1; e < gs->spiders_eaten; e++) spider_score += spider_score;  /* port: *2 */
            gs->score += spider_score;
            gs->popup_active = 1;
            gs->popup_timer = EAT_SPIDER_TIME;
            gs->popup_x = g->b.pixel_x;
            gs->popup_y = g->b.pixel_y;
            gs->popup_score = spider_score;
        }

        gs->ev_flags |= EV_EAT_SPIDER;
        gs->state = STATE_EAT_SPIDER;
        gs->state_timer = EAT_SPIDER_TIME;
    } else if (g->state != SPIDER_EATEN) {
        /* ladybug dies */
        gs->state = STATE_DYING;
        gs->state_timer = DYING_TIME;
        gs->ev_flags |= EV_DIE;
        return 1;
    }
    return 0;
}

/* port: |dx| < 6 && |dy| < 6 as unsigned range tests, for a constant index */
#define TOUCHING(i) (!gs->spiders[i].home && \
    (UWORD)(gs->hero.b.pixel_x - gs->spiders[i].b.pixel_x + 5) <= 10 && \
    (UWORD)(gs->hero.b.pixel_y - gs->spiders[i].b.pixel_y + 5) <= 10)

/* port: the original's loop over the spiders, in order, unrolled */
static void check_spider_collision(GameState *gs_)
{
    if (TOUCHING(0) && spider_touch(gs, &gs->spiders[0])) return;
    if (TOUCHING(1) && spider_touch(gs, &gs->spiders[1])) return;
    if (TOUCHING(2) && spider_touch(gs, &gs->spiders[2])) return;
    if (TOUCHING(3)) spider_touch(gs, &gs->spiders[3]);
}

void game_update(GameState *gs_, InputState *input)
{
    WORD i;
    gs->frame++;

    switch (gs->state) {
    case STATE_TITLE:
        gs->title_blink++;
        if (input->start) {
            gs->lives = 3;
            gs->score = 0;
            gs->level = 1;
            gs->extra_life_given = 0;
            game_start_level(gs);
        }
        break;

    case STATE_READY:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_PLAYING:
        update_hero(gs, input);
        if (gs->state != STATE_PLAYING) break; /* level done or dying */

        update_mode(gs);

        {
            /* Speed up slightly with levels */
            WORD spider_speed = SPIDER_SPEED;
            if (gs->level >= 2) spider_speed += 16;
            if (gs->level >= 5) spider_speed += 16;
            update_spider(gs, 0);
            update_spider(gs, 1);
            update_spider(gs, 2);
            update_spider(gs, 3);
        }

        /* Fright timer */
        if (gs->fright_active) {
            gs->fright_timer--;
            if (gs->fright_timer <= 0) {
                end_fright_mode(gs);
            }
        }

        /* Bonus timer */
        if (gs->bonus_active) {
            gs->bonus_timer--;
            if (gs->bonus_timer <= 0) {
                gs->bonus_active = 0;
            }
            /* Check ladybug eats bonus */
            if (gs->bonus_active) {
                WORD dx = gs->hero.b.pixel_x - gs->bonus_x;
                WORD dy = gs->hero.b.pixel_y - gs->bonus_y;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx < 6 && dy < 6) {
                    gs->score += bonus_scores[gs->bonus_type];
                    gs->popup_active = 1;
                    gs->popup_timer = 60;
                    gs->popup_x = gs->bonus_x;
                    gs->popup_y = gs->bonus_y;
                    gs->popup_score = bonus_scores[gs->bonus_type];
                    gs->bonus_active = 0;
                    gs->ev_flags |= EV_BONUS;
                }
            }
        }

        /* Score popup timer */
        if (gs->popup_active) {
            gs->popup_timer--;
            if (gs->popup_timer <= 0) gs->popup_active = 0;
        }

        check_spider_collision(gs);

        /* Update pulse speed based on dots eaten */
        gs->pulse_speed = gs->dots_eaten * 3 / gs->dots_total;
        break;

    case STATE_EAT_SPIDER:
        gs->state_timer--;
        if (gs->popup_active) {
            gs->popup_timer--;
            if (gs->popup_timer <= 0) gs->popup_active = 0;
        }
        if (gs->state_timer <= 0) {
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_DYING:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->lives--;
            if (gs->lives <= 0) {
                if (gs->score > gs->hiscore) gs->hiscore = gs->score;
                gs->state = STATE_GAMEOVER;
                gs->state_timer = 150;
            } else {
                game_start_life(gs);
            }
        }
        break;

    case STATE_GAMEOVER:
        gs->state_timer--;
        gs->title_blink++;
        if (gs->state_timer <= 0 && input->start) {
            gs->state = STATE_TITLE;
        }
        break;

    case STATE_LEVEL_DONE:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->level++;
            game_start_level(gs);
        }
        break;
    }
}
