/*
 * Miner Meteor for the ZX Spectrum 48K - port of amiga_mcp/examples/miner_meteor.
 *
 * game.c is the original's rules (game.cpp) at a fixed 50 Hz; this file is
 * the Spectrum front end. port: the 320x240 cavern is drawn at half size,
 * one 16-pixel tile to a character cell (20 x 15 cells), so every tile has
 * its own attribute colours. The miner and the guardians are 8x8 sprites
 * composited per cell over the tiles (sdk cellspr48.h): smooth, no flicker,
 * each colouring the cells it is in. Beeper effects and a title tune (the
 * AY plays the tune in the game too on a 128K), a demo that plays the
 * caverns, high scores with initials.
 *
 * Controls (README.md): LEFT/RIGHT walk, UP or FIRE jumps; keys, Sinclair, Kempston.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "game.h"
#include "levels_data.h"
#include "tmap.h"
#include "cellspr48.h"
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"

#include "demo.h"

/* ------------------------------------------------------------ layout */

#define ROOM_COL 6                       /* the cavern: cells (6..25, 1..15) */
#define ROOM_ROW 1
#define ROW_NAME 17
#define ROW_AIR  19
#define ROW_SCORE 21
#define ROW_LIVES 23
#define AIR_COL  10                      /* the air bar: cells 10..25 */
#define AIR_CELLS 16

/* ------------------------------------------------------------ art (1-bit, '#' = ink) */

/* display tiles (cellspr48 ids) */
enum {
    D_EMPTY, D_BRICK, D_KEY, D_EXIT, D_EXIT_OPEN, D_CONV_L, D_CONV_R, D_HAZARD,
    D_CRUMBLE,                           /* 8 stages: D_CRUMBLE .. D_CRUMBLE + 7 */
    D_COUNT = D_CRUMBLE + 8
};

static const char *const art_tiles[D_CRUMBLE + 1][8] = {
    { "........", "........", "........", "........", "........", "........", "........", "........" },
    { "#######.", "#######.", "#######.", "........", "###.####", "###.####", "###.####", "........" },
    { "........", ".###....", "#...#...", "#...####", "#...#.##", ".###..#.", "........", "........" },
    { "..####..", ".#....#.", "#..##..#", "#.#..#.#", "#.####.#", "#.####.#", "#......#", "########" },
    { "..####..", ".######.", "##....##", "##....##", "##....##", "##....##", "##....##", "########" },
    { "########", ".#...#..", "#...#...", ".#...#..", "########", ".##..##.", "#..##..#", ".##..##." },
    { "########", "..#...#.", "...#...#", "..#...#.", "########", ".##..##.", "#..##..#", ".##..##." },
    { "........", "........", ".#...#..", ".#...#..", "###.###.", "###.###.", "########", "########" },
    { "########", "##....##", "#.#..#.#", "#..##..#", "#..##..#", "#.#..#.#", "##....##", "########" },
};

/* the miner, facing right: 4 walk frames (left = mirrored) */
static const char *const art_miner[4][8] = {
    { "..###...", ".#####..", "..#.##..", "..###...", ".####...", "#.###.#.", "..#.#...", ".##.##.." },
    { "..###...", ".#####..", "..#.##..", "..###...", ".####...", ".####...", "..##....", "..###..." },
    { "..###...", ".#####..", "..#.##..", "..###...", ".####...", "#.###.#.", ".#...#..", "##...##." },
    { "..###...", ".#####..", "..#.##..", "..###...", ".####...", ".####...", "..##....", ".##.#..." },
};

/* the three guardians (the original's colours 4, 5, 6), 2 frames each */
static const char *const art_guard[3][2][8] = {
    { { ".#....#.", "..#..#..", "..####..", ".######.", "##.##.##", "########", "########", "#.#..#.#" },
      { "#......#", ".#....#.", "..####..", ".######.", "##.##.##", "########", "########", ".#.##.#." } },
    { { "#......#", ".#....#.", "..####..", ".######.", "#..##..#", "#.####.#", "########", "#.#..#.#" },
      { ".#....#.", ".#....#.", "..####..", ".######.", "#..##..#", "#.####.#", "########", ".#.##.#." } },
    { { ".#....#.", "..#..#..", ".######.", "#.####.#", "########", "#.#.#.##", "##.#.#.#", "#.#..#.#" },
      { "..#..#..", "..#..#..", ".######.", "#.####.#", "########", "##.#.#.#", "#.#.#.##", ".#.##.#." } },
};
static const uint8_t guard_ink[3] = { INK_CYAN | BRIGHT, INK_MAGENTA | BRIGHT, INK_YELLOW | BRIGHT };

