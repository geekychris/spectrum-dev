/*
 * Jump Quest for the ZX Spectrum 48K - port of amiga_mcp/examples/jump_quest
 * (RJ and Dale's platformer).
 *
 * jq.c is the original's logic at a fixed 50 Hz; this file is the Spectrum
 * front end. port: the original's 16-pixel tiles stay 16 pixels (2 x 2
 * character cells, each with its own colours), 16 of them across instead of
 * 20. The 14 tile rows are 224 pixels and the screen 192: rows 1-12 are shown
 * (row 0 is clouds or the ceiling, row 13 the earth under the grass), the top
 * 8 pixels of row 1 behind the HUD. The view jumps half a screen when the
 * player nears its edge (view_target: scrolling cost more than a frame each
 * step, README.md). The player, the enemies and the particles are composed
 * per cell over the tiles (sdk cellspr48), one colour each; the items are
 * tiles.
 *
 * Controls (README.md): LEFT/RIGHT walk, FIRE or UP jumps; keys, Sinclair, Kempston.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "jq.h"
#include "gfx_data.h"
#include "music_data.h"
#include "tmap.h"
#include "cellspr48.h"
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"

#include "demo.h"

/* ------------------------------------------------------------ layout */

#define ROOM_ROW  1                 /* the view: screen rows 1..23 */
#define ROOM_W    32
#define ROOM_H    23
#define VIEW_Y    24                /* world y of the view's first pixel line */
#define VIEW_TILES 16
#define HUD_ROW   0

/* sprite slots, in drawing order (cellspr48: later sprites win the colour):
   the original draws the enemies, the player, the particles (the items are
   tiles: see view_items) */
#define SLOT_ENEMY  0
#define N_ENEMY     5
#define SLOT_PLAYER 5               /* then particles in 6..13 (5 too while dying) */
#define N_SLOTS     14

static uint8_t view[ROOM_W * ROOM_H];
static uint8_t view_tx;             /* the view's first tile column */

/* Sprites that move a pixel at a time and animate (the player, the beetles,
   flies and ants) get the one shifted copy they need, made when it changes
   (spr16_shift), in two buffers taken in turn so the picture's address changes
   too (cellspr48 redraws a sprite when it does). Things that only stand on
   whole tiles (items, the spider) need only the unshifted copy. */
#define N_SHIFTED (1 + N_ENEMY)          /* the player, then the enemy slots */
static uint8_t player_buf[2][3 * GFX_PLAYER_H];
static uint8_t enemy_buf[N_ENEMY][2][3 * 16];
static const uint8_t *shift_src[N_SHIFTED];
static uint8_t shift_x[N_SHIFTED], shift_bufi[N_SHIFTED];
static uint8_t pre_spider[2][3 * 16];
static uint8_t pre_dot[8 * 2 * 2];

/* sprite k's picture src (16 pixels wide, h lines) at pixel x */
static const uint8_t *shifted(uint8_t k, const uint8_t *src, uint8_t h, uint8_t x)
{
    uint8_t shift = x & 7, *buf = k ? enemy_buf[k - 1][0] : player_buf[0];
    uint16_t size = 3 * h;
    if (src != shift_src[k] || shift != shift_x[k]) {
        shift_src[k] = src;
        shift_x[k] = shift;
        shift_bufi[k] ^= 1;
        spr16_shift(buf + (shift_bufi[k] ? size : 0), src, h, shift);
    }
    /* cellspr48 picks copy x & 7 of eight: point that copy at the buffer */
    return buf + (shift_bufi[k] ? size : 0) - shift * size;
}

static void shift0(const uint8_t *src, uint8_t h, uint8_t *dst)
{
    while (h--) {
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = 0;
    }
}

static void art_init(void)
{
    uint8_t t, f;
    static const uint8_t dot[2] = { 0xC0, 0xC0 };
    for (f = 0; f < 2; f++) shift0(gfx_enemy + (2 * 2 + f) * 32, 16, pre_spider[f]);
    spr_preshift(dot, 1, 2, pre_dot);
}

/* ------------------------------------------------------------ the view */

/* the cell id of view row r (world line VIEW_Y + 8r), half h of tile t */
static uint8_t row_tile_ty[ROOM_H], row_half[ROOM_H];

static void view_rows_init(void)
{
    uint8_t r;
    for (r = 0; r < ROOM_H; r++) {
        row_tile_ty[r] = (uint8_t)((VIEW_Y / 8 + r) >> 1);
        row_half[r] = (uint8_t)(((VIEW_Y / 8 + r) & 1) << 1);
    }
}

static uint8_t cell_id(uint8_t tx, uint8_t r, uint8_t right)
{
    return (uint8_t)((TMAP(tx, row_tile_ty[r]) << 2) | row_half[r] | right);
}

