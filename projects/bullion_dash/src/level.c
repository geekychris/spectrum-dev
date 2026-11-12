/*
 * Bullion Dash - Level management (the original's level.c)
 *
 * port: the 150 levels are the original's (src/levels.txt, extracted from
 * its level.c by tools/extract_levels.py), packed by the SDK's gridpack into
 * ~10.9 KB (from 33.6 KB of nibbles). The original's brick_timer[28][16],
 * scanned every frame, is a list of the dug bricks (gs.holes): bricks that
 * come back in the same frame are handled in the order they were dug rather
 * than column by column. The text save/load of the editor is in editor.c.
 */
#include <string.h>
#include "level.h"
#include "enemy.h"
#include "game.h"
#include "render.h"
#include "gridbfs.h"
#include "levels_data.h"
#include "memmap.h"
#include "coords.h"

/* the guards' pathfinding field (gridbfs) */
static uint8_t path_cells[GB_CELLS(GRID_COLS, GRID_ROWS)];
#ifdef __SDCC
#define path_queue ((uint16_t *)HI_PATHQ)          /* a 256-byte page */
#else
static uint16_t path_queue[GB_QUEUE / 2];          /* host builds (tools/hostsim.c) */
#endif

/* port: row addresses (no multiply per lookup); at HI_TILEROW on the Spectrum */
#ifndef __SDCC
uint8_t *tile_row[GRID_ROWS];
#endif

uint8_t tile_at(int16_t x, int16_t y)
{
    if ((uint16_t)x >= GRID_COLS || (uint16_t)y >= GRID_ROWS)
        return TILE_SOLID;      /* out of bounds is solid */
    return tile_row[y][x];
}

/* a cell's flags for gridbfs: the moves the original's BFS allows */
static void path_flags(int16_t x, int16_t y)
{
    uint8_t t = TILE(x, y), f = GB_D;           /* down: always */
    if (enemy_can_enter(t)) f |= GB_IN;
    if (enemy_has_support(x, y) || t == TILE_BAR) f |= GB_L | GB_R;
    if (t == TILE_LADDER) f |= GB_U;            /* up: only on a ladder */
    gb_set((uint8_t)x, (uint8_t)y, f);
}

void tile_set(int16_t x, int16_t y, uint8_t t)
{
    TILE(x, y) = t;
    render_tile_changed((uint8_t)x, (uint8_t)y);
    path_flags(x, y);
    if (y > 0) path_flags(x, y - 1);            /* its support changed */
}

void level_refresh_paths(void)
{
    int16_t x, y;
    for (y = 0; y < GRID_ROWS; y++)
        for (x = 0; x < GRID_COLS; x++)
            path_flags(x, y);
}

void level_init(void)
{
    uint8_t y;
    for (y = 0; y < GRID_ROWS; y++) tile_row[y] = gs.tiles[y];
    gb_init(path_cells, path_queue, GRID_COLS, GRID_ROWS);
    coords_init();
}

static void reset_entities(void)
{
    Player *p = &gs.player;
    uint8_t i;

    memset(gs.holes, 0, sizeof(gs.holes));
    gs.holes_used = 0;
    for (i = 0; i < MAX_ENEMIES; i++)
        gs.enemies[i].active = 0;
    p->dir = DIR_NONE;
    p->state = PS_IDLE;
    p->anim = 0;
    p->dig_timer = 0;
    p->fall_count = 0;
}

/* TILE_PLAYER / TILE_ENEMY markers -> the player and the enemies (column by
   column, as the original numbers its enemies) */