static uint8_t tiles[D_COUNT * 8];
static uint8_t tile_attr[D_COUNT];
static uint8_t pre_miner[2][4][8 * 2 * 8];   /* facing, frame: 8 shifts x 2 bytes x 8 lines */
static uint8_t pre_guard[3][2][8 * 2 * 8];
static uint8_t miner_icon[8], guard_icon[8];  /* plain bitmaps (HUD, title) */
static uint8_t room[GRID_W * GRID_H];         /* display tile ids */
static uint8_t crumble_stage[COLLAPSE_FRAMES + 1];

static void art_bits(const char *const *art, uint8_t *out, uint8_t mirror)
{
    uint8_t y, x;
    for (y = 0; y < 8; y++) {
        uint8_t b = 0;
        for (x = 0; x < 8; x++)
            if (art[y][mirror ? 7 - x : x] == '#') b |= 0x80 >> x;
        out[y] = b;
    }
}

static void art_init(void)
{
    uint8_t i, f, k, bits[8];
    for (i = 0; i <= D_CRUMBLE; i++) art_bits(art_tiles[i], tiles + i * 8, 0);
    /* crumbling floor: the crate sinks a line at a time */
    for (k = 1; k < 8; k++) {
        uint8_t *t = tiles + (D_CRUMBLE + k) * 8;
        memset(t, 0, k);
        memcpy(t + k, tiles + D_CRUMBLE * 8, 8 - k);
    }
    for (k = 0; k <= COLLAPSE_FRAMES; k++)
        crumble_stage[k] = k ? (uint8_t)((COLLAPSE_FRAMES - k) * 8 / COLLAPSE_FRAMES) : 0;
    for (f = 0; f < 4; f++)
        for (i = 0; i < 2; i++) {
            art_bits(art_miner[f], bits, i == 0);
            spr_preshift(bits, 1, 8, pre_miner[i][f]);
            if (f == 0 && i == 1) memcpy(miner_icon, bits, 8);
        }
    for (k = 0; k < 3; k++)
        for (f = 0; f < 2; f++) {
            art_bits(art_guard[k][f], bits, 0);
            spr_preshift(bits, 1, 8, pre_guard[k][f]);
            if (k == 0 && f == 0) memcpy(guard_icon, bits, 8);
        }
}

/* the original's palette_variant (unused there): the walls' colour */
static const uint8_t wall_ink[4] = { INK_RED | BRIGHT, INK_MAGENTA | BRIGHT, INK_GREEN | BRIGHT, INK_BLUE | BRIGHT };

static void colours(uint8_t variant)
{
    uint8_t k;
    tile_attr[D_EMPTY] = INK_WHITE;
    tile_attr[D_BRICK] = wall_ink[variant & 3];
    tile_attr[D_KEY] = INK_YELLOW | BRIGHT;
    tile_attr[D_EXIT] = INK_WHITE;
    tile_attr[D_EXIT_OPEN] = INK_YELLOW | BRIGHT;
    tile_attr[D_CONV_L] = tile_attr[D_CONV_R] = INK_YELLOW;
    tile_attr[D_HAZARD] = INK_CYAN | BRIGHT;
    for (k = 0; k < 8; k++) tile_attr[D_CRUMBLE + k] = INK_YELLOW;
}

/* ------------------------------------------------------------ the cavern view */

static uint16_t key_cells[8];
static uint8_t key_c[8], key_r[8], key_n;          /* animated cells (colour cycling, belts) */
/* belt cells in 4 groups: one group's moving lines are redrawn a frame */
#define BELT_MAX (GRID_W * 3)
static uint8_t conv_c[4][BELT_MAX / 2], conv_r[4][BELT_MAX / 2], conv_n[4];
static uint8_t exit_c, exit_r, have_exit;