/* port: the items are tiles (the original bobs them 2 pixels up and down;
   as cellspr48 sprites they cost more than the rest of the frame): an active
   item's four cells replace its (empty) tile's. Writes the item cells for
   tile columns first..first + n - 1 into map (2n cells a room row) */
static uint8_t item_on[MAX_ITEMS];          /* what the view shows */

static void view_items(uint8_t first, uint8_t n, uint8_t *map, uint8_t stride)
{
    uint8_t i, r;
    const Item *it = game.items;
    for (i = 0; i < MAX_ITEMS; i++, it++) {
        uint8_t tx = (uint8_t)(it->x >> 4), ty = (uint8_t)(it->y >> 4), id, *m;
        if (!it->active || (uint8_t)(tx - first) >= n) continue;
        id = (uint8_t)(GFX_ITEM_CELLS + ((it->type < 20 ? it->type - ENT_BOARD : it->type - ENT_BRIEFCASE + 5) << 2));
        m = map + ((tx - first) << 1);
        for (r = 0; r < ROOM_H; r++, m += stride)
            if (row_tile_ty[r] == ty) {
                m[0] = id | row_half[r];
                m[1] = id | row_half[r] | 1;
            }
    }
}

static int16_t view_x;                /* view_tx in pixels */

/* the view at tile column tx. jump: the screen shows a view already; only
   the cells that change are drawn (the sky mostly stays) */
static void view_build(uint8_t tx, uint8_t jump)
{
    uint8_t r, c, *v = view, i;
    view_tx = tx;
    view_x = (int16_t)tx << 4;
    if (jump) cs_wipe_sprites(0);
    for (r = 0; r < ROOM_H; r++) {
        const uint8_t *t = tmap_row[row_tile_ty[r]] + tx;
        uint8_t h = row_half[r];
        for (c = 0; c < ROOM_W; c += 2, v += 2) {
            uint8_t id = (uint8_t)((*t++ << 2) | h);
            if (!jump) {
                v[0] = id;
                v[1] = id | 1;
                continue;
            }
            if (v[0] != id) cs_put_tile(c, r, id);
            if (v[1] != (id | 1)) cs_put_tile(c + 1, r, id | 1);
        }
    }
    /* the items: their cells (in a jump, then drawn) */
    for (i = 0; i < MAX_ITEMS; i++) item_on[i] = game.items[i].active;
    if (!jump) {
        view_items(tx, VIEW_TILES, view, ROOM_W);
        return;
    }
    {
        const Item *it = game.items;
        for (i = 0; i < MAX_ITEMS; i++, it++) {
            uint8_t x = (uint8_t)(it->x >> 4), y = (uint8_t)(it->y >> 4), id;
            if (!it->active || (uint8_t)(x - tx) >= VIEW_TILES) continue;
            id = (uint8_t)(GFX_ITEM_CELLS + ((it->type < 20 ? it->type - ENT_BOARD : it->type - ENT_BRIEFCASE + 5) << 2));
            c = (uint8_t)((x - tx) << 1);
            for (r = 0; r < ROOM_H; r++)
                if (row_tile_ty[r] == y) {
                    cs_put_tile(c, r, id | row_half[r]);
                    cs_put_tile(c + 1, r, id | row_half[r] | 1);
                }
        }
    }
}

/* port: the view jumps half a screen (8 tiles) when the player gets near
   its edge: scrolling cost more than a frame each 16 pixels (README.md).
   The original's camera (cam_x) lags 60-odd pixels behind the player (it
   snaps to whole tiles every tick), which its 320-pixel screen could
   afford; this view keeps the player between 48 and 176 pixels from the
   left (80 or more ahead), up to the level's ends. */
#define VIEW_STEP 8

static uint8_t view_target(void)
{
    const Player *p = &game.player[game.active_player];
    int16_t sx = p->x - ((int16_t)view_tx << 4);
    uint8_t tx = view_tx, last = (uint8_t)(tmap_w - VIEW_TILES);
    while (sx > 176 && tx < last) { tx = tx + VIEW_STEP > last ? last : tx + VIEW_STEP; sx = p->x - ((int16_t)tx << 4); }
    while (sx < 48 && tx > 0) { tx = tx < VIEW_STEP ? 0 : tx - VIEW_STEP; sx = p->x - ((int16_t)tx << 4); }
    return tx;
}

static void view_follow(void)
{
    uint8_t tx = view_target();
    if (tx == view_tx) return;
    view_build(tx, 1);
}

/* ? blocks the player hit, redrawn after the scroll */
static uint8_t tile_q_tx[6], tile_q_ty[6], tile_qn;

