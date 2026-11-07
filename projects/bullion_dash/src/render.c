/*
 * Bullion Dash - the Spectrum screen (see render.h).
 *
 * port: the original redraws its whole 320 x 256 screen every frame; here
 * cellspr48 rewrites only the cells a sprite left or entered and the tiles
 * that changed. Colours follow the original's palette as attributes: brick
 * red, solid grey (white), ladders bright cyan, bars cyan, gold yellow, the
 * player green, the guards red. A sprite takes the ink of the cells it is in.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "game.h"
#include "render.h"
#include "art.h"
#include "zxgame.h"
#include "zxtext.h"
#include "cellspr48.h"
#include "memmap.h"
#include "coords.h"

#define PAPER   PAPER_BLACK
#define SIDE    (PAPER_BLUE | INK_WHITE)            /* the original's dark blue sides */

const uint8_t tile_attr[NUM_TILES] = {
    PAPER | INK_WHITE,                  /* empty */
    PAPER | INK_RED,                    /* brick */
    PAPER | INK_WHITE,                  /* solid */
    PAPER | INK_CYAN | BRIGHT,          /* ladder */
    PAPER | INK_CYAN,                   /* bar */
    PAPER | INK_YELLOW | BRIGHT,        /* gold */
    PAPER | INK_WHITE,                  /* hidden ladder (not shown) */
    PAPER | INK_GREEN | BRIGHT,         /* player marker */
    PAPER | INK_RED | BRIGHT,           /* enemy marker */
    PAPER | INK_RED,                    /* trap: looks like brick */
};
static const uint8_t edit_attr[NUM_TILES] = {
    PAPER | INK_WHITE, PAPER | INK_RED, PAPER | INK_WHITE, PAPER | INK_CYAN | BRIGHT,
    PAPER | INK_CYAN, PAPER | INK_YELLOW | BRIGHT, PAPER | INK_CYAN, PAPER | INK_GREEN | BRIGHT,
    PAPER | INK_RED | BRIGHT, PAPER | INK_MAGENTA | BRIGHT,
};

#define INK_PLAYER (INK_GREEN | BRIGHT)
#define INK_ENEMY  (INK_RED | BRIGHT)
#define INK_LOOT   (INK_YELLOW | BRIGHT)                /* a guard carrying gold */

/* pre-shifted frames (8 shifts x 2 bytes x 8 lines), facing right, then the
   left-facing copies of the frames that aren't symmetric */
#define PRE_SIZE (8 * 2 * 8)
#define FR_RUN1_L (NUM_FRAMES)
#define FR_RUN2_L (NUM_FRAMES + 1)
#define FR_DIG_L  (NUM_FRAMES + 2)
#define PRE(f) ((uint8_t *)HI_PRE + (f) * PRE_SIZE)
static const uint8_t flip_frames[3] = { FR_RUN1, FR_RUN2, FR_DIG };


static uint8_t live;                /* the room is on screen: changed tiles are redrawn */

static uint8_t mirror(uint8_t b)
{
    uint8_t r = 0, i;
    for (i = 0; i < 8; i++) {
        r = (r << 1) | (b & 1);
        b >>= 1;
    }
    return r;
}

void render_init(void)
{
    uint8_t f, i, flipped[8];

    for (f = 0; f < NUM_FRAMES; f++)
        spr_preshift(frame_gfx + 8 * f, 1, 8, PRE(f));
    for (f = 0; f < 3; f++) {
        const uint8_t *src = frame_gfx + 8 * flip_frames[f];
        for (i = 0; i < 8; i++) flipped[i] = mirror(src[i]);
        spr_preshift(flipped, 1, 8, PRE(NUM_FRAMES + f));
    }
    cs_init(&gs.tiles[0][0], GRID_COLS, GRID_ROWS, ROOM_COL, ROOM_ROW, tile_gfx, tile_attr);
    cs_nspr = 1 + MAX_ENEMIES;
}

void render_tile_changed(uint8_t x, uint8_t y)
{
    if (live) cs_mark(x, y);
}

void render_room(uint8_t editor)
{
    uint8_t i;
    live = 0;
    screen_clear(SIDE, INK_BLUE);
    attr_fill(ROOM_COL, ROOM_ROW, GRID_COLS, GRID_ROWS, PAPER | INK_WHITE);
    attr_fill(0, 0, 32, 1, PAPER_BLUE | INK_YELLOW | BRIGHT);
    zx_print_at(1, 0, "BULLION DASH");
    attr_fill(0, 18, 32, 1, PAPER_BLUE | INK_CYAN);
    gfx_fill_rect(0, 18 * 8 + 3, 32, 1, 0xFF);          /* the original's cyan line */
    cs_tiles = editor ? edit_gfx : tile_gfx;
    cs_attrs = editor ? edit_attr : tile_attr;
    for (i = 0; i < CS_MAX_SPR; i++) cs_spr[i].on = 0;
    cs_draw_all();
    live = 1;
    if (!editor) render_status(1);
}

static void place(cs_sprite *s, int16_t px, int16_t py, uint8_t frame, uint8_t left, uint8_t ink)
{
    if (py >= GRID_ROWS * TILE_H) {                     /* fell off the grid */
        s->on = 0;
        return;
    }
    if (left) {
        if (frame == FR_RUN1) frame = FR_RUN1_L;
        else if (frame == FR_RUN2) frame = FR_RUN2_L;
        else if (frame == FR_DIG) frame = FR_DIG_L;
    }
    s->x = ROOM_X(px);
    s->y = ROOM_Y(py);
    s->gfx = PRE(frame);
    s->w = 1;
    s->h = 8;
    s->ink = ink;
    s->keep = 0xF8 & ~(INK_WHITE | BRIGHT);            /* the cell's paper and flash */
    s->on = 1;
}