static uint8_t display_id(uint16_t cell)
{
    switch (gs.kind[cell]) {
    case T_SOLID:    return D_BRICK;
    case T_KEY:      return D_KEY;
    case T_EXIT:     return gs.keys_remaining ? D_EXIT : D_EXIT_OPEN;
    case T_COLLAPSE: return D_CRUMBLE + crumble_stage[game_collapse_timer(cell)];
    case T_CONV_L:   return D_CONV_L;
    case T_CONV_R:   return D_CONV_R;
    case T_HAZARD:   return D_HAZARD;
    default:         return D_EMPTY;
    }
}

static void cell_rc(uint16_t cell, uint8_t *c, uint8_t *r)
{
    uint8_t row = 0;
    while (cell >= GRID_W) { cell -= GRID_W; row++; }
    *c = (uint8_t)cell;
    *r = row;
}

static void sprites_from_game(void);

/* the whole cavern (after a load) */
static void room_build(void)
{
    uint16_t cell;
    uint8_t c = 0, r = 0;
    key_n = 0;
    memset(conv_n, 0, sizeof(conv_n));
    have_exit = 0;
    colours(level_palette[gs.level_index]);
    for (cell = 0; cell < GRID_W * GRID_H; cell++) {
        uint8_t id = display_id(cell);
        room[cell] = id;
        if (id == D_KEY && key_n < sizeof(key_c)) { key_cells[key_n] = cell; key_c[key_n] = c; key_r[key_n++] = r; }
        if (id == D_CONV_L || id == D_CONV_R) {
            uint8_t g = c & 3, n = conv_n[g]++;
            conv_c[g][n] = c;
            conv_r[g][n] = r;
        }
        if (id == D_EXIT || id == D_EXIT_OPEN) { exit_c = c; exit_r = r; have_exit = 1; }
        if (++c == GRID_W) { c = 0; r++; }
    }
    sprites_from_game();
    cs_draw_all();
}

/* the tiles the logic changed this tick */
static void room_changes(void)
{
    uint8_t i, c, r;
    for (i = 0; i < gs.num_changed; i++) {
        uint16_t cell = gs.changed[i];
        cell_rc(cell, &c, &r);
        cs_set(c, r, display_id(cell));
    }
    if ((gs.events & EV_UNLOCK) && have_exit) cs_set(exit_c, exit_r, D_EXIT_OPEN);
}

/* belts, key colours and the open exit move on their own */
static const uint8_t key_inks[4] = { INK_YELLOW | BRIGHT, INK_CYAN | BRIGHT, INK_MAGENTA | BRIGHT, INK_GREEN | BRIGHT };

static void rotate_belt(uint8_t *t, uint8_t left)
{
    uint8_t y;
    for (y = 1; y < 4; y++)
        t[y] = left ? (uint8_t)((t[y] << 1) | (t[y] >> 7)) : (uint8_t)((t[y] >> 1) | (t[y] << 7));
}

static void animate(uint8_t frame)
{
    uint8_t i;
    if ((frame & 3) == 0) {                   /* the belts move a pixel every 4 frames */
        rotate_belt(tiles + D_CONV_L * 8, 1);
        rotate_belt(tiles + D_CONV_R * 8, 0);
    }
    {   /* and their moving lines are redrawn a quarter of the cells a frame */
        uint8_t g = frame & 3;
        if (conv_n[g]) cs_refresh(conv_c[g], conv_r[g], conv_n[g], 1, 3);
    }
    if ((frame & 7) == 2) {
        tile_attr[D_KEY] = key_inks[(frame >> 3) & 3];
        for (i = 0; i < key_n; i++)
            if (room[key_cells[i]] == D_KEY) cs_mark(key_c[i], key_r[i]);
    }
    if ((frame & 7) == 5 && have_exit && room[exit_r * GRID_W + exit_c] == D_EXIT_OPEN) {
        tile_attr[D_EXIT_OPEN] = (frame & 8) ? (INK_YELLOW | BRIGHT) : (INK_WHITE | BRIGHT | PAPER_RED);
        cs_mark(exit_c, exit_r);
    }
}