/* items the player took since the last look: their tiles back */
static void items_taken(void)
{
    uint8_t i;
    const Item *it = game.items;
    for (i = 0; i < MAX_ITEMS; i++, it++)
        if (item_on[i] && !it->active && tile_qn < sizeof(tile_q_tx)) {
            item_on[i] = 0;
            tile_q_tx[tile_qn] = (uint8_t)(it->x >> 4);
            tile_q_ty[tile_qn++] = (uint8_t)(it->y >> 4);
        }
}

static void view_tiles(void)
{
    uint8_t i, r, c;
    for (i = 0; i < tile_qn; i++) {
        uint8_t tx = tile_q_tx[i], ty = tile_q_ty[i];
        if (tx < view_tx || tx >= view_tx + VIEW_TILES) continue;
        c = (uint8_t)((tx - view_tx) << 1);
        for (r = 0; r < ROOM_H; r++)
            if (row_tile_ty[r] == ty) {
                cs_set(c, r, cell_id(tx, r, 0));
                cs_set(c + 1, r, cell_id(tx, r, 1));
            }
    }
    tile_qn = 0;
}

/* ------------------------------------------------------------ sprites */

static uint8_t show_enemies, show_player;

static cs_sprite *slot;              /* the next sprite slot to fill */
extern uint8_t enemies_on_screen(int16_t view_x, uint8_t *list);   /* checks.asm */

/* would the original draw a sprite w bytes wide at world (wx, wy)? (its draw
   functions skip anything not wholly on the screen) */
static uint8_t visible(int16_t wx, int16_t wy, uint8_t w)
{
    return (uint16_t)(wx - view_x) <= (uint16_t)(SCREEN_W - 8 * w)
        && (uint16_t)(wy - VIEW_Y) <= 224 - 16 - VIEW_Y;
}

/* the next slot shows gfx at world (wx, wy), if visible */
static void put(int16_t wx, int16_t wy, const uint8_t *gfx, uint8_t ink, uint8_t w)
{
    uint16_t x = (uint16_t)(wx - view_x), y = (uint16_t)(wy - VIEW_Y);
    if (!visible(wx, wy, w)) return;
    slot->x = (uint8_t)x;
    slot->y = (uint8_t)y;
    slot->gfx = gfx;
    slot->ink = ink;
    slot->on = 1;
    slot++;
}

static void slots_off(cs_sprite *end)
{
    for (; slot < end; slot++) slot->on = 0;
}

static void sprites_from_game(void)
{
    uint8_t i;
    const Player *p = &game.player[game.active_player];

    slot = cs_spr + SLOT_ENEMY;
    if (show_enemies) {
        uint8_t list[N_ENEMY], n = enemies_on_screen(view_x, list), k;
        for (k = 0; k < n; k++) {
            const Enemy *e = &game.enemies[list[k]];
            uint8_t t = (uint8_t)(e->type - ENT_BEETLE);
            const uint8_t *g = t == 2 ? pre_spider[e->frame]
                : shifted(k + 1, gfx_enemy + ((t * 2 + e->frame) << 5), 16, (uint8_t)(e->x - view_x));
            put(e->x, e->y, g, gfx_enemy_ink[t], 2);
        }
    }
    slots_off(cs_spr + SLOT_PLAYER);

    /* the player (player_draw: not drawn while dying, blinks when hurt) */
    slot->on = 0;
    if (show_player) {
        if (!(p->invuln > 0 && (p->invuln & 4))) {
            uint8_t leg = p->state == 1 ? (p->walk_frame == 1 ? 1 : p->walk_frame == 3 ? 2 : 0) : 0;
            int16_t y = p->y < VIEW_Y ? VIEW_Y : p->y;     /* port: no higher than the HUD */
            uint8_t frame = (uint8_t)((p->character * 2 + (p->dir < 0)) * 3 + leg);
            if (visible(p->x, y, 2))
                put(p->x, y, shifted(0, gfx_player + frame * (GFX_PLAYER_H * 2), GFX_PLAYER_H,
                                     (uint8_t)(p->x - view_x)), INK_WHITE, 2);
        }
        slot = cs_spr + SLOT_PLAYER + 1;
    }

    /* particles: 2x2 dots in the slots left (only after a death: the
       original spawns them there and nowhere else) */
    if (!show_player) {
        const Particle *q = game.particles;
        for (i = 0; i < MAX_PARTICLES && slot < cs_spr + N_SLOTS; i++, q++)
            if (q->life > 0) put(q->x, q->y, pre_dot, INK_RED, 1);
    }
    slots_off(cs_spr + N_SLOTS);
}

static void sprites_init(void)
{
    uint8_t i;
    for (i = 0; i < N_SLOTS; i++) {
        cs_spr[i].w = 2;
        cs_spr[i].h = 16;
        cs_spr[i].keep = 0x38;                  /* the cell keeps its paper */
    }
    cs_spr[SLOT_PLAYER].h = GFX_PLAYER_H;
}

