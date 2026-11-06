/*
 * StakAttack - Spectrum Next front end.
 *
 * game.c is the original's logic, unchanged; this file replaces the Amiga
 * main.c/draw.c/input.c:
 *   Layer 2 (256x192, double buffered): the well, the HUD and the screens,
 *     drawn with the original's 16 colours and 5x7 font. Each buffer keeps a
 *     copy of what it shows, so a frame only redraws the cells that changed.
 *   hardware sprites: the falling piece, its landing marks and the next piece
 *   TurboSound: the original's MOD (tools/mod2ay.py) on two AY chips, the
 *     effects on the first chip's third channel
 * The game runs at 50 Hz, as on the Amiga (one game_update per frame).
 */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "game.h"
#include "font5.h"
#include "modplay.h"

/* port: 320x256 with 12-pixel cells becomes 256x192 with 9-pixel cells */
#define CELL     9
#define FIELD_X  128
#define FIELD_Y  6
#define FW       (FIELD_W * CELL)
#define FH       (FIELD_H * CELL)
#define HX       10                 /* HUD column */
#define NEXT_X   (HX + 15)
#define NEXT_Y   110

/* the original's palette (12-bit Amiga RGB) */
static const uint16_t palette[16] = {
    0x112, 0xE4A, 0x3BB, 0xDA3, 0x96F, 0x6D6, 0x5AF, 0xF75,
    0x888, 0xFFF, 0x667, 0x112, 0xFF0, 0xC85, 0xFD8, 0x223
};

GameState gs;

/* ------------------------------------------------------------ drawing */

#define draw_text   l2_text5          /* the SDK's 5x7 font (the original's) */
#define draw_center l2_text5_center
#define text_width  font5_width

static char *num_str(uint32_t v)
{
    static char buf[11];
    char *p = buf + 10;
    *p = 0;
    do { *--p = '0' + (uint8_t)(v % 10); v /= 10; } while (v);
    return p;
}

/* a gem: rounded body, dark lower edge, a white glint top-left (draw.c) */
static uint8_t gem[8][64];          /* 8x8 Layer 2 images, pens 1..7 */
static uint8_t pattern[256];

static void make_gem(uint8_t *d, uint8_t pen, uint8_t stride)
{
    uint8_t r, c, v;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++) {
            v = pen;
            if ((r == 0 || r == 7) && (c == 0 || c == 7)) v = L2_TRANSPARENT;
            else if (r == 6 && c >= 2 && c <= 5) v = 11;
            else if (r >= 1 && r <= 2 && c >= 1 && c <= 2) v = 9;
            d[r * stride + c] = v;
        }
}

static void make_graphics(void)
{
    uint8_t i;
    for (i = 0; i < 16; i++) {
        uint16_t a = palette[i];
        uint16_t c = ((a >> 9) & 7) << 6 | ((a >> 5) & 7) << 3 | ((a >> 1) & 7);
        pal_set(1, i, c);
        pal_set(2, i, c);
    }
    spr_init();
    for (i = 1; i <= 7; i++) {
        make_gem(gem[i], i, 8);
        memset(pattern, L2_TRANSPARENT, 256);
        make_gem(pattern, i, 16);
        spr_pattern(i - 1, pattern);
    }
    /* landing marks: a small dot in the cell */
    memset(pattern, L2_TRANSPARENT, 256);
    for (i = 0; i < 9; i++) pattern[(3 + i / 3) * 16 + 3 + i % 3] = 10;
    spr_pattern(7, pattern);
}

static void draw_gem(uint8_t x, uint8_t y, uint8_t pen)
{
    l2_blit(x, y, 8, 8, gem[pen], L2_TRANSPARENT);
}

/* what each Layer 2 buffer shows: cell values (0xFF = unknown), HUD numbers */
static uint8_t shown[2][FIELD_H][FIELD_W];
static uint32_t shown_score[2];
static int shown_lines[2], shown_level[2];
static uint8_t layout;              /* buffers still to be laid out from scratch */
static uint8_t overlay;             /* buffers still to get the pause / game over box */
static uint8_t reframe;             /* buffers whose well walls the pause box covered */