/* sprite 0..2: guardians, 3: the miner (last, so its colour wins) */
static void sprites_from_game(void)
{
    uint8_t i;
    cs_sprite *s = cs_spr;
    const Guardian *G = gs.guards;
    for (i = 0; i < 3; i++, s++, G++) {
        uint8_t k;
        s->on = i < gs.num_guards && G->alive;
        if (!s->on) continue;
        k = G->colour - 4;                    /* the original's colours 4, 5, 6 */
        if (k > 2) k = 0;
        s->x = (uint8_t)(G->x >> 1);
        s->y = (uint8_t)(G->y >> 1);
        s->gfx = pre_guard[k][(G->frame >> 3) & 1];
        s->ink = guard_ink[k];
    }
    s->x = (uint8_t)(gs.player.b.x >> 1);
    s->y = (uint8_t)(gs.player.b.y >> 1);
    s->gfx = pre_miner[gs.player.facing][gs.player.frame];
}

/* the parts of the sprites that never change */
static void sprites_init(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        cs_spr[i].w = 1;
        cs_spr[i].h = 8;
        cs_spr[i].keep = 0x38;               /* the cell keeps its paper */
    }
    cs_spr[3].on = 1;
    cs_spr[3].ink = INK_WHITE | BRIGHT;
}

/* ------------------------------------------------------------ HUD */

static uint8_t hud_air_len = 255, hud_lives = 255, hud_keys = 255;
static int16_t hud_air = -1;
static uint32_t hud_score = 0xFFFFFFFF;

static void hud_frame(void)
{
    attr_fill(0, ROW_NAME, 32, 1, PAPER_YELLOW | INK_BLACK);
    gfx_clear_rect(0, ROW_NAME * 8, 32, 8);
    text_center(ROW_NAME, lv_names[gs.level_index]);
    attr_fill(ROOM_COL, ROW_AIR, 4, 1, PAPER_RED | INK_WHITE | BRIGHT);
    zx_print_at(ROOM_COL, ROW_AIR, "AIR ");
    attr_fill(AIR_COL, ROW_AIR, AIR_CELLS, 1, PAPER_GREEN | INK_WHITE | BRIGHT);
    gfx_clear_rect(AIR_COL, ROW_AIR * 8, AIR_CELLS, 8);
    attr_fill(0, ROW_SCORE, 32, 1, INK_YELLOW | BRIGHT);
    zx_print_at(1, ROW_SCORE, "HI-SCORE");
    text_num(10, ROW_SCORE, hs_table[0].score, 6);
    zx_print_at(19, ROW_SCORE, "SCORE");
    attr_fill(0, ROW_LIVES, 32, 1, INK_WHITE | BRIGHT);
    hud_air_len = hud_lives = hud_keys = 255;
    hud_air = -1;
    hud_score = 0xFFFFFFFF;
}

static void hud(void)
{
    uint8_t len;
    if (gs.air != hud_air) {                  /* every 6 ticks */
        hud_air = gs.air;
        len = (uint8_t)((uint16_t)(gs.air > 0 ? gs.air : 0) * 8 / 63);   /* 1000 -> 126 px */
    } else len = hud_air_len;
    if (len != hud_air_len) {
        /* the bar: pixel lines 2..5 of the row; redraw only the byte columns that changed */
        uint8_t from = hud_air_len == 255 ? 0 : (len < hud_air_len ? len : hud_air_len) >> 3;
        uint8_t to = ((len > hud_air_len || hud_air_len == 255 ? len : hud_air_len) >> 3) + 1, b, y;
        if (to > AIR_CELLS) to = AIR_CELLS;
        for (b = from; b < to; b++) {
            uint8_t v = len >= (uint8_t)(b * 8 + 8) ? 0xFF : len <= b * 8 ? 0 : (uint8_t)(0xFF00 >> (len - b * 8));
            for (y = 2; y < 6; y++) zx_row[ROW_AIR * 8 + y][AIR_COL + b] = v;
        }
        hud_air_len = len;
    }
    if (gs.score != hud_score) {
        text_num(25, ROW_SCORE, gs.score, 6);
        hud_score = gs.score;
    }
    if (gs.lives != hud_lives) {
        uint8_t i;
        for (i = 0; i < 6; i++) {
            if (i < gs.lives) tile_draw(1 + i * 2, ROW_LIVES, miner_icon);
            else gfx_clear_rect(1 + i * 2, ROW_LIVES * 8, 1, 8);
        }
        hud_lives = gs.lives;
    }
    if (gs.keys_remaining != hud_keys) {
        char k[2] = {0, 0};
        zx_print_at(20, ROW_LIVES, "KEYS  /");
        k[0] = '0' + (gs.keys_total - gs.keys_remaining);
        zx_print_at(25, ROW_LIVES, k);
        k[0] = '0' + gs.keys_total;
        zx_print_at(27, ROW_LIVES, k);
        attr_fill(20, ROW_LIVES, 8, 1, gs.keys_remaining ? (INK_WHITE | BRIGHT) : (INK_YELLOW | BRIGHT));
        hud_keys = gs.keys_remaining;
    }
}

