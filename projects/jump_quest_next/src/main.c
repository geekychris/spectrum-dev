/*
 * Jump Quest for the ZX Spectrum Next - port of amiga_mcp/examples/jump_quest
 * (RJ and Dale's platformer).
 *
 * The logic is the 48K project's jq.c (the original's, line for line, its C
 * versions here: at 28 MHz a tick is a few per cent of a frame); this file is
 * the Next front end, in the original's own layout and colours:
 *
 * - the tilemap (40 x 32 cells of 8 x 8, 4-bit) is the original's 320 x 256
 *   screen: rows 0-27 its 20 x 14 tiles of 16 pixels (each 2 x 2 cells), rows
 *   28-31 its HUD. Pen 0 (the sky) is transparent: the fallback colour shows.
 * - scrolling: the original's camera moves in whole tiles; the tilemap scrolls
 *   in hardware (NextReg 0x2F/0x30) and colring.h fills the columns that come
 *   into view. The copper sets the scroll at the top of the frame and puts it
 *   back to 0 at the HUD's first line.
 * - the player, the enemies, the items (bobbing) and the particles are
 *   hardware sprites, 4-bit patterns in the original's 16 colours.
 * - sound: the original's tune (melody and bass) and effects on the AY.
 *
 * Controls (README.md): LEFT/RIGHT walk, FIRE or UP jumps; keys, Sinclair, Kempston.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "jq.h"
#include "gfx_next.h"
#include "music_data.h"
#include "tmap.h"
#include "zxgame.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "nextsprl.h"
#include "nextcopper.h"
#include "colring.h"
#include "zxlog.h"

#include "demo.h"

/* ------------------------------------------------------------ tiles and palettes */

#define PLAY_ROWS   28              /* tilemap rows of the playfield (224 pixels) */
#define HUD_ROW     28
#define T_SKY2      (GFX_TILES)     /* solid tiles made here: the title's boxes */
#define T_YELLOW    (GFX_TILES + 1)
#define T_FONT      96              /* the ROM font on black (HUD): ' ' .. '_' */
#define T_TEXT      160             /* the same over the sky (transparent) */
#define T_BIG       224             /* the title's big letters, 4 tiles each */
static const char big_chars[] = "JUMPQEST";

/* tilemap palettes: 0 the original's colours; text: pen 1 the colour, pen 2 black */
enum { P_GAME, P_WHITE, P_YELLOW, P_GREY, P_RED, P_COUNT };
static const uint8_t text_pen[P_COUNT] = { 0, 11, 13, 9, 14 };   /* the original's COL_* */
#define COL_SKY 0
#define COL_SKY2 1
#define COL_BLACK 12

static uint8_t rom_font[768];

static void solid(uint8_t tile, uint8_t pen)
{
    uint8_t def[32];
    memset(def, (uint8_t)(pen << 4 | pen), 32);
    tm_def(tile, def, 1);
}

static void tiles_init(void)
{
    uint8_t i;
    tm_init();
    ZXN_WRITE_REG(0x4A, RGB8((gfx_pal[COL_SKY] >> 6) & 7, (gfx_pal[COL_SKY] >> 3) & 7,
                             (gfx_pal[COL_SKY] >> 1) & 3));       /* the fallback colour: the sky */
    for (i = 0; i < 16; i++) tm_pal(P_GAME, i, gfx_pal[i]);
    for (i = 1; i < P_COUNT; i++) {
        tm_pal(i, 1, gfx_pal[text_pen[i]]);
        tm_pal(i, 2, gfx_pal[COL_BLACK]);
    }
    tm_def(0, gfx_tiles, GFX_TILES);
    solid(T_SKY2, COL_SKY2);
    solid(T_YELLOW, 13);
    tm_font(T_FONT, rom_font, 64, 1, 2);
    tm_font(T_TEXT, rom_font, 64, 1, 0);
    for (i = 0; big_chars[i]; i++)
        tm_glyph2x(T_BIG + i * 4, rom_font + (big_chars[i] - 32) * 8, 1, 2);
    tm_clear();
    pal_load(2, gfx_pal, 16);               /* sprites: the same colours, pen 0 transparent */
    spl_init();
    spl_patterns(0, gfx_sprites, GFX_SPRITES);
}

