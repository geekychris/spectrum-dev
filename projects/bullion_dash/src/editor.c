/*
 * Bullion Dash - in-game level editor (the original's editor.c)
 *
 * Cursor-based tile placement: move with the joystick or keys, FIRE puts the
 * current tile, ENTER test-plays the level, BREAK goes back to the title.
 *
 * port: the original picks tiles with SPACE (cycle) or 1-0 and saves/loads
 * DH2:Dev/levels/level_NNN.txt with F1/F2. On the Spectrum SPACE is fire and
 * the digits are the Sinclair joysticks, so Z / X step through the tiles;
 * S keeps the level in memory and L brings it back (no files: it lasts until
 * the Spectrum is switched off). A test play comes back to the editor when
 * the player dies or finishes (the original carried on with the built-in
 * levels). Hidden ladders and traps are drawn in the editor so they can be
 * seen (the original drew them as empty and as brick).
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "game.h"
#include "level.h"
#include "editor.h"
#include "render.h"
#include "art.h"
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"
#include "cellspr48.h"
#include "memmap.h"

#define MOVE_REPEAT_DELAY 6

static const uint8_t tile_cycle[NUM_TILES] = {
    TILE_EMPTY, TILE_BRICK, TILE_SOLID, TILE_LADDER, TILE_BAR,
    TILE_GOLD, TILE_HIDDEN_LADDER, TILE_PLAYER, TILE_ENEMY, TILE_TRAP
};
static const char *const tile_names[NUM_TILES] = {
    "EMPTY ", "BRICK ", "SOLID ", "LADDER", "BAR   ",
    "GOLD  ", "HIDDEN", "PLAYER", "ENEMY ", "TRAP  "
};

uint8_t ed_x, ed_y, ed_tile;                       /* cursor and tile (the test reads them) */
static uint8_t move_delay, status_timer;
static uint8_t redraw;                              /* the editor screen needs drawing */
static uint8_t shown_x = 255, shown_y, shown_idx = 255;
#define edit_grid ((uint8_t *)HI_EDIT)              /* the level while it is test-played */
#define kept ((uint8_t *)HI_KEPT)                   /* port: S / L */
static uint8_t have_kept;
static const char *status_msg;

/* a level in 224 bytes: two tiles (ids < 16) a byte */
static void pack_level(uint8_t *dst)
{
    const uint8_t *t = &gs.tiles[0][0];
    uint8_t n;
    for (n = GRID_COLS * GRID_ROWS / 2; n; n--, t += 2) *dst++ = (t[0] << 4) | t[1];
}

static void unpack_level(const uint8_t *src)
{
    uint8_t *t = &gs.tiles[0][0], n;
    for (n = GRID_COLS * GRID_ROWS / 2; n; n--, src++) {
        *t++ = *src >> 4;
        *t++ = *src & 15;
    }
    level_refresh_paths();
}

static void enter(void)
{
    redraw = 1;
    shown_x = shown_idx = 255;
    status_timer = 0;
}

void editor_init(void)
{
    uint8_t x;
    memset(gs.tiles, TILE_EMPTY, sizeof(gs.tiles));
    for (x = 0; x < GRID_COLS; x++)
        gs.tiles[GRID_ROWS - 1][x] = TILE_SOLID;
    level_refresh_paths();
    gs.level_num = 0;
    ed_x = ed_y = 0;
    ed_tile = 1;                                    /* TILE_BRICK */
    move_delay = 0;
    enter();
}

void editor_resume(void)
{
    unpack_level(edit_grid);
    enter();
}

static void message(const char *s)
{
    status_msg = s;
    status_timer = 100;                             /* ~2 seconds */
    zx_log("bullion: editor %s\n", s);
}

uint8_t editor_update(uint8_t held, uint8_t pressed, char letter)
{
    int8_t dx = (held & IN_LEFT) ? -1 : (held & IN_RIGHT) ? 1 : 0;
    int8_t dy = (held & IN_UP) ? -1 : (held & IN_DOWN) ? 1 : 0;

    /* cursor movement with repeat delay */
    if (dx == 0 && dy == 0) {
        move_delay = 0;
    } else if (move_delay == 0) {
        int8_t nx = ed_x + dx, ny = ed_y + dy;
        if (nx >= 0 && nx < GRID_COLS) ed_x = nx;
        if (ny >= 0 && ny < GRID_ROWS) ed_y = ny;
        move_delay = MOVE_REPEAT_DELAY;
    } else {
        move_delay--;
    }

    /* place the tile with fire */
    if (held & IN_FIRE)
        tile_set(ed_x, ed_y, tile_cycle[ed_tile]);

    /* port: Z / X step through the tiles (the original: SPACE, 1-0) */
    if (pressed & IN_FIRE2) ed_tile = ed_tile ? ed_tile - 1 : NUM_TILES - 1;
    if (letter == 'X') ed_tile = (ed_tile + 1) % NUM_TILES;

    if (letter == 'S') {                            /* the original's F1 */
        pack_level(kept);
        have_kept = 1;
        message("KEPT IN MEMORY");
    }
    if (letter == 'L') {                            /* F2 */
        if (have_kept) {
            unpack_level(kept);
            redraw = 1;
            message("LEVEL BACK");
        } else {
            message("NOTHING KEPT");
        }
    }
    if (status_timer) status_timer--;

    if (pressed & IN_START) {                       /* F3: test play */
        pack_level(edit_grid);
        level_activate_spawn_markers();
        gs.gold_total = level_count_gold();
        gs.gold_collected = 0;
        return EDITOR_TEST;
    }
    if (pressed & IN_QUIT) return EDITOR_EXIT;      /* ESC */
    return 0;
}

static void draw_palette(void)
{
    uint8_t i;
    attr_fill(0, 19, 32, 5, PAPER_BLUE | INK_WHITE | BRIGHT);
    for (i = 0; i < NUM_TILES; i++) {
        uint8_t t = tile_cycle[i];
        tile_draw(1 + 2 * i, 20, edit_gfx + 8 * t);
        attr_set(1 + 2 * i, 20, (uint8_t)(PAPER_BLACK | (cs_attrs[t] & 0x47)));
    }
    zx_print_at(0, 22, "QAOP MOVE FIRE PUT Z/X TILE");
    zx_print_at(0, 23, "ENTER TEST S/L KEEP/GET BREAK END");
}

void editor_draw(void)
{
    if (redraw) {
        render_room(1);
        draw_palette();
        redraw = 0;
    }
    if (ed_tile != shown_idx) {
        uint8_t i;
        for (i = 0; i < NUM_TILES; i++)
            zx_print_at(1 + 2 * i, 21, i == ed_tile ? "^" : " ");
        zx_print_at(22, 20, tile_names[ed_tile]);
        shown_idx = ed_tile;
    }
    if (ed_x != shown_x || ed_y != shown_y) {
        zx_print_at(21, 19, "(  ,  )");
        text_num(22, 19, ed_x, 2);
        text_num(25, 19, ed_y, 2);
        zx_print_at(1, 19, "EDITOR");
        shown_x = ed_x;
        shown_y = ed_y;
    }
    attr_fill(14, 0, 18, 1, PAPER_BLUE | INK_YELLOW | BRIGHT);
    zx_print_at(14, 0, status_timer ? status_msg : "              ");
    render_cursor(ed_x, ed_y, (gs.frame >> 3) & 1);
}