/* the screen around the cavern: a frame, a few stars */
static void game_screen(void)
{
    static const uint8_t stars[][2] = { {1, 2}, {3, 5}, {2, 9}, {4, 12}, {0, 14}, {28, 1}, {30, 4}, {27, 8}, {29, 11}, {31, 14} };
    uint8_t i;
    screen_clear(INK_WHITE, INK_BLACK);
    for (i = 0; i < sizeof(stars) / 2; i++) {
        gfx_plot(stars[i][0] * 8 + 3, (stars[i][1] + ROOM_ROW) * 8 + 4);
        attr_set(stars[i][0], stars[i][1] + ROOM_ROW, (i & 1) ? INK_CYAN : INK_WHITE | BRIGHT);
    }
    hud_frame();
    room_build();
    hud();
}

/* ------------------------------------------------------------ sound */

/* the tune (original, port addition: the Amiga's sound was a stub):
   Am - F - G - E arpeggios, MIDI note numbers */
static const uint8_t tune_notes[] = {
    69, 72, 76, 81, 79, 76, 72, 76,  65, 69, 72, 77, 76, 72, 69, 72,
    67, 71, 74, 79, 77, 74, 71, 74,  64, 68, 71, 76, 74, 71, 68, 71,
};
#define TUNE_LEN sizeof(tune_notes)
static ay_note tune_ay[TUNE_LEN + 1];
static uint16_t tune_beep[TUNE_LEN];         /* beep() periods */
static uint16_t tune_cycles[TUNE_LEN];       /* cycles in ~2 frames */

static void tune_init(void)
{
    uint8_t i;
    for (i = 0; i < TUNE_LEN; i++) {
        uint16_t p = AY_NOTE(tune_notes[i] - 12);   /* AY an octave down: a bass line */
        tune_ay[i].period = p;
        tune_ay[i].len = 8;
        /* beep(): a half period is ~26 * period + 40 T-states; an AY period
           P is a half period of ~15.8 * P T-states, so period = 0.607 P - 1.5 */
        p = AY_NOTE(tune_notes[i]);
        tune_beep[i] = (uint16_t)(((uint32_t)p * 155) >> 8) - 2;
        tune_cycles[i] = (uint16_t)(70000UL / (26UL * tune_beep[i] + 40));
    }
    tune_ay[TUNE_LEN].period = 0;
    tune_ay[TUNE_LEN].len = 0;
}

enum { S_JUMP, S_KEY, S_DIE, S_LEVEL, S_OVER, S_WIN };

/* beeper effects in the game are played a chunk a frame (a few thousand
   T-states each), so an effect never costs the game a frame; the long
   jingles (death, cavern cleared, the end) play while the game waits anyway.
   {beep period, cycles}, 0 ends */
/* a chunk is cycles * 2 * (26 * period + 40) T-states: ~4,500 here */
static const uint8_t fx_jump[] = { 50, 2, 44, 2, 38, 2, 34, 3, 30, 3, 26, 3, 0 };
static const uint8_t fx_key[] = { 20, 4, 16, 5, 13, 6, 10, 8, 13, 6, 10, 8, 0 };
static const uint8_t *fx_at;

static void fx_tick(void)
{
    if (!fx_at) return;
    if (!fx_at[0]) { fx_at = 0; return; }
    beep(fx_at[0], fx_at[1]);
    fx_at += 2;
}