/* the player and particle slots change size (the player's is 28 lines, a dot 2) */
static void slot_sizes(void)
{
    uint8_t i;
    for (i = SLOT_PLAYER; i < N_SLOTS; i++) {
        cs_spr[i].w = 1;
        cs_spr[i].h = 2;
    }
    if (show_player) {
        cs_spr[SLOT_PLAYER].w = 2;
        cs_spr[SLOT_PLAYER].h = GFX_PLAYER_H;
    }
}

/* ------------------------------------------------------------ HUD (hud.c) */

static int32_t hud_score = -1;
static int8_t hud_lives = -1, hud_health = -1, hud_level = -1, hud_char = -1;

static void hud_reset(void)
{
    hud_score = -1;
    hud_lives = hud_health = hud_level = hud_char = -1;
}

static void hud(const Player *p)
{
    char b[2] = { 0, 0 };
    if (p->character != hud_char) {
        attr_fill(0, HUD_ROW, 32, 1, PAPER_BLACK | INK_WHITE | BRIGHT);
        gfx_clear_rect(0, 0, 32, 8);
        zx_print_at(0, HUD_ROW, p->character == CHAR_RJ ? "RJ" : "DALE");
        attr_fill(11, HUD_ROW, 5, 1, PAPER_BLACK | INK_YELLOW | BRIGHT);
        zx_print_at(11, HUD_ROW, "SCORE");
        attr_fill(24, HUD_ROW, 2, 1, PAPER_BLACK | INK_YELLOW | BRIGHT);
        zx_print_at(24, HUD_ROW, "x");
        attr_fill(28, HUD_ROW, 2, 1, PAPER_BLACK | INK_YELLOW | BRIGHT);
        zx_print_at(28, HUD_ROW, "LV");
        hud_char = p->character;
    }
    if (p->health != hud_health) {
        uint8_t i;
        for (i = 0; i < (uint8_t)p->max_health; i++) {
            tile_draw(5 + i, HUD_ROW, gfx_heart + 8);
            attr_set(5 + i, HUD_ROW, i < (uint8_t)p->health ? (PAPER_BLACK | INK_RED | BRIGHT) : (PAPER_BLACK | INK_RED));
        }
        hud_health = p->health;
    }
    if (p->score != hud_score) {
        text_num(17, HUD_ROW, (uint32_t)p->score, 6);
        hud_score = p->score;
    }
    if (p->lives != hud_lives) {
        b[0] = (char)('0' + (p->lives > 9 ? 9 : p->lives));
        zx_print_at(25, HUD_ROW, b);
        hud_lives = p->lives;
    }
    if (game.current_level != hud_level) {
        b[0] = (char)('1' + (game.current_level < NUM_LEVELS ? game.current_level : NUM_LEVELS - 1));
        zx_print_at(31, HUD_ROW, b);
        hud_level = game.current_level;
    }
}

/* ------------------------------------------------------------ sound */

enum { S_JUMP, S_STOMP, S_COLLECT, S_POWERUP, S_HURT, S_DIE, S_LEVEL };

/* beeper effects play a chunk a frame ({period, cycles} pairs, 0 ends; a
   chunk is about 4,000 T-states), so an effect never costs the game a frame;
   the longer jingles play while the game waits anyway (dying, level done).
   They follow the original's samples: a rising blip, a thud, a sparkle, a
   fanfare, a falling buzz */
static const uint8_t fx_jump[] = { 60, 2, 50, 2, 42, 2, 36, 3, 30, 3, 0 };
static const uint8_t fx_stomp[] = { 90, 2, 110, 2, 130, 2, 150, 1, 0 };
static const uint8_t fx_collect[] = { 22, 5, 18, 6, 14, 7, 0 };
static const uint8_t fx_powerup[] = { 40, 3, 34, 3, 28, 4, 22, 5, 18, 6, 14, 7, 0 };
static const uint8_t fx_hurt[] = { 70, 2, 90, 2, 110, 2, 130, 2, 150, 1, 0 };
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
    if (ay_present) {                           /* channel 2: the original's SFX channel */
        switch (s) {
        case S_JUMP:    ay_sfx(2, 400, 13, 1, -18); break;
        case S_STOMP:   ay_sfx(2, 0, 15, 2, 0); break;
        case S_COLLECT: ay_sfx(2, 160, 12, 1, -6); break;
        case S_POWERUP: ay_sfx(2, 300, 14, 1, -10); break;
        case S_HURT:    ay_sfx(2, 250, 15, 1, 25); break;
        case S_DIE:     ay_sfx(2, 300, 15, 1, 12); break;
        case S_LEVEL:   ay_sfx(2, 250, 15, 1, -5); break;
        }
        return;
    }
    switch (s) {
    case S_JUMP:    if (!fx_at) fx_at = fx_jump; break;
    case S_STOMP:   fx_at = fx_stomp; break;
    case S_COLLECT: fx_at = fx_collect; break;
    case S_POWERUP: fx_at = fx_powerup; break;
    case S_HURT:    fx_at = fx_hurt; break;
    case S_DIE:     fx_at = 0; beep_sweep(40, 220, 6, 3); break;
    case S_LEVEL:   fx_at = 0; beep(60, 20); beep(45, 25); beep(36, 30); beep(30, 35); beep(22, 50); break;
    }
}