/* the original's animation choices (render.c draw_player / draw_enemy) */
static uint8_t player_frame(const Player *p)
{
    uint8_t alt = (p->anim >> 2) & 1;
    switch (p->state) {
    case PS_RUNNING:  return alt ? FR_RUN2 : FR_RUN1;
    case PS_CLIMBING: return alt ? FR_CLIMB2 : FR_CLIMB1;
    case PS_HANGING:  return FR_HANG;
    case PS_DIGGING:  return FR_DIG;
    case PS_FALLING:  return FR_FALL;               /* port: the original stands */
    }
    return FR_STAND;
}

static uint8_t enemy_frame(const Enemy *e)
{
    uint8_t alt = (e->anim >> 2) & 1;
    switch (e->state) {
    case ES_TRAPPED:  return FR_TRAPPED;
    case ES_RUNNING:  return alt ? FR_RUN2 : FR_RUN1;
    case ES_CLIMBING: return alt ? FR_CLIMB2 : FR_CLIMB1;
    case ES_HANGING:  return FR_HANG;
    case ES_FALLING:  return FR_FALL;
    }
    return FR_STAND;
}

void render_frame(uint8_t entities)
{
    cs_sprite *s = cs_spr;
    const Enemy *e = gs.enemies;
    uint8_t i;

    if (entities) {
        const Player *p = &gs.player;
        place(s, p->px, p->py, player_frame(p), p->dir == DIR_LEFT, INK_PLAYER);
    } else {
        s->on = 0;
    }
    for (i = 0, s++; i < MAX_ENEMIES; i++, s++, e++) {
        if (!entities || i >= gs.num_enemies || !e->active || e->state == ES_DEAD) {
            s->on = 0;
            continue;
        }
        place(s, e->px, e->py, enemy_frame(e), e->dir == DIR_LEFT,
              e->has_gold ? INK_LOOT : INK_ENEMY);
    }
    cs_update();
}

void render_cursor(uint8_t x, uint8_t y, uint8_t on)
{
    cs_sprite *s = cs_spr;
    s->x = x * 8;
    s->y = y * 8;
    s->gfx = PRE(FR_CURSOR);
    s->w = 1;
    s->h = 8;
    s->ink = INK_WHITE | BRIGHT;
    s->keep = 0xF8 & ~(INK_WHITE | BRIGHT);
    s->on = on;
    cs_update();
}

/* ------------------------------------------------------------ status line */

static uint32_t shown_score = 0xFFFFFFFF;
static int16_t shown_level = -1, shown_lives = -1, shown_gold = -1, shown_total = -1;

static void num_at(uint8_t col, uint8_t row, uint16_t v)
{
    char buf[6], *p = buf + 5;
    *p = 0;
    do { *--p = '0' + v % 10; v /= 10; } while (v);
    zx_print_at(col, row, p);
}

void render_status(uint8_t force)
{
    if (force) {
        attr_fill(0, 19, 32, 1, SIDE | BRIGHT);
        attr_fill(0, 20, 32, 1, PAPER_BLUE | INK_YELLOW | BRIGHT);
        zx_print_at(1, 19, "SCORE:");
        zx_print_at(15, 19, "LV:");
        zx_print_at(23, 19, "LIVES:");
        zx_print_at(11, 20, "GOLD:");
        shown_score = 0xFFFFFFFF;
        shown_level = shown_lives = shown_gold = shown_total = -1;
    }
    if (gs.score != shown_score) {
        text_num(7, 19, gs.score, 6);                 /* SCORE:%06ld */
        shown_score = gs.score;
    }
    if (gs.level_num != shown_level) {
        zx_print_at(18, 19, "    ");
        num_at(18, 19, gs.level_num);
        zx_print_at(25, 0, "LEVEL    ");
        num_at(31 - (gs.level_num > 99) - (gs.level_num > 9), 0, gs.level_num);
        shown_level = gs.level_num;
    }
    if (gs.lives != shown_lives) {
        zx_print_at(29, 19, "  ");
        num_at(29, 19, gs.lives);
        shown_lives = gs.lives;
    }
    if (gs.gold_collected != shown_gold || gs.gold_total != shown_total) {
        uint8_t c = 16;
        zx_print_at(16, 20, "       ");
        num_at(c, 20, gs.gold_collected);
        c += 1 + (gs.gold_collected > 9);
        zx_print_at(c, 20, "/");
        num_at(c + 1, 20, gs.gold_total);
        shown_gold = gs.gold_collected;
        shown_total = gs.gold_total;
    }
}

/* ------------------------------------------------------------ message box */

static void box_line(uint8_t row, const char *s, uint8_t attr)
{
    attr_fill(6, row, 20, 1, attr);
    zx_print_at(6, row, "                    ");
    if (s) zx_print_at(16 - (uint8_t)(strlen(s) / 2), row, s);
}

void render_box(const char *l1, const char *l2, const char *l3, uint8_t attr)
{
    box_line(7, 0, attr);
    box_line(8, l1, attr);
    box_line(9, l2, attr);
    box_line(10, l3, attr);
    box_line(11, 0, attr);
}