static void sound(uint8_t s)
{
    if (ay_present) {
        switch (s) {
        case S_JUMP:  ay_sfx(0, 300, 12, 2, -12); break;
        case S_KEY:   ay_sfx(1, 60, 15, 1, -2); break;
        case S_DIE:   ay_sfx(1, 0, 15, 1, 0); break;
        case S_LEVEL: ay_sfx(0, 200, 15, 1, -6); break;
        case S_OVER:  ay_sfx(0, 400, 15, 1, 20); break;
        case S_WIN:   ay_sfx(0, 100, 15, 1, -3); break;
        }
        return;
    }
    switch (s) {
    case S_JUMP:  if (!fx_at) fx_at = fx_jump; break;
    case S_KEY:   fx_at = fx_key; break;
    case S_DIE:   fx_at = 0; beep_sweep(40, 200, 8, 2); beep_noise(300); break;
    case S_LEVEL: fx_at = 0; beep(60, 20); beep(45, 25); beep(36, 30); beep(30, 35); beep(22, 50); break;
    case S_OVER:  beep(40, 25); beep(60, 20); beep(90, 15); beep(130, 12); break;
    case S_WIN:   beep(45, 25); beep(36, 30); beep(30, 35); beep(22, 50); beep(30, 35); beep(22, 80); break;
    }
}

/* ------------------------------------------------------------ title */

static void title_screen(void)
{
    screen_clear(INK_WHITE, INK_BLACK);
    attr_fill(0, 1, 32, 2, INK_YELLOW | BRIGHT);
    text_big(5, 1, "MINER METEOR");
    /* a strip of cavern: bricks, a key, the miner, a guardian, the exit */
    {
        uint8_t c;
        for (c = 6; c < 26; c++) { tile_draw(c, 6, tiles + D_BRICK * 8); attr_set(c, 6, INK_RED | BRIGHT); }
        tile_draw(8, 5, miner_icon);  attr_set(8, 5, INK_WHITE | BRIGHT);
        tile_draw(12, 5, tiles + D_KEY * 8); attr_set(12, 5, INK_YELLOW | BRIGHT);
        tile_draw(16, 5, guard_icon); attr_set(16, 5, INK_CYAN | BRIGHT);
        tile_draw(19, 5, tiles + D_HAZARD * 8); attr_set(19, 5, INK_CYAN | BRIGHT);
        tile_draw(23, 5, tiles + D_EXIT * 8); attr_set(23, 5, INK_WHITE);
    }
    attr_fill(0, 8, 32, 3, INK_WHITE | BRIGHT);
    text_center(8, "COLLECT THE KEYS, THEN REACH");
    text_center(9, "THE EXIT BEFORE THE AIR RUNS OUT");
    text_center(10, "SIX CAVERNS  THREE LIVES");
    attr_fill(0, 12, 32, 1, INK_YELLOW | BRIGHT);
    text_center(12, "HIGH SCORES");
    attr_fill(0, 13, 32, 5, INK_WHITE | BRIGHT);
    hs_draw(10, 13);
    attr_fill(0, 19, 32, 1, INK_CYAN);
    text_center(19, "O/P Q/A CURSOR SINCLAIR KEMPSTON");
    text_center(20, "UP OR FIRE JUMPS");
}

/* ------------------------------------------------------------ logging */

static const char *const mode_names[5] = { "title", "playing", "won", "gameover", "cleared" };

static void log_state(void)
{
    static uint8_t last_mode = 255, last_level = 255, last_lives = 255, last_keys = 255;
    if (gs.mode != last_mode || gs.level_index != last_level || gs.lives != last_lives
        || gs.keys_remaining != last_keys) {
        zx_log("miner: state=%s level=%u lives=%u keys=%u score=%lu air=%d\n", mode_names[gs.mode],
               gs.level_index + 1, gs.lives, gs.keys_remaining, gs.score, gs.air);
        last_mode = gs.mode;
        last_level = gs.level_index;
        last_lives = gs.lives;
        last_keys = gs.keys_remaining;
    }
}

/* ------------------------------------------------------------ demo */

static uint8_t demo_on;                       /* playing demo_route instead of the keys */
static uint16_t demo_at;                      /* index into demo_route */
static uint8_t demo_left;                     /* ticks left in this entry */

static uint8_t demo_input(void)
{
    if (gs.mode != GM_PLAYING) return 0;
    if (gs.level_ticks == 0) {                /* a cavern starts: find its route */
        uint8_t l = 0;
        demo_at = 0;
        while (l < gs.level_index) if (demo_route[demo_at++][0] == 0xFF) l++;
        demo_left = demo_route[demo_at][1];
    }
    while (demo_left == 0 && demo_route[demo_at][0] != 0xFF) demo_left = demo_route[++demo_at][1];
    if (demo_route[demo_at][0] == 0xFF) return 0;
    demo_left--;
    return demo_route[demo_at][0];
}