static void print(uint8_t col, uint8_t row, const char *s, uint8_t pal)
{
    tm_print(col, row, s, T_FONT, pal);
}

static void print_sky(uint8_t col, uint8_t row, const char *s, uint8_t pal)
{
    tm_print(col, row, s, T_TEXT, pal);
}

static void big(uint8_t col, uint8_t row, const char *s, uint8_t pal)
{
    for (; *s; s++, col += 2) {
        const char *k = strchr(big_chars, *s);
        uint8_t t;
        if (!k) continue;
        t = (uint8_t)(T_BIG + (k - big_chars) * 4);
        tm_put(col, row, t, pal);
        tm_put(col + 1, row, t + 1, pal);
        tm_put(col, row + 1, t + 2, pal);
        tm_put(col + 1, row + 1, t + 3, pal);
    }
}

static void number(uint8_t col, uint8_t row, uint32_t v, uint8_t pal)
{
    char b[11];
    uint8_t i = 10;
    b[10] = 0;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v && i);
    print(col, row, b + i, pal);
}

/* ------------------------------------------------------------ the scrolling playfield */

static colring ring;
static uint16_t scroll_x = 0xFFFF;

/* the copper: the playfield's scroll from the top of the frame, 0 for the HUD */
static void copper_scroll(uint16_t x)
{
    if (x == scroll_x) return;
    scroll_x = x;
    copper_begin();
    copper_move(0x2F, (uint8_t)(x >> 8));       /* (the list starts in the top border) */
    copper_move(0x30, (uint8_t)x);
    copper_wait(L2Y_TO_LINE(HUD_ROW * 8) - 1, 50);
    copper_move(0x2F, 0);
    copper_move(0x30, 0);
    copper_end();
}

/* world column wx (8 pixels) into ring column rc: the level's tiles, rows 0-27 */
static void fill_col(void *ctx, uint16_t rc, uint32_t wx)
{
    uint8_t tx = (uint8_t)(wx >> 1), right = (uint8_t)(wx & 1), r;
    (void)ctx;
    for (r = 0; r < PLAY_ROWS; r++)
        tm_put((uint8_t)rc, r, (uint8_t)((TMAP(tx, r >> 1) << 2) | ((r & 1) << 1) | right), P_GAME);
}

static void play_screen(void)
{
    tm_fill(0, HUD_ROW, TM_COLS, 32 - HUD_ROW, GFX_T_BLACK, P_GAME);
    cr_init(&ring, TM_COLS, 0, fill_col, 0);
    cr_goto(&ring, (uint32_t)(game.cam_x >> 3));
    scroll_x = 0xFFFF;
    copper_scroll((uint16_t)ring.off << 3);
}

static void play_follow(void)
{
    cr_move(&ring, (uint32_t)(game.cam_x >> 3));
    copper_scroll((uint16_t)ring.off << 3);
}

/* a tile the logic changed (a ? block hit) */
static void play_tile(uint8_t tx, uint8_t ty)
{
    uint8_t k, r;
    for (k = 0; k < 2; k++) {
        uint16_t rc = cr_col(&ring, (uint32_t)tx * 2 + k);
        if (rc == CR_NONE) continue;
        for (r = ty * 2; r < ty * 2 + 2 && r < PLAY_ROWS; r++)
            tm_put((uint8_t)rc, r, (uint8_t)((TMAP(tx, ty) << 2) | ((r & 1) << 1) | k), P_GAME);
    }
}

/* text in the scrolled playfield, at a screen column */
static void play_text(uint8_t col, uint8_t row, const char *s, uint8_t pal)
{
    for (; *s; s++, col++)
        tm_put((uint8_t)((ring.off + col) % TM_COLS), row, (uint8_t)(T_FONT + *s - 32), pal);
}

/* ------------------------------------------------------------ sprites */

static uint8_t show_player, show_enemies;

static void put(int16_t x, int16_t y, uint8_t pat)
{
    spl_add(x, y, pat, 0, 0);
}