#define BUF (l2_back & 1)

static void draw_cell(uint8_t x, uint8_t y, uint8_t c, uint8_t v)
{
    if (v >= 16) {                      /* line-clear flash: white / copper */
        l2_fill(x, y, CELL, CELL, v == 16 ? 9 : 13);
        return;
    }
    /* field background: shaded column stripes */
    l2_fill(x, y, CELL, CELL, (c & 1) ? 0 : 15);
    if (v) draw_gem(x, y, v);
}

static void draw_well_cells(void)
{
    const uint8_t *f = &gs.field[0][0];
    uint8_t *s = &shown[BUF][0][0];
    uint8_t r, c, v, x, y = FIELD_Y, flash;
    if (gs.clear_timer == 0 && memcmp(f, s, FIELD_H * FIELD_W) == 0) return;
    for (r = 0; r < FIELD_H; r++, y += CELL) {
        /* rows being cleared flash (cell values 16/17) */
        flash = 0;
        if (gs.clear_timer > 0)
            for (c = 0; c < gs.clear_count; c++)
                if (gs.clear_lines[c] == r) flash = (gs.clear_timer & 2) ? 16 : 17;
        for (c = 0, x = FIELD_X; c < FIELD_W; c++, x += CELL, s++, f++) {
            v = flash ? flash : *f;
            if (*s == v) continue;
            *s = v;
            draw_cell(x, y, c, v);
        }
    }
}

static void draw_well_frame(void)
{
    /* open-topped well: thick sides and floor, no lid */
    l2_fill(FIELD_X - 3, FIELD_Y, 2, FH + 3, 13);
    l2_fill(FIELD_X + FW + 1, FIELD_Y, 2, FH + 3, 13);
    l2_fill(FIELD_X - 3, FIELD_Y + FH + 1, FW + 6, 2, 13);
}

static void draw_hud_labels(void)
{
    draw_text(HX, 10, "STAKATTACK", 13, 1);
    draw_text(HX, 30, "POINTS", 10, 1);
    draw_text(HX, 62, "ROWS", 10, 1);
    draw_text(HX + 48, 62, "STAGE", 10, 1);
    draw_text(HX, NEXT_Y - 12, "COMING UP", 10, 1);
    l2_fill(HX - 4, NEXT_Y + 4 * CELL + 2, 4 * CELL + 46, 2, 13);
}

static void draw_hud_numbers(void)
{
    uint8_t b = BUF;
    if (shown_score[b] != gs.score) {
        shown_score[b] = gs.score;
        l2_fill(HX, 40, 110, 14, 0);
        draw_text(HX, 40, num_str(gs.score), 14, 2);
    }
    if (shown_lines[b] != gs.lines) {
        shown_lines[b] = gs.lines;
        l2_fill(HX, 72, 40, 7, 0);
        draw_text(HX, 72, num_str(gs.lines), 9, 1);
    }
    if (shown_level[b] != gs.level) {
        shown_level[b] = gs.level;
        l2_fill(HX + 48, 72, 40, 7, 0);
        draw_text(HX + 48, 72, num_str(gs.level), 9, 1);
    }
}

static void forget_buffer(void)
{
    uint8_t b = BUF;
    memset(shown[b], 0xFF, sizeof shown[b]);
    shown_score[b] = 0xFFFFFFFFUL;
    shown_lines[b] = shown_level[b] = -1;
}

static void draw_overlay_box(uint8_t top, uint8_t bottom)
{
    /* across the walls too: GAME OVER at scale 2 is wider than the well */
    l2_fill(FIELD_X - 3, FIELD_Y + top, FW + 6, bottom - top, 0);
    /* the cells under it are no longer what `shown` says */
    memset(shown[BUF][top / CELL], 0xFE, (bottom / CELL - top / CELL + 1) * FIELD_W);
}