/* ------------------------------------------------------------ main loop */

/* for test bots (read by symbol): what the game is doing, in one place */
struct {
    uint16_t level_ticks;
    uint8_t mode, level, lives, keys;
    int16_t x, y;
    uint16_t frames, ticks, overruns;         /* fps = 50 * frames / ticks */
} miner_probe;

/* PROFILE=1 builds paint the border per stage of the frame: red draw, green
   HUD, blue logic, magenta sprites/animation, black idle (one scanline is
   224 T-states) */
#ifdef PROFILE
#define STAGE(c) zx_border(c)
#else
#define STAGE(c) do { } while (0)
#endif

static void update_probe(void)
{
    miner_probe.level_ticks = gs.level_ticks;
    miner_probe.mode = gs.mode;
    miner_probe.level = gs.level_index;
    miner_probe.lives = gs.lives;
    miner_probe.keys = gs.keys_remaining;
    miner_probe.x = gs.player.b.x;
    miner_probe.y = gs.player.b.y;
}

static void isr_work(void)
{
    input_sample();
}

static uint8_t death_frames;                  /* port: the cavern flashes when you die */
/* port: frames spent redrawing a whole cavern are not logic time (the
   Amiga's load is instant): the next frame runs one tick */
static uint8_t resync;

static uint8_t read_input(uint8_t *start)
{
    uint8_t in = input_read(), p = input_pressed(), f = 0;
    if (in & IN_LEFT) f |= INPUT_LEFT;
    if (in & IN_RIGHT) f |= INPUT_RIGHT;
    if (in & (IN_UP | IN_FIRE)) f |= INPUT_JUMP;
    *start = (p & (IN_FIRE | IN_START)) != 0;
    if (*start) f |= INPUT_START;
    return f;
}

static void start_game_screen(void)
{
    game_screen();
    update_probe();
    resync = 1;
    if (ay_present) music_play(2, tune_ay, 9);
}

/* title: the tune on the beeper between key checks, the demo after a while */
static void title_loop(void)
{
    uint16_t idle = 0;
    uint8_t note = 0, start;
    title_screen();
    zx_log("miner: title\n");
    if (ay_present) music_play(2, tune_ay, 10);
    for (;;) {
        uint8_t blink;
        frame_wait();
        if (ay_present) sound_tick();
        read_input(&start);
        blink = (frame_count & 31) < 20;
        if ((frame_count & 15) == 0) {
            attr_fill(0, 21, 32, 1, blink ? (INK_WHITE | BRIGHT) : INK_BLACK);
            text_center(21, "PRESS FIRE TO START");
        }
        if (start) break;
        if (!ay_present && (frame_count & 7) == 0) {     /* a note every 8 frames, 2 frames long */
            beep(tune_beep[note], tune_cycles[note]);
            if (++note == TUNE_LEN) note = 0;
        }
        if (++idle == 600) {                  /* 12 s: the demo */
            demo_on = 1;
            zx_log("miner: demo start\n");
            break;
        }
    }
    game_tick(INPUT_START);
    log_state();
    start_game_screen();
}

static void end_screen(uint8_t won)
{
    music_stop();
    attr_fill(ROOM_COL, 6, 20, 4, won ? (PAPER_BLUE | INK_YELLOW | BRIGHT) : (PAPER_RED | INK_WHITE | BRIGHT));
    gfx_clear_rect(ROOM_COL, 6 * 8, 20, 32);
    text_big(won ? 8 : 7, 6, won ? "YOU WIN" : "GAME OVER");
    text_center(9, won ? "ALL CAVERNS CLEARED" : "THE MINE WINS");
    sound(won ? S_WIN : S_OVER);
}