static void spawn_from_markers(void)
{
    int16_t x, y, ne = 0;

    for (x = 0; x < GRID_COLS; x++) {
        for (y = 0; y < GRID_ROWS; y++) {
            uint8_t t = TILE(x, y);
            if (t == TILE_PLAYER) {
                gs.player.gx = x;
                gs.player.gy = y;
                gs.player.px = x * TILE_W;
                gs.player.py = y * TILE_H;
                TILE(x, y) = TILE_EMPTY;
            } else if (t == TILE_ENEMY) {
                if (ne < MAX_ENEMIES) {
                    Enemy *e = &gs.enemies[ne];
                    e->gx = x;
                    e->gy = y;
                    e->px = x * TILE_W;
                    e->py = y * TILE_H;
                    e->active = 1;
                    e->state = ES_RUNNING;
                    e->has_gold = 0;
                    e->dir = DIR_LEFT;
                    e->anim = 0;
                    e->trap_timer = 0;
                    e->move_timer = 0;
                    ne++;
                }
                /* always strip the marker, even past MAX_ENEMIES */
                TILE(x, y) = TILE_EMPTY;
            }
        }
    }
    gs.num_enemies = ne;
    /* fix: every guard waits ENEMY_START_DELAY frames before its first step;
       one placed on the player's row enters from the top instead */
    for (x = 0; x < ne; x++) {
        gs.enemies[x].move_timer = ENEMY_START_DELAY;
        if (gs.enemies[x].gy == gs.player.gy)
            gs.enemies[x].state = ES_DEAD;      /* enters from the top, as a respawn */
    }
}

void level_activate_spawn_markers(void)
{
    reset_entities();
    spawn_from_markers();
    level_refresh_paths();
    gs.hidden_left = level_count_hidden();
}

void level_load(int16_t level_num)
{
    if (level_num < 1) level_num = 1;
    if (level_num > MAX_LEVELS) level_num = MAX_LEVELS;
    gs.level_num = level_num;

    reset_entities();
    gp_unpack(&lv_set, level_num - 1, &gs.tiles[0][0]);
    spawn_from_markers();
    level_refresh_paths();

    gs.gold_total = level_count_gold();
    gs.gold_collected = 0;
    gs.hidden_left = level_count_hidden();
}

void level_tick_bricks(void)
{
    Hole *h = gs.holes;
    uint8_t n;
    int16_t i;

    for (n = gs.holes_used; n; n--, h++) {
        uint8_t x = h->x, y = h->y;
        if (!h->timer || --h->timer) continue;
        /* an enemy in the cell dies (and respawns); its gold drops above */
        for (i = 0; i < gs.num_enemies; i++) {
            Enemy *e = &gs.enemies[i];
            if (e->active && e->gx == x && e->gy == y) {
                e->state = ES_DEAD;
                if (e->has_gold) {
                    if (y > 0 && TILE(x, y - 1) == TILE_EMPTY)
                        tile_set(x, y - 1, TILE_GOLD);
                    e->has_gold = 0;
                }
            }
        }
        /* the player in the cell dies */
        if (gs.player.gx == x && gs.player.gy == y)
            gs.player.state = PS_DEAD;
        tile_set(x, y, TILE_BRICK);
    }
}

void level_reveal_hidden_ladders(void)
{
    int16_t x, y;
    for (y = 0; y < GRID_ROWS; y++)
        for (x = 0; x < GRID_COLS; x++)
            if (TILE(x, y) == TILE_HIDDEN_LADDER)
                tile_set(x, y, TILE_LADDER);
}

uint8_t level_dig_brick(int16_t gx, int16_t gy)
{
    Hole *h = gs.holes;
    uint8_t n;

    if ((uint16_t)gx >= GRID_COLS || (uint16_t)gy >= GRID_ROWS)
        return 0;
    if (TILE(gx, gy) != TILE_BRICK)
        return 0;
    for (n = MAX_HOLES; n; n--, h++) {
        if (!h->timer) {
            h->x = (uint8_t)gx;
            h->y = (uint8_t)gy;
            h->timer = BRICK_REGEN_FRAMES;
            n = MAX_HOLES + 1 - n;                  /* slots in use */
            if (n > gs.holes_used) gs.holes_used = n;
            tile_set(gx, gy, TILE_EMPTY);
            return 1;
        }
    }
    return 0;                   /* port: no free slot (never more than 15 dug at once) */
}

uint8_t level_count_hidden(void)
{
    const uint8_t *t = &gs.tiles[0][0];
    int16_t n;
    uint8_t count = 0;
    for (n = GRID_COLS * GRID_ROWS; n; n--)
        if (*t++ == TILE_HIDDEN_LADDER) count = 1;
    return count;
}

int16_t level_count_gold(void)
{
    const uint8_t *t = &gs.tiles[0][0];
    int16_t n, count = 0;
    for (n = GRID_COLS * GRID_ROWS; n; n--)
        if (*t++ == TILE_GOLD) count++;
    return count;
}