/* the original's tune on the AY (sound.c's sound_music_tick): channel 0 the
   melody, 1 the bass, a row every MUSIC_SPEED ticks, a 0 silences the channel */
static uint8_t mus_on, mus_tick, mus_row, mus_pos;

static void music_start(void)
{
    if (!ay_present) return;
    mus_on = 1;
    mus_tick = MUSIC_SPEED - 1;                 /* the first tick plays the first row */
    mus_row = mus_pos = 0;
}

static void music_off(void)
{
    if (!mus_on) return;
    mus_on = 0;
    ay_off(0);
    ay_off(1);
}

/* the note of a channel's pattern (melody or bass) at the song position */
static uint8_t music_note(const uint8_t *pats, uint8_t pos, uint8_t row)
{
    return pats[music_order[pos] * MUSIC_PATTERN + row];
}

static void music_tick(void)
{
    uint8_t n;
    if (!mus_on || ++mus_tick < MUSIC_SPEED) return;
    mus_tick = 0;
    n = music_note(music_melody, mus_pos, mus_row);
    if (n) ay_tone(0, music_period[n], 11);
    else ay_off(0);
    n = music_note(music_bass, mus_pos, mus_row);
    if (n) ay_tone(1, music_period[n], 12);
    else ay_off(1);
    if (++mus_row == MUSIC_PATTERN) {
        mus_row = 0;
        if (++mus_pos == MUSIC_SONG) mus_pos = 0;
    }
}

/* ------------------------------------------------------------ title (title.c) */

static uint8_t sel_char, sel_players = 1;

/* a character's standing picture, unshifted, at a cell column and pixel line */
static void draw_figure(uint8_t col, uint8_t y, uint8_t character)
{
    const uint8_t *g = gfx_player + (character * 2) * 3 * GFX_PLAYER_H * 2;
    uint8_t i;
    for (i = 0; i < GFX_PLAYER_H; i++, g += 2) {
        zx_row[y + i][col] = g[0];
        zx_row[y + i][col + 1] = g[1];
    }
}

static void title_select(void)
{
    uint8_t c;
    for (c = 0; c < 2; c++) {
        uint8_t col = c ? 18 : 12, on = sel_char == c;
        attr_fill(col - 1, 7, 4, 5, on ? (PAPER_YELLOW | INK_BLACK) : (PAPER_CYAN | INK_WHITE));
        attr_fill(col, 8, 2, 4, PAPER_CYAN | INK_WHITE);
        attr_fill(col - 1, 12, 4, 1, on ? (PAPER_BLACK | INK_YELLOW | BRIGHT) : (PAPER_BLACK | INK_WHITE));
    }
    attr_fill(0, 5, 32, 1, PAPER_BLACK | INK_WHITE | BRIGHT);
    zx_print_at(0, 5, "                                ");
    text_center(5, sel_players == 1 ? "1 PLAYER" : "2 PLAYERS");
}

static void title_screen(void)
{
    uint8_t c;
    screen_clear(PAPER_BLACK | INK_WHITE, INK_BLACK);
    attr_fill(0, 0, 32, 1, PAPER_BLACK | INK_WHITE | BRIGHT);
    text_center(0, "RJ AND DALE'S");
    attr_fill(0, 2, 32, 2, PAPER_BLACK | INK_YELLOW | BRIGHT);
    text_big(6, 2, "JUMP QUEST");
    draw_figure(12, 8 * 8, CHAR_RJ);
    draw_figure(18, 8 * 8 + 4, CHAR_DALE);
    zx_print_at(11, 12, " RJ ");
    zx_print_at(17, 12, "DALE");
    attr_fill(0, 6, 32, 1, PAPER_BLACK | INK_CYAN);
    text_center(6, "UP/DOWN: PLAYERS");
    attr_fill(0, 13, 32, 1, PAPER_BLACK | INK_CYAN);
    text_center(13, "LEFT/RIGHT: SELECT");
    /* a strip of the Office Park: grass, a beetle, an ant */
    for (c = 0; c < 32; c++) {
        tile_draw(c, 14, gfx_tile_cells + (TILE_GRASS * 4 + (c & 1)) * 8);
        attr_set(c, 14, gfx_tile_attr[TILE_GRASS * 4 + (c & 1)]);
    }
    attr_fill(0, 15, 32, 1, PAPER_BLACK | INK_YELLOW | BRIGHT);
    text_center(15, "HIGH SCORES");
    attr_fill(0, 16, 32, 5, PAPER_BLACK | INK_WHITE | BRIGHT);
    hs_draw(10, 16);
    attr_fill(0, 23, 32, 1, PAPER_BLACK | INK_CYAN);
    text_center(23, "O/P Q/A CURSOR SINCLAIR KEMPSTON");
    title_select();
}