static void draw_paused(void)
{
    draw_overlay_box(75, 105);
    draw_center(FIELD_X + FW / 2, FIELD_Y + 83, "PAUSED", 9, 2);
}

static void draw_gameover(void)
{
    draw_overlay_box(52, 128);
    draw_center(FIELD_X + FW / 2, FIELD_Y + 60, "GAME OVER", 1, 2);
    draw_center(FIELD_X + FW / 2, FIELD_Y + 84, num_str(gs.score), 9, 2);
    draw_center(FIELD_X + FW / 2, FIELD_Y + 108, "PRESS FIRE", 9, 1);
}

static void draw_hiscores(uint8_t y)
{
    uint8_t i;
    draw_center(128, y, "BEST STACKERS", 10, 1);
    for (i = 0; i < HS_ENTRIES; i++) {
        uint8_t yy = y + 14 + i * 10;
        char place[2] = {'1' + i, 0};
        draw_text(76, yy, place, 13, 1);
        draw_text(92, yy, hs_table[i].name, 9, 1);
        draw_text(184 - text_width(num_str(hs_table[i].score), 1), yy, num_str(hs_table[i].score), 14, 1);
    }
}

static void draw_title(void)
{
    uint8_t i;
    draw_center(128, 22, "STAKATTACK", 13, 3);
    /* a row of the seven gems under the title */
    for (i = 0; i < NUM_PIECES; i++) draw_gem(128 - (NUM_PIECES * 12) / 2 + i * 12 + 2, 54, i + 1);
    draw_hiscores(76);                       /* port: high-score table */
    draw_center(128, 152, "PRESS FIRE TO START", 9, 1);
    draw_center(128, 172, "O P MOVE  Q ROTATE  A DROP", 10, 1);
    draw_center(128, 181, "SPACE SLAM  ENTER PAUSE", 10, 1);
}

/* sprites: 0-3 landing marks, 4-7 the falling piece, 8-11 the next piece */
static void show_piece(uint8_t first, uint8_t type, uint8_t rot, int x0, int y0, uint8_t pat)
{
    const PieceShape *sh = piece_get_shape(type, rot);
    uint8_t r, c, n = first;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            if (sh->cells[r][c]) spr_show(n++, x0 + c * CELL + 32, y0 + r * CELL + 32, pat, 0);
}

static void hide_sprites(uint8_t from, uint8_t to)
{
    for (; from < to; from++) spr_hide(from);
}

/* the sprites only change with the piece, the next piece or the state */
static int spr_key[7];

static void draw_sprites(void)
{
    int key[7];
    key[0] = gs.current.type;
    key[1] = gs.current.rotation;
    key[2] = gs.current.x;
    key[3] = gs.current.y;
    key[4] = gs.next_type;
    key[5] = gs.state;
    key[6] = gs.clear_timer > 0;
    if (memcmp(key, spr_key, sizeof key) == 0) return;
    memcpy(spr_key, key, sizeof key);
    if (gs.state == STATE_PLAYING && gs.clear_timer == 0) {
        int gy = game_ghost_y(&gs);
        int px = FIELD_X + gs.current.x * CELL;
        if (gy != gs.current.y)
            show_piece(0, gs.current.type, gs.current.rotation, px, FIELD_Y + gy * CELL, 7);
        else
            hide_sprites(0, 4);
        show_piece(4, gs.current.type, gs.current.rotation, px, FIELD_Y + gs.current.y * CELL, gs.current.type);
    }
    else
        hide_sprites(0, 8);
    if (gs.state == STATE_PLAYING || gs.state == STATE_PAUSED)
        show_piece(8, gs.next_type, 0, NEXT_X, NEXT_Y, gs.next_type);
    else
        hide_sprites(8, 12);
}