/* the frame's sprites in the original's drawing order: items, enemies, the
   player, the particles; each culled as its draw function culls */
static void sprites(void)
{
    uint8_t i;
    int16_t cam = game.cam_x;
    const Player *p = &game.player[game.active_player];
    spl_begin();
    {
        const Item *it = game.items;
        for (i = 0; i < MAX_ITEMS; i++, it++) {
            int16_t sx = it->x - cam, sy = it->y + it->float_off;
            if (!it->active || sx < 0 || sx + 16 > SCREEN_W || sy < 0 || sy + 16 > 224) continue;
            put(sx, sy, GFX_S_ITEM(it->type < 20 ? it->type - ENT_BOARD : it->type - ENT_BRIEFCASE + 5));
        }
    }
    if (show_enemies) {
        const Enemy *e = game.enemies;
        for (i = 0; i < MAX_ENEMIES; i++, e++) {
            int16_t sx = e->x - cam;
            if (!e->alive || sx < 0 || sx + 16 > SCREEN_W || e->y < 0 || e->y + 16 > 224) continue;
            put(sx, e->y, GFX_S_ENEMY(e->type, e->frame));
        }
    }
    if (show_player && !(p->invuln > 0 && (p->invuln & 4))) {
        uint8_t leg = p->state == 1 ? (p->walk_frame == 1 ? 1 : p->walk_frame == 3 ? 2 : 0) : 0;
        int16_t sx = p->x - cam;
        put(sx, p->y, GFX_S_PLAYER(p->character, p->dir < 0, leg, 0));
        if (p->y + 16 < 224)                    /* (the HUD is drawn over the rest) */
            put(sx, p->y + 16, GFX_S_PLAYER(p->character, p->dir < 0, leg, 1));
    }
    {
        const Particle *q = game.particles;
        for (i = 0; i < MAX_PARTICLES; i++, q++) {
            int16_t sx = q->x - cam;
            if (q->life > 0 && sx >= 0 && sx < SCREEN_W && q->y >= 0 && q->y < 224)
                put(sx, q->y, GFX_S_DOT);
        }
    }
    spl_end();
}

/* ------------------------------------------------------------ HUD (hud.c) */

static int32_t hud_score = -1;
static int8_t hud_lives = -1, hud_health = -1, hud_level = -1, hud_char = -1;

static void hud_reset(void)
{
    hud_score = -1;
    hud_lives = hud_health = hud_level = hud_char = -1;
}

/* the original's layout: name (4, 234), hearts (4 + 8i, 240), SCORE at 100,
   LIVES at 200, LV at 260 (pixels -> cells) */
static void hud(const Player *p)
{
    if (p->character != hud_char) {
        tm_fill(0, HUD_ROW, TM_COLS, 32 - HUD_ROW, GFX_T_BLACK, P_GAME);
        print(1, 29, p->character == CHAR_RJ ? "RJ" : "DALE", P_WHITE);
        print(12, 29, "SCORE", P_YELLOW);
        print(25, 29, "LIVES", P_YELLOW);
        print(32, 29, "LV", P_YELLOW);
        hud_char = p->character;
    }
    if (p->health != hud_health) {
        uint8_t i;
        for (i = 0; i < (uint8_t)p->max_health; i++)
            tm_put(1 + i, 30, i < (uint8_t)p->health ? GFX_T_HEART : GFX_T_HEART_EMPTY, P_GAME);
        hud_health = p->health;
    }
    if (p->score != hud_score) {
        number(12, 30, (uint32_t)p->score, P_WHITE);
        hud_score = p->score;
    }
    if (p->lives != hud_lives) {
        print(25, 30, "   ", P_WHITE);
        number(25, 30, (uint32_t)(p->lives < 0 ? 0 : p->lives), P_WHITE);
        hud_lives = p->lives;
    }
    if (game.current_level != hud_level) {
        number(35, 29, (uint32_t)(game.current_level < NUM_LEVELS ? game.current_level + 1 : NUM_LEVELS), P_WHITE);
        hud_level = game.current_level;
    }
}