/* ------------------------------------------------------------ logging, probe */

static const char *const state_names[5] = { "title", "playing", "dying", "levelwin", "gameover" };

static void log_state(void)
{
    static int8_t last_state = -1, last_level = -1, last_lives = -1, last_health = -1, last_player = -1;
    const Player *p = &game.player[game.active_player];
    if (game.state != last_state || game.current_level != last_level || p->lives != last_lives
        || p->health != last_health || game.active_player != last_player) {
        zx_log("jq: state=%s level=%u player=%u lives=%d health=%d score=%ld\n", state_names[game.state],
               game.current_level + 1, game.active_player + 1, p->lives, p->health, (long)p->score);
        last_state = game.state;
        last_level = game.current_level;
        last_lives = p->lives;
        last_health = p->health;
        last_player = game.active_player;
    }
}

/* for test bots (read by symbol): what the game is doing, in one place */
struct {
    uint16_t level_ticks;
    uint8_t state, level, lives, health;
    int16_t x, y, cam_x;
    uint16_t frames, ticks, overruns;   /* fps = 50 * frames / ticks */
    uint16_t scrolls;
} jq_probe;

static void update_probe(void)
{
    const Player *p = &game.player[game.active_player];
    jq_probe.level_ticks = game.level_ticks;
    jq_probe.state = (uint8_t)game.state;
    jq_probe.level = (uint8_t)game.current_level;
    jq_probe.lives = (uint8_t)p->lives;
    jq_probe.health = (uint8_t)p->health;
    jq_probe.x = p->x;
    jq_probe.y = p->y;
    jq_probe.cam_x = game.cam_x;
}

/* ------------------------------------------------------------ demo */

static uint8_t demo_on;
static uint16_t demo_at;
static uint8_t demo_left;

static uint8_t demo_input(void)
{
    if (game.state != STATE_PLAYING) return 0;
    if (game.level_ticks == 0) {              /* a level starts (before its first tick): its route */
        uint8_t l = 0;
        demo_at = 0;
        while (l < game.current_level) if (demo_route[demo_at++][0] == 0xFF) l++;
        demo_left = demo_route[demo_at][1];
    }
    while (demo_left == 0 && demo_route[demo_at][0] != 0xFF) demo_left = demo_route[++demo_at][1];
    if (demo_route[demo_at][0] == 0xFF) return 0;
    demo_left--;
    return demo_route[demo_at][0];
}

/* ------------------------------------------------------------ main loop */

#ifdef PROFILE
#define STAGE(c) zx_border(c)
#else
#define STAGE(c) do { } while (0)
#endif

static void isr_work(void)
{
    input_sample();
}

/* the original's input bits. port: read once a frame (presses latched by the
   interrupt); a slow frame's extra ticks take the same keys */
static uint8_t keys_to_inp(uint8_t in)
{
    uint8_t f = 0;
    if (in & IN_LEFT) f |= INP_LEFT;
    if (in & IN_RIGHT) f |= INP_RIGHT;
    if (in & IN_DOWN) f |= INP_DOWN;
    if (in & (IN_UP | IN_FIRE)) f |= INP_JUMP;   /* port: UP jumps too (Q, joystick up) */
    if (in & IN_START) f |= INP_START;
    return f;
}

static uint8_t read_input(uint8_t *start)
{
    uint8_t in = input_read();
    *start = (input_pressed() & (IN_FIRE | IN_START)) != 0;
    return keys_to_inp(in);
}

static uint8_t resync, full_redraw;

static void game_screen(void)
{
    const Player *p = &game.player[game.active_player];
    memset(shift_src, 0, sizeof(shift_src));
    show_enemies = show_player = 1;
    slot_sizes();
    tmap_init(game.tiles, levels[game.current_level].width, TILES_Y);
    view_tx = 0;                                /* a level starts at its left end */
    view_build(view_target(), 0);
    zx_border(INK_BLACK);
    hud_reset();
    hud(p);
    sprites_from_game();
    cs_draw_all();
    tile_qn = 0;
    resync = 1;
}