/* the two screens that don't change, drawn once into banks of their own;
   a new screen is a DMA copy of one of them into the back buffer */
#define TITLE_BANK 18                   /* banks 18-20 */
#define GAME_BANK  21                   /* banks 21-23: the well (empty), the labels */

static void draw_into(uint8_t bank, void (*draw)(void))
{
    uint8_t back = l2_back;
    l2_back = bank;
    l2_clear(0);
    draw();
    l2_back = back;
}

static void draw_game_screen(void)
{
    uint8_t r, c, x, y = FIELD_Y;
    draw_well_frame();
    draw_hud_labels();
    for (r = 0; r < FIELD_H; r++, y += CELL)
        for (c = 0, x = FIELD_X; c < FIELD_W; c++, x += CELL) draw_cell(x, y, c, 0);
}

static void render(void)
{
    uint8_t lay = layout != 0;
    if (lay) {
        layout--;
        l2_copy_to_back(gs.state == STATE_TITLE ? TITLE_BANK : GAME_BANK);
        forget_buffer();
        memset(shown[BUF], 0, sizeof shown[0]);         /* the copy has the empty well */
    }
    switch (gs.state) {
    case STATE_TITLE:
        break;
    case STATE_PLAYING:
    case STATE_PAUSED:
    case STATE_GAMEOVER:
        if (reframe && gs.state == STATE_PLAYING) {
            reframe--;
            draw_well_frame();
        }
        if (gs.state == STATE_PLAYING || overlay || lay) draw_well_cells();
        draw_hud_numbers();
        if (overlay) {
            overlay--;
            if (gs.state == STATE_PAUSED) draw_paused();
            if (gs.state == STATE_GAMEOVER) draw_gameover();
        }
        break;
    }
}

/* ------------------------------------------------------------ sound */

/* the MOD (src/music.c, sdk/tools/mod2ay.py; see README): lead + bass on
   AY 0 A/B, drums + stab/bell on AY 1 A/B; AY 0 C is left for the effects */
extern const mod_song stak_song;
#define music_start() modplay_start(&stak_song)
#define music_tick()  modplay_tick()

static void sound_events(void)
{
    ay_chip(0);
    if (gs.just_cleared) ay_sfx(2, 0, 15, 1, 0);
    else if (gs.just_dropped) ay_sfx(2, 700, 15, 2, 60);
    else if (gs.just_locked) ay_sfx(2, 450, 12, 3, 0);
}

/* ------------------------------------------------------------ high scores */

static void enter_name(int8_t place)
{
    hs_editor ed;
    uint8_t i, redraw = 2, full = 2, r = 0;
    zx_log("stak: hiscore rank=%d\n", place);
    hide_sprites(0, 12);
    hs_edit_begin(&ed);
    while (!(r & HS_EDIT_DONE)) {
        if (r & HS_EDIT_CHANGED) redraw = 2;
        if (r & HS_EDIT_TAKEN) ay_sfx(2, 120, 12, 3, 0);
        if (redraw) {                   /* into each buffer once after a change */
            redraw--;
            if (full) {
                full--;
                l2_clear(0);
                draw_center(128, 30, "NEW HIGH SCORE", 13, 2);
                draw_center(128, 60, num_str(gs.score), 14, 2);
                draw_center(128, 90, "TYPE YOUR INITIALS", 9, 1);
                draw_center(128, 100, "OR JOYSTICK AND FIRE", 10, 1);
            }
            for (i = 0; i < 3; i++) {
                char ch[2] = {ed.name[i], 0};
                l2_fill(104 + i * 18, 124, 14, 18, i == ed.pos ? 15 : 0);
                draw_text(106 + i * 18, 126, ch, i < ed.pos ? 9 : 14, 2);
            }
        }
        frame_wait();
        l2_flip();
        music_tick();
        sound_tick();
        input_read();
        r = hs_edit_step(&ed);
    }
    hs_insert(place, ed.name, gs.score);
    zx_log("stak: name=%s\n", ed.name);
}