/* ------------------------------------------------------------ sound */

enum { S_JUMP, S_STOMP, S_COLLECT, S_POWERUP, S_HURT, S_DIE, S_LEVEL };

/* channel 2: the original's SFX channel; its samples as AY sweeps */
static void sound(uint8_t s)
{
    switch (s) {
    case S_JUMP:    ay_sfx(2, 400, 13, 1, -18); break;
    case S_STOMP:   ay_sfx(2, 0, 15, 2, 0); break;
    case S_COLLECT: ay_sfx(2, 160, 12, 1, -6); break;
    case S_POWERUP: ay_sfx(2, 300, 14, 1, -10); break;
    case S_HURT:    ay_sfx(2, 250, 15, 1, 25); break;
    case S_DIE:     ay_sfx(2, 300, 15, 1, 12); break;
    case S_LEVEL:   ay_sfx(2, 250, 15, 1, -5); break;
    }
}

/* the original's tune (sound.c's sound_music_tick): channel 0 the melody,
   1 the bass, a row every MUSIC_SPEED ticks, a 0 silences the channel */
static uint8_t mus_on, mus_tick, mus_row, mus_pos;

static void music_start(void)
{
    mus_on = 1;
    mus_tick = MUSIC_SPEED - 1;
    mus_row = mus_pos = 0;
}

static void music_off(void)
{
    if (!mus_on) return;
    mus_on = 0;
    ay_off(0);
    ay_off(1);
}

static void music_tick(void)
{
    uint16_t at;
    uint8_t n;
    if (!mus_on || ++mus_tick < MUSIC_SPEED) return;
    mus_tick = 0;
    at = music_order[mus_pos] * MUSIC_PATTERN + mus_row;
    n = music_melody[at];
    if (n) ay_tone(0, music_period[n], 11);
    else ay_off(0);
    n = music_bass[at];
    if (n) ay_tone(1, music_period[n], 12);
    else ay_off(1);
    if (++mus_row == MUSIC_PATTERN) {
        mus_row = 0;
        if (++mus_pos == MUSIC_SONG) mus_pos = 0;
    }
}

/* ------------------------------------------------------------ title (title.c) */

static uint8_t sel_char, sel_players = 1, title_page;

static void title_bg(void)
{
    uint8_t c;
    copper_scroll(0);
    tm_clear();
    for (c = 0; c < TM_COLS; c++) {                 /* the ground: y 200 on */
        tm_put(c, 25, (TILE_GRASS << 2) | (c & 1), P_GAME);
        tm_put(c, 26, (TILE_GRASS << 2) | 2 | (c & 1), P_GAME);
    }
    tm_fill(0, 27, TM_COLS, 5, TILE_GROUND << 2, P_GAME);
    print_sky(5, 3, "RJ AND DALE'S", P_WHITE);
    big(10, 5, "JUMP QUEST", P_YELLOW);
}

/* the character boxes (draw_character_preview): sky2, yellow round the selected */
static void title_select(void)
{
    uint8_t c;
    for (c = 0; c < 2; c++) {
        uint8_t col = c ? 23 : 12, on = sel_char == c;
        tm_fill(col, 13, 6, 7, on ? T_YELLOW : 0, P_GAME);
        tm_fill(col + 1, 14, 4, 5, T_SKY2, P_GAME);
        print_sky(col + (c ? 1 : 2), 20, c ? "DALE" : "RJ", on ? P_YELLOW : P_WHITE);
    }
    print_sky(14, 10, sel_players == 1 ? "1 PLAYER " : "2 PLAYERS", P_WHITE);
}

static void title_page_show(uint8_t page)
{
    title_page = page;
    title_bg();
    if (page == 0) {
        print_sky(12, 11, "UP/DOWN:PLAYERS", P_GREY);
        print_sky(11, 22, "LEFT/RIGHT:SELECT", P_GREY);
        title_select();
    } else {
        uint8_t i;
        print_sky(14, 10, "HIGH SCORES", P_YELLOW);
        for (i = 0; i < HS_ENTRIES; i++) {
            char n[2] = { 0, 0 };
            n[0] = (char)('1' + i);
            print_sky(12, 12 + i * 2, n, P_WHITE);
            print_sky(15, 12 + i * 2, hs_table[i].name, P_WHITE);
            number(20, 12 + i * 2, hs_table[i].score, P_WHITE);
        }
    }
}