/* the title: character and players, the tune; the demo after 12 s */
static void title_loop(void)
{
    uint16_t idle = 0;
    uint8_t row = 0, prow = 0, pos = 0, start;
    title_screen();
    zx_log("jq: title\n");
    music_start();
    input_read();
    for (;;) {
        uint8_t pr;
        frame_wait();
        if (ay_present) { music_tick(); sound_tick(); }
        read_input(&start);
        pr = input_pressed();
        if (pr & (IN_LEFT | IN_RIGHT)) {
            sel_char = (pr & IN_LEFT) ? CHAR_RJ : CHAR_DALE;
            title_select();
            idle = 0;
        }
        if (pr & (IN_UP | IN_DOWN)) {
            sel_players = sel_players == 1 ? 2 : 1;
            title_select();
            idle = 0;
        }
        if ((frame_count & 15) == 0) {
            uint8_t on = (frame_count & 16) != 0;
            attr_fill(0, 22, 32, 1, on ? (PAPER_BLACK | INK_RED | BRIGHT) : (PAPER_BLACK | INK_BLACK));
            text_center(22, "PRESS FIRE TO START");
        }
        if (start) break;
        if (!ay_present && ++row == MUSIC_SPEED) {   /* the melody on the beeper */
            uint8_t n = music_note(music_melody, pos, prow);
            row = 0;
            if (n) {
                uint16_t bp = (uint16_t)(((uint32_t)music_period[n] * 155) >> 8) - 2;
                beep(bp, (uint16_t)(14000UL / (26UL * bp + 40)));
            }
            if (++prow == MUSIC_PATTERN) { prow = 0; if (++pos == MUSIC_SONG) pos = 0; }
        }
        if (++idle == 600) {
            demo_on = 1;
            zx_log("jq: demo start\n");
            break;
        }
    }
    music_off();
    if (demo_on) jq_new_game(CHAR_RJ, 1);
    else jq_new_game(sel_char, sel_players);
    zx_log("jq: start character=%u players=%u demo=%u\n", game.player[0].character, game.num_players, demo_on);
    log_state();
    music_start();
    game_screen();
    update_probe();
}

static void level_text(void)
{
    /* LEVEL COMPLETE! / BONUS: +500, as the original prints them */
    attr_fill(8, 11, 16, 1, PAPER_BLUE | INK_WHITE | BRIGHT);
    zx_print_at(8, 11, "LEVEL COMPLETE! ");
    attr_fill(10, 13, 12, 1, PAPER_BLUE | INK_YELLOW | BRIGHT);
    zx_print_at(10, 13, " BONUS: +500");
}

static void gameover_screen(void)
{
    int32_t sv = game.player[0].score;
    if (game.num_players == 2) sv += game.player[1].score;
    music_off();
    screen_clear(PAPER_BLACK | INK_WHITE, INK_BLACK);
    hud_reset();
    hud(&game.player[0]);
    if (game.current_level >= NUM_LEVELS) {
        attr_fill(0, 8, 32, 2, PAPER_BLACK | INK_YELLOW | BRIGHT);
        text_big(0, 8, "CONGRATULATIONS!");
        attr_fill(0, 12, 32, 1, PAPER_BLACK | INK_WHITE | BRIGHT);
        text_center(12, "YOU SAVED THE DAY!");
    } else {
        attr_fill(0, 9, 32, 2, PAPER_BLACK | INK_RED | BRIGHT);
        text_big(7, 9, "GAME OVER");
    }
    attr_fill(0, 16, 32, 1, PAPER_BLACK | INK_WHITE | BRIGHT);
    zx_print_at(8, 16, "FINAL SCORE:");
    text_num(21, 16, (uint32_t)sv, 6);
    zx_log("jq: gameover score=%ld won=%u overruns=%u\n", (long)sv, game.current_level >= NUM_LEVELS,
           jq_probe.overruns);
}

static void game_over_to_title(void)
{
    uint8_t i;
    music_off();
    if (!demo_on)
        for (i = 0; i < (uint8_t)game.num_players; i++) {
            int8_t place = hs_rank((uint32_t)game.player[i].score);
            if (place >= 0) hs_enter(place, (uint32_t)game.player[i].score, "jq");
        }
    demo_on = 0;
    title_loop();
}

/* the stack (down from 0xFF58) must never reach the game's state (jq.h,
   up to 0xFCBA): 0xFCC0-0xFDFF is filled at the start, test.py checks it */
#define CANARY 0xFCC0
#define CANARY_LEN 0x140