/* ------------------------------------------------------------ main */

/* input as the original maps it: move and soft drop while held; rotate,
   hard drop, start and pause on the press */
static int game_input(void)
{
    uint8_t held = input_read(), edge = input_pressed();
    int in = 0;
    held |= edge & (IN_LEFT | IN_RIGHT | IN_DOWN);         /* a quick tap moves once */
    /* port: a new press restarts the auto-shift, as the release in between
       would have if a slow frame hadn't hidden it */
    if ((edge & IN_LEFT) && gs.das_dir == -1) gs.das_dir = 0;
    if ((edge & IN_RIGHT) && gs.das_dir == 1) gs.das_dir = 0;
    if (held & IN_LEFT) in |= GINPUT_LEFT;
    if (held & IN_RIGHT) in |= GINPUT_RIGHT;
    if (held & IN_DOWN) in |= GINPUT_DOWN;
    if (edge & (IN_UP | IN_FIRE2)) in |= GINPUT_ROTATE;     /* port: Z rotates too */
    if (edge & IN_FIRE) in |= gs.state == STATE_PLAYING ? GINPUT_DROP : GINPUT_START;
    if (edge & IN_START) in |= GINPUT_START;
    return in;
}

int main(void)
{
    int prev_state = -1, prev_lines = 0;
    uint8_t ticks, t;

    next_init();
    frame_init();
    input_init();
    input_latch(1);                     /* a tap during a slow (layout) frame still counts */
    frame_hook = input_sample;
    ay_init();
    l2_init();
    make_graphics();
    hs_init(1000);
    draw_into(TITLE_BANK, draw_title);
    draw_into(GAME_BANK, draw_game_screen);
    music_start();
    game_init(&gs);
    prev_state = gs.state;
    layout = 2;
    zx_log("stak: ready ay=%u kempston=%u\n", ay_present, input_kempston);

    ticks = 1;
    for (;;) {
        int in0 = game_input();         /* once a frame: presses go to the first tick */
        for (t = 0; t < ticks; t++) {
            int in = t == 0 ? in0 : (in0 & (GINPUT_LEFT | GINPUT_RIGHT | GINPUT_DOWN));
            uint8_t was = gs.state;
            /* port: a high score is entered on the way from game over to the title */
            if (was == STATE_GAMEOVER && (in & GINPUT_START) && hs_rank(gs.score) >= 0) {
                enter_name(hs_rank(gs.score));
                draw_into(TITLE_BANK, draw_title);         /* the new table */
                gs.state = STATE_TITLE;
            }
            else
                game_update(&gs, in);
            music_tick();
            if (was == STATE_PLAYING) sound_events();   /* the event flags stay set after the game */
            sound_tick();
            if (gs.state != prev_state) {
                if (gs.state == STATE_TITLE) zx_log("stak: title\n");
                else if (gs.state == STATE_GAMEOVER)
                    zx_log("stak: gameover score=%lu lines=%d level=%d overruns=%u\n",
                           gs.score, gs.lines, gs.level, frame_overruns);
                zx_log("stak: state=%d\n", gs.state);
                /* a new screen for title <-> game; pause and game over put a
                   box over the well (whose cells repaint themselves after) */
                if (gs.state == STATE_TITLE || prev_state == STATE_TITLE) layout = 2;
                else if (gs.state == STATE_PLAYING) reframe = 2;
                else overlay = 2;
                prev_state = gs.state;
            }
            if (was == STATE_PLAYING && gs.just_locked) zx_log("stak: lock score=%lu\n", gs.score);
            if (gs.lines != prev_lines) {
                zx_log("stak: lines=%d level=%d score=%lu\n", gs.lines, gs.level, gs.score);
                prev_lines = gs.lines;
            }
        }
        render();                       /* into the back buffer */
        ticks = frame_wait();
        l2_flip();
        draw_sprites();
    }
}