/* the previews (RJ and Dale standing, in their boxes) and the title's bugs */
static void title_sprites(void)
{
    spl_begin();
    if (title_page == 0) {
        put(12 * 8 + 16, 14 * 8 + 6, GFX_S_PLAYER(CHAR_RJ, 0, 0, 0));
        put(12 * 8 + 16, 14 * 8 + 22, GFX_S_PLAYER(CHAR_RJ, 0, 0, 1));
        put(23 * 8 + 16, 14 * 8 + 10, GFX_S_PLAYER(CHAR_DALE, 0, 0, 0));
        put(23 * 8 + 16, 14 * 8 + 26, GFX_S_PLAYER(CHAR_DALE, 0, 0, 1));
    }
    put(20, 184, GFX_S_ENEMY(ENT_BEETLE, (frame_count >> 3) & 1));
    put(280, 184, GFX_S_ENEMY(ENT_ANT, (frame_count >> 3) & 1));
    spl_end();
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

/* for test bots (read by symbol), as the 48K's */
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

static void isr_work(void)
{
    input_sample();
}

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

static uint8_t full_redraw;

static void game_screen(void)
{
    show_enemies = show_player = 1;
    tmap_init(game.tiles, levels[game.current_level].width, TILES_Y);
    play_screen();
    hud_reset();
    hud(&game.player[game.active_player]);
    sprites();
}

static void title_loop(void)
{
    uint16_t idle = 0;
    uint8_t start;
    title_page_show(0);
    zx_log("jq: title\n");
    music_start();
    input_read();
    for (;;) {
        uint8_t pr;
        frame_wait();
        music_tick();
        sound_tick();
        read_input(&start);
        pr = input_pressed();
        if (title_page == 0 && (pr & (IN_LEFT | IN_RIGHT))) {
            sel_char = (pr & IN_LEFT) ? CHAR_RJ : CHAR_DALE;
            title_select();
            idle = 0;
        }
        if (title_page == 0 && (pr & (IN_UP | IN_DOWN))) {
            sel_players = sel_players == 1 ? 2 : 1;
            title_select();
            idle = 0;
        }
        if ((frame_count & 15) == 0)
            print_sky(10, 24, (frame_count & 16) ? "PRESS FIRE TO START" : "                   ", P_RED);
        if (start) break;
        ++idle;
        if (idle == 300 || idle == 450) title_page_show(idle == 300 ? 1 : 0);
        if (idle == 600) {
            demo_on = 1;
            zx_log("jq: demo start\n");
            break;
        }
        title_sprites();
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
    /* LEVEL COMPLETE! at (80, 100), BONUS: +500 at (100, 120) */
    play_text(10, 12, "LEVEL COMPLETE!", P_WHITE);
    play_text(12, 15, "BONUS: +500", P_YELLOW);
}

static void gameover_screen(void)
{
    int32_t sv = game.player[0].score;
    if (game.num_players == 2) sv += game.player[1].score;
    music_off();
    copper_scroll(0);
    tm_fill(0, 0, TM_COLS, PLAY_ROWS, GFX_T_BLACK, P_GAME);
    spl_begin();
    spl_end();
    hud_reset();
    hud(&game.player[0]);
    if (game.current_level >= NUM_LEVELS) {
        print(12, 10, "CONGRATULATIONS!", P_YELLOW);
        print(11, 14, "YOU SAVED THE DAY!", P_WHITE);
    } else
        print(15, 10, "GAME OVER", P_RED);
    print(10, 18, "FINAL SCORE:", P_WHITE);
    number(23, 18, (uint32_t)sv, P_WHITE);
    zx_log("jq: gameover score=%ld won=%u overruns=%u\n", (long)sv, game.current_level >= NUM_LEVELS,
           jq_probe.overruns);
}

/* the initials for a high score (hs_edit_*: typed letters, or UP/DOWN + FIRE) */
static void initials(int8_t place, uint32_t score)
{
    hs_editor ed;
    uint8_t i;
    copper_scroll(0);
    tm_fill(0, 0, TM_COLS, TM_ROWS, GFX_T_BLACK, P_GAME);
    print(11, 8, "A NEW HIGH SCORE!", P_YELLOW);
    print(13, 11, "PLACE", P_GREY);
    {
        char n[2] = { 0, 0 };
        n[0] = (char)('1' + place);
        print(19, 11, n, P_WHITE);
    }
    number(22, 11, score, P_WHITE);
    print(11, 14, "TYPE YOUR INITIALS", P_GREY);
    print(9, 15, "(OR UP/DOWN AND FIRE)", P_GREY);
    zx_log("jq: hiscore rank=%d\n", place);
    hs_edit_begin(&ed);
    for (;;) {
        uint8_t r;
        frame_wait();
        input_read();
        r = hs_edit_step(&ed);
        for (i = 0; i < 3; i++) {
            char c[2] = { 0, 0 };
            c[0] = ed.name[i];
            print(16 + i * 3, 19, c, i == ed.pos ? P_YELLOW : P_WHITE);
            print(16 + i * 3, 20, i == ed.pos ? "-" : " ", P_YELLOW);
        }
        if (r & HS_EDIT_TAKEN) ay_sfx(2, 120, 12, 2, -4);
        if (r & HS_EDIT_DONE) break;
    }
    hs_insert(place, ed.name, score);
    zx_log("jq: name=%s\n", ed.name);
}

static void game_over_to_title(void)
{
    uint8_t i;
    music_off();
    if (!demo_on)
        for (i = 0; i < (uint8_t)game.num_players; i++) {
            int8_t place = hs_rank((uint32_t)game.player[i].score);
            if (place >= 0) initials(place, (uint32_t)game.player[i].score);
        }
    demo_on = 0;
    title_loop();
}

int main(void)
{
    memcpy(rom_font, (const void *)0x3D00, sizeof(rom_font));
    next_init();
    zx_border(INK_BLACK);
    ZXN_WRITE_REG(0x15, 0x01);                  /* sprites on, layer order S L U (sprites over the tilemap) */
    tiles_init();
    input_init();
    ay_init();
    hs_init(100);                               /* 500 400 300 200 100 */
    frame_init();
    input_latch(1);
    frame_hook = isr_work;
    zx_log("jq: ready kempston=%u ay=%u jump=%d\n", input_kempston, ay_present, JUMP_FORCE);
    title_loop();

    for (;;) {
        uint8_t ticks, t, f, start, prev_state;
        Player *p;
        ticks = frame_wait();                   /* fixed timestep: the logic runs `ticks` times */
        if (ticks > 6) ticks = 6;
        jq_probe.ticks += ticks;
        jq_probe.frames++;
        if (ticks > 1) jq_probe.overruns += ticks - 1;

        if (full_redraw) {
            full_redraw = 0;
            game_screen();
            ticks = 1;
        } else if (game.state != STATE_GAMEOVER) {
            uint16_t before = ring.off;
            sprites();                          /* (sent at once; the beam is in the top border) */
            play_follow();
            if (ring.off != before) jq_probe.scrolls++;
            hud(&game.player[game.active_player]);
        }
        music_tick();
        sound_tick();

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
            if (game.events & EV_TILE) play_tile(game.tile_tx, game.tile_ty);
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

        if (game.state != prev_state) {
            if (game.state == STATE_DYING) show_player = 0;     /* the original stops drawing the player */
            if (game.state == STATE_LEVELWIN) {                 /* and the enemies */
                show_enemies = 0;
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
        update_probe();
        if ((jq_probe.frames & 127) == 0 && game.state == STATE_PLAYING)
            zx_log("jq: perf frames=%u ticks=%u overruns=%u scrolls=%u level=%u\n", jq_probe.frames,
                   jq_probe.ticks, jq_probe.overruns, jq_probe.scrolls, game.current_level + 1);
    }
}