int main(void)
{
    memset((void *)CANARY, 0xA5, CANARY_LEN);
    memset(&game, 0, sizeof(Game));
    gfx_init();
    art_init();
    view_rows_init();
    input_init();
    input_fast();
    ay_init();
    hs_init(100);                               /* 500 400 300 200 100 */
    cs_init(view, ROOM_W, ROOM_H, 0, ROOM_ROW, gfx_tile_cells, gfx_tile_attr);
    cs_nspr = N_SLOTS;
    sprites_init();
    frame_init();
    input_latch(1);
    frame_hook = isr_work;
    zx_log("jq: ready kempston=%u ay=%u jump=%d\n", input_kempston, ay_present, JUMP_FORCE);
    title_loop();

    for (;;) {
        uint8_t ticks, t, f, start, prev_state;
        Player *p;
        STAGE(INK_BLACK);
        ticks = frame_wait();                   /* fixed timestep: the logic runs `ticks` times */
        if (ticks > 25) ticks = 25;           /* catch-up cap: a level start, not a busy frame */
        if (resync) { ticks = 1; resync = 0; }
        jq_probe.ticks += ticks;
        jq_probe.frames++;
        if (ticks > 1) jq_probe.overruns += ticks - 1;

        /* draw what the last frame's logic left, while the beam is in the top border */
        STAGE(INK_RED);
        if (full_redraw) {
            full_redraw = 0;
            game_screen();
            ticks = 1;
        } else if (game.state != STATE_GAMEOVER) {
            uint8_t before = view_tx;
            view_follow();
            if (view_tx != before) jq_probe.scrolls++;
            view_tiles();
            cs_update();
            STAGE(INK_GREEN);
            hud(&game.player[game.active_player]);
        }
        STAGE(INK_BLUE);
        if (ay_present) { music_tick(); sound_tick(); }
        else fx_tick();

        f = read_input(&start);
        if (demo_on && (start || (input_last & IN_QUIT))) {
            zx_log("jq: demo stop\n");
            demo_on = 0;
            music_off();
            title_loop();
            continue;
        }
        if (!demo_on && (input_last & IN_QUIT) && game.state == STATE_PLAYING) {   /* port: BREAK gives up */
            music_off();
            title_loop();
            continue;
        }

        prev_state = game.state;
        for (t = 0; t < ticks; t++) {
            if (demo_on) f = demo_input();
            update_probe();                     /* what a test bot sees before each tick */
            jq_tick(f);
            if (game.events & (EV_COLLECT | EV_POWERUP)) items_taken();
            if (game.events & EV_TILE && tile_qn < sizeof(tile_q_tx)) {
                tile_q_tx[tile_qn] = game.tile_tx;
                tile_q_ty[tile_qn++] = game.tile_ty;
            }
            if (game.events & EV_JUMP) sound(S_JUMP);
            if (game.events & EV_STOMP) sound(S_STOMP);
            if (game.events & EV_COLLECT) sound(S_COLLECT);
            if (game.events & EV_POWERUP) sound(S_POWERUP);
            if (game.events & EV_HURT) sound(S_HURT);
            if (game.events & EV_DIE) {
                p = &game.player[game.active_player];
                zx_log("jq: died lives=%d level=%u x=%d\n", p->lives - 1, game.current_level + 1, p->x);
                sound(S_DIE);
            }
            if (game.events & EV_LEVELWIN) {
                zx_log("jq: cleared level=%u score=%ld ticks=%u\n", game.current_level + 1,
                       (long)game.player[game.active_player].score, game.level_ticks);
                sound(S_LEVEL);
            }
            if (game.events & EV_LEVEL) full_redraw = 1;
            log_state();
            if (game.state != prev_state || full_redraw) break;
        }

        STAGE(INK_MAGENTA);
        if (game.state != prev_state) {
            if (game.state == STATE_DYING) {    /* the original stops drawing the player */
                show_player = 0;
                slot_sizes();
            }
            if (game.state == STATE_LEVELWIN) { /* and the enemies */
                show_enemies = 0;
                sprites_from_game();
                cs_update();
                level_text();
            }
            if (game.state == STATE_GAMEOVER) gameover_screen();
            if (game.state == STATE_TITLE) { game_over_to_title(); continue; }
        }
        if (demo_on && game.state == STATE_GAMEOVER && game.transition <= 0) {
            zx_log("jq: demo done\n");
            demo_on = 0;
            title_loop();
            continue;
        }
        if (game.state == STATE_PLAYING || game.state == STATE_DYING) sprites_from_game();
        update_probe();
        if ((jq_probe.frames & 63) == 0 && game.state == STATE_PLAYING)
            zx_log("jq: perf frames=%u ticks=%u overruns=%u scrolls=%u level=%u\n", jq_probe.frames,
                   jq_probe.ticks, jq_probe.overruns, jq_probe.scrolls, game.current_level + 1);
    }
}