int main(void)
{
    gfx_init();
    art_init();
    tune_init();
    input_init();
    input_fast();                             /* table key decode (sdk input_fast.c) */
    ay_init();
    hs_init(1000);                            /* table 5000 4000 3000 2000 1000 */
    game_init();
    cs_init(room, GRID_W, GRID_H, ROOM_COL, ROOM_ROW, tiles, tile_attr);
    cs_nspr = 4;                              /* three guardians and the miner */
    sprites_init();
    colours(0);
    frame_init();
    input_latch(1);
    frame_hook = isr_work;
    zx_log("miner: ready kempston=%u ay=%u\n", input_kempston, ay_present);
    title_loop();

    for (;;) {
        uint8_t ticks, t, f, start, prev_mode = gs.mode;
        STAGE(INK_BLACK);

        ticks = frame_wait();                 /* fixed timestep: the logic runs `ticks` times */
        if (ticks > 4) ticks = 4;
        if (resync) { ticks = 1; resync = 0; }
        miner_probe.ticks += ticks;
        miner_probe.frames++;
        if (ticks > 1) miner_probe.overruns += ticks - 1;

        /* draw first, while the beam is still in the top border */
        if (gs.mode == GM_PLAYING || gs.mode == GM_LEVELWIN) {
            STAGE(INK_RED);
            cs_update();
            STAGE(INK_GREEN);
            hud();
        }
        STAGE(INK_BLUE);
        if (ay_present) sound_tick();
        else fx_tick();

        f = read_input(&start);
        if (demo_on) {
            if (start || (input_last & IN_QUIT)) { demo_on = 0; music_stop(); zx_log("miner: demo stop\n"); game_init(); title_loop(); continue; }
            f = 0;
        }
        else if ((input_last & IN_QUIT) && gs.mode == GM_PLAYING) {   /* port: BREAK = give up */
            game_init();
            music_stop();
            title_loop();
            continue;
        }

        if (death_frames) {                   /* port: the death flash pauses the cavern */
            uint8_t a = (death_frames & 4) ? (PAPER_WHITE | INK_BLACK) : (PAPER_BLACK | INK_WHITE);
            attr_fill(ROOM_COL, ROOM_ROW, GRID_W, GRID_H, a);
            if (--death_frames == 0) {
                if (gs.mode == GM_LOSE) { room_build(); hud(); end_screen(0); }
                else { room_build(); hud_frame(); hud(); resync = 1; update_probe(); }
            }
            continue;
        }

        for (t = 0; t < ticks; t++) {
            if (t) f = read_input(&start) & ~INPUT_START;   /* each tick reads the keys */
            if (demo_on) f = demo_input();
            update_probe();                   /* what a test bot sees before each tick */
            game_tick(f);
            if (gs.events & EV_LOADED) {
                if (!(gs.events & EV_DIED)) { room_build(); hud_frame(); resync = 1; update_probe(); }
            }
            else room_changes();
            if (gs.events & EV_JUMP) sound(S_JUMP);
            if (gs.events & EV_KEY) sound(S_KEY);
            if (gs.events & EV_DIED) {
                zx_log("miner: died lives=%u level=%u\n", gs.lives, gs.level_index + 1);
                sound(S_DIE);
                death_frames = 24;
            }
            if (gs.events & EV_LEVEL) {
                zx_log("miner: cleared level=%u score=%lu\n", gs.level_index, gs.score);
                sound(S_LEVEL);
            }
            if (gs.events & EV_WON) end_screen(1);
            log_state();
            if (death_frames || gs.mode != prev_mode) break;
        }
        STAGE(INK_MAGENTA);
        if (gs.mode == GM_PLAYING) {
            sprites_from_game();
            animate((uint8_t)frame_count);
        }
        if (gs.mode == GM_LOSE && prev_mode != GM_LOSE)
            zx_log("miner: gameover score=%lu overruns=%u\n", gs.score, miner_probe.overruns);
        if (gs.mode == GM_TITLE && prev_mode != GM_TITLE) {   /* from the end screen */
            int8_t place = demo_on ? -1 : hs_rank(gs.score);
            demo_on = 0;
            music_stop();
            if (place >= 0) hs_enter(place, gs.score, "miner");
            game_init();
            title_loop();
        }
        if (demo_on && (gs.mode == GM_WIN || gs.mode == GM_LOSE) && gs.state_timer == 0) {
            demo_on = 0;
            zx_log("miner: demo done\n");
            game_init();
            title_loop();
        }
        update_probe();
        /* speed: frames drawn and logic ticks so far (fps = 50 * frames / ticks) */
        if ((miner_probe.frames & 127) == 0 && gs.mode == GM_PLAYING)
            zx_log("miner: perf frames=%u ticks=%u overruns=%u level=%u\n", miner_probe.frames,
                   miner_probe.ticks, miner_probe.overruns, gs.level_index + 1);
    }
}
