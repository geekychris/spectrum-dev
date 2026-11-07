/*
 * Sky Knights for the ZX Spectrum 48K - port of amiga_mcp/examples/sky_knights.
 *
 * game.c is the original logic in the original's 320 x 256 world (port
 * changes marked there); main.c follows the original main loop and draws
 * that world on the Spectrum's 256 x 192: x * 4/5, y * 3/4 through tables.
 * Knights and pods are pre-shifted XOR sprites that can hang off either edge
 * (sdk sprclip.h); each takes its colour in the cells of open sky and
 * ledges it covers (cells.asm). The rules run at a fixed 50 Hz with the
 * input sampled every frame by the interrupt, the screen as often as it can.
 * Beeper effects on a 48K; on a 128K the AY plays the original's melody too.
 *
 * Controls: one player - Q/A/O/P, Sinclair 1 or 2, Kempston: left/right,
 * fire or up flaps. Two players - player 1 keys (O/P, Q or SPACE flaps) or
 * Sinclair 2, player 2 Kempston or Sinclair 1 (6/7, 9 or 0 flaps).
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include <intrinsic.h>
#include "game.h"
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"
#include "sprclip.h"
#include "input2.h"
#include "mem.h"

static GameState gs;

/* ------------------------------------------------------------ world -> screen */

#define WX_MIN   (-PLAYER_W)              /* wrap_x keeps x in -20..320 */
#define sx_tab ((int16_t *)MEM_SX_TAB)       /* SCREEN_W - WX_MIN + 1 entries */
#define sy_tab ((uint8_t *)MEM_SY_TAB)       /* SCREEN_H + 1 */

static void map_init(void)
{
    int16_t i;
    for (i = 0; i <= SCREEN_W - WX_MIN; i++)
        sx_tab[i] = (int16_t)(((i + WX_MIN) * 4 + 80) / 5) - 16;     /* floor(x * 0.8) */
    for (i = 0; i <= SCREEN_H; i++)
        sy_tab[i] = i ? (uint8_t)((i * 3) / 4 - 1) : 0;               /* ground 236 -> 176 */
}

static int16_t scr_x(WORD wx)
{
    if (wx < WX_MIN) wx = WX_MIN;
    if (wx > SCREEN_W) wx = SCREEN_W;
    return sx_tab[wx - WX_MIN];
}

#define scr_y(wy) (sy_tab[(wy) < 0 ? 0 : (wy) > SCREEN_H ? SCREEN_H : (wy)])

/* ------------------------------------------------------------ art (1-bit, '#' = ink) */

/* port: drawn for 1-bit 16 x 16 (the original's 20 x 22 rectangles at 4/5 x
   3/4). Facing right: the lance and the mount's head point the way the
   knight flies (the original draws them on the left for facing 0 = right) */
#define RIDER_H 16
static const char *const art_top[8] = {          /* rider, lance, mount's head */
    ".....##.........", "....####........", "....#.##........", "....############",
    "...#####........", "...#####....###.", "....###....#####", "....###....##...",
};
static const char *const art_wing_down[5] = {     /* rows 8..12, wings folded */
    "..##########....", ".############...", "##############..", ".###########....",
    "..#..####.......",
};
static const char *const art_wing_up[5] = {
    "#.##########....", "##############..", ".############...", "..##########....",
    "...#.####.......",
};
static const char *const art_wing_up_top[3] = {   /* rows 5..7 with the wings raised */
    "#..#####....###.", "##..###....#####", ".##.###....##...",
};
static const char *const art_legs[2][3] = {
    {"....#....#......", "....#.....#.....", "...##....##....."},
    {".....#..#.......", ".....#..#.......", ".....##.##......"},
};
#define EGG_H 6
static const char *const art_egg[EGG_H] = {
    "..##....", ".####...", "######..", "######..", ".####...", "..##....",
};
static const uint8_t life_tile[8] = {0x00, 0x0C, 0x1E, 0x0C, 0x7E, 0xFC, 0x48, 0x00};

/* 8 rider frames: facing * 4 + wings up * 2 + leg frame, pre-shifted (3 x 16 x 8) */
#define RIDER_PRE (8 * 3 * RIDER_H)
static uint8_t pre_rider[8][RIDER_PRE];    /* read by every blit: uncontended RAM */
#define pre_egg ((uint8_t *)MEM_PRE_EGG)                    /* 8 * 2 * EGG_H */
const uint8_t *rider_ptr[8];                                /* each frame (draw.asm) */

static void row_bits(const char *s, uint8_t w, uint8_t *o)
{
    uint8_t x, m = 0x80;
    *o = 0;
    if (w > 8) o[1] = 0;
    for (x = 0; x < w; x++) {
        if (s[x] == '#') *o |= m;
        m >>= 1;
        if (!m) { m = 0x80; o++; }
    }
}

static uint8_t rev8(uint8_t b)
{
    uint8_t r = 0, i;
    for (i = 0; i < 8; i++) { r = (r << 1) | (b & 1); b >>= 1; }
    return r;
}

static void art_init(void)
{
    static uint8_t bits[2 * RIDER_H], mirror[2 * RIDER_H];
    uint8_t f, y;
    for (f = 0; f < 4; f++) {
        uint8_t up = f >> 1, leg = f & 1;
        for (y = 0; y < RIDER_H; y++) {
            const char *s;
            if (y < 5) s = art_top[y];
            else if (y < 8) s = up ? art_wing_up_top[y - 5] : art_top[y];
            else if (y < 13) s = up ? art_wing_up[y - 8] : art_wing_down[y - 8];
            else s = art_legs[leg][y - 13];
            row_bits(s, 16, bits + 2 * y);
            mirror[2 * y] = rev8(bits[2 * y + 1]);
            mirror[2 * y + 1] = rev8(bits[2 * y]);
        }
        spr_preshift(bits, 2, RIDER_H, pre_rider[f]);
        spr_preshift(mirror, 2, RIDER_H, pre_rider[4 + f]);
        rider_ptr[f] = pre_rider[f];
        rider_ptr[4 + f] = pre_rider[4 + f];
    }
    for (y = 0; y < EGG_H; y++) row_bits(art_egg[y], 8, bits + y);
    spr_preshift(bits, 1, EGG_H, pre_egg);
}

/* ------------------------------------------------------------ colours */

#define A_SKY      (PAPER_BLACK | INK_RED)            /* open sky; the ledges are its ink */
#define A_GROUND   (PAPER_YELLOW | INK_RED)
#define A_HUD      (PAPER_BLACK | INK_YELLOW | BRIGHT)
#define A_P1       (INK_YELLOW | BRIGHT)
#define A_P2       (INK_CYAN | BRIGHT)
const uint8_t enemy_attr[3] = {           /* draw.asm reads it */
    INK_RED | BRIGHT,                 /* swooper (orange) */
    INK_WHITE,                        /* raider (grey) */
    INK_BLUE | BRIGHT,                /* wraith (dark blue) */
};
#define A_EGG      (INK_GREEN | BRIGHT)
#define A_EGG_HATCH (INK_MAGENTA | BRIGHT)

/* ------------------------------------------------------------ playfield */

/* a line of pattern pat from pixel x0 to x1 (inclusive), a byte at a time */
static void pattern_line(uint8_t x0, uint8_t x1, uint8_t y, uint8_t pat)
{
    uint8_t *d = zx_row[y] + (x0 >> 3);
    uint8_t c0 = x0 >> 3, c1 = x1 >> 3;
    uint8_t lm = 0xFF >> (x0 & 7), rm = (uint8_t)(0xFF << (7 - (x1 & 7)));
    if (c0 == c1) { *d |= pat & lm & rm; return; }
    *d++ |= pat & lm;
    for (c0++; c0 < c1; c0++) *d++ = pat;
    *d |= pat & rm;
}

static void draw_platform(const Platform *pl)
{
    uint8_t x0 = (uint8_t)scr_x(pl->x), y = scr_y(pl->y), i;
    int16_t x1 = scr_x(pl->x + pl->w) - 1;
    if (x1 > 255) x1 = 255;
    for (i = 0; i < 6; i++)                  /* 8 lines at 3/4: the surface, then shade */
        pattern_line(x0, (uint8_t)x1, y + i, i < 2 ? 0xFF : (i & 1) ? 0x55 : 0xAA);
}

static void draw_ground(void)
{
    uint8_t x;
    attr_fill(0, 22, 32, 1, A_GROUND);
    gfx_clear_rect(0, 176, 32, 8);
    for (x = 0; x < 32 * 8 - 2; x += 10)            /* the original's notches every 12 px */
        { gfx_plot(x, 176); gfx_plot(x + 1, 176); gfx_plot(x, 177); gfx_plot(x + 1, 177);
          gfx_plot(x, 178); gfx_plot(x + 1, 178); }
}

static void draw_playfield(void)
{
    uint8_t i;
    screen_clear(A_SKY, INK_BLACK);
    for (i = 1; i < gs.num_platforms; i++) draw_platform(&gs.platforms[i]);
    draw_ground();
    attr_fill(0, 23, 32, 1, A_HUD);
}

/* ------------------------------------------------------------ sprites */

/* the sprite pass is draw.asm; the C it replaces is kept under DRAW_IN_C */
#ifndef DRAW_IN_C
#define DRAW_IN_C 0
#endif

#define NUM_SLOTS (MAX_EGGS + MAX_ENEMIES + MAX_PLAYERS)  /* pods, enemies, players: last wins colour */
typedef struct { uint8_t col, row, w, h; } Cells;
typedef struct {
    int16_t x;
    uint8_t y, on, w, h, attr;
    const uint8_t *img;
    Cells t;                    /* cells painted with attr */
    uint8_t ch;                 /* moved to other cells / new colour since the last tint_pass */
} Slot;                         /* draw.asm knows this layout */

/* cells.asm */
extern uint8_t cells_plain, cl_w, cl_h, cl_attr;
extern uint16_t cl_off;
extern void cells_capture(void);
extern void cells_restore(void);
extern void cells_paint(void);

static void cells_at(const Cells *c)
{
    cl_off = ((uint16_t)c->row << 5) | c->col;
    cl_w = c->w;
    cl_h = c->h;
}
Slot slots[NUM_SLOTS];
Player *dr_players;
Enemy *dr_enemies;
Egg *dr_eggs;
uint8_t dr_nplayers;
#if !DRAW_IN_C
extern void draw_pass(void);
extern void tint_pass(void);
#else
static void tint_pass(void);
#endif

static void slots_reset(void)
{
    Slot *s = slots;
    uint8_t i;
    for (i = 0; i < NUM_SLOTS; i++, s++) {
        s->on = 0;
        s->t.h = 0;
        s->ch = 0;
        s->w = i < MAX_EGGS ? 1 : 2;
        s->h = i < MAX_EGGS ? EGG_H : RIDER_H;
    }
}

uint8_t tint_dirty;

static const uint8_t *rider_img(WORD facing, WORD up, WORD leg)
{
    return rider_ptr[(facing ? 4 : 0) + (up ? 2 : 0) + (leg ? 1 : 0)];
}

/* take every object off the screen (the wave intro) */
static void hide_all(void)
{
    Slot *s = slots;
    uint8_t i;
    for (i = 0; i < NUM_SLOTS; i++, s++) {
        if (!s->on) continue;
        sprc_xor(s->x, s->y, s->img, s->w, s->h);
        s->on = 0;
        s->ch = 1;
    }
    tint_dirty = 1;
    tint_pass();
}

static uint16_t draws;          /* screen updates in play (fps) */
static uint8_t fps_ticks, hud_check;

#if DRAW_IN_C
/* move slot s to (x, y, img) or hide it (img = 0) */
static void slot_set(Slot *s, int16_t x, uint8_t y, const uint8_t *img, uint8_t attr)
{
    uint8_t on = img != 0;
    if (on != s->on || (on && (x != s->x || y != s->y || img != s->img))) {
        if (s->on) sprc_xor(s->x, s->y, s->img, s->w, s->h);
        if (on) sprc_xor(x, y, img, s->w, s->h);
        if (on != s->on || (x >> 3) != (s->x >> 3) || (y >> 3) != (s->y >> 3)) tint_dirty = 1;
        s->x = x;
        s->y = y;
        s->img = img;
        s->on = on;
    }
    if (on && attr != s->attr) tint_dirty = 1;
    s->attr = attr;
}

/* restore every slot's cells, then colour every visible one */
static void tint_pass(void)
{
    Slot *s;
    uint8_t i;
    if (!tint_dirty) return;
    tint_dirty = 0;
    for (i = 0, s = slots; i < NUM_SLOTS; i++, s++)
        if (s->t.h) { cells_at(&s->t); cells_restore(); }
    for (i = 0, s = slots; i < NUM_SLOTS; i++, s++) {
        int16_t x1;
        uint8_t c0, r1;
        s->t.h = 0;
        if (!s->on || s->x > 255 || s->y > 191) continue;   /* wholly off screen */
        x1 = s->x + s->w * 8 - 1;
        if (x1 < 0) continue;
        c0 = s->x < 0 ? 0 : (uint8_t)(s->x >> 3);
        if (x1 > 255) x1 = 255;
        r1 = (s->y + s->h - 1) >> 3;
        if (r1 > 23) r1 = 23;
        s->t.col = c0;
        s->t.row = s->y >> 3;
        s->t.w = (uint8_t)(x1 >> 3) - c0 + 1;
        s->t.h = r1 - s->t.row + 1;
        cells_at(&s->t);
        cl_attr = s->attr;
        cells_paint();
    }
}

static void draw_sprites(void)
{
    Slot *s = slots;
    uint8_t i;
    const Egg *eg = gs.eggs;
    const Enemy *e = gs.enemies;
    const Player *p = gs.players;
    for (i = 0; i < MAX_EGGS; i++, eg++, s++) {
        WORD x, y;
        uint8_t a = A_EGG;
        if (!eg->active) { slot_set(s, 0, 0, 0, 0); continue; }
        x = FIX_INT(eg->x);
        y = FIX_INT(eg->y);
        if (eg->timer < EGG_WOBBLE_TIME) {           /* the original's wobble before hatching */
            if (eg->timer & 4) a = A_EGG_HATCH;
            if (eg->timer < 60) x += (eg->timer & 2) ? 1 : -1;
        }
        slot_set(s, scr_x(x), scr_y(y), pre_egg, a);
    }
    for (i = 0; i < MAX_ENEMIES; i++, e++, s++) {
        if (!e->active) { slot_set(s, 0, 0, 0, 0); continue; }
        slot_set(s, scr_x(FIX_INT(e->x)), scr_y(FIX_INT(e->y)) - 1,
                 rider_img(e->facing, e->anim_frame, e->anim_frame), enemy_attr[e->type]);
    }
    for (i = 0; i < MAX_PLAYERS; i++, p++, s++) {
        if (i >= gs.num_players || !p->alive) { slot_set(s, 0, 0, 0, 0); continue; }
        slot_set(s, scr_x(FIX_INT(p->x)), scr_y(FIX_INT(p->y)) - 1,
                 rider_img(p->facing, p->flapping, p->anim_frame), i ? A_P2 : A_P1);
    }
    tint_pass();
}
#else
static void draw_sprites(void)
{
    draw_pass();
    tint_pass();
}
#endif

/* ------------------------------------------------------------ HUD (row 23; port: the original's is at the top) */

static LONG hud_score[2];
static WORD hud_lives[2], hud_wave;

static void hud_lives_draw(uint8_t col, WORD lives, uint8_t attr)
{
    uint8_t i;
    for (i = 0; i < 3; i++) {
        if (i < lives) tile_draw(col + i, 23, life_tile);
        else gfx_clear_rect(col + i, 184, 1, 8);
    }
    attr_fill(col, 23, 3, 1, PAPER_BLACK | attr);
}

static void hud(uint8_t force)
{
    uint8_t i;
    for (i = 0; i < gs.num_players; i++) {
        if (force || hud_score[i] != gs.score[i]) {
            hud_score[i] = gs.score[i];
            text_num(i ? 25 : 0, 23, (uint32_t)gs.score[i], 7);
        }
        if (force || hud_lives[i] != gs.players[i].lives) {
            hud_lives[i] = gs.players[i].lives;
            hud_lives_draw(i ? 21 : 8, hud_lives[i], i ? A_P2 : A_P1);
        }
    }
    if (force || hud_wave != gs.wave) {
        hud_wave = gs.wave;
        attr_fill(12, 23, 8, 1, PAPER_BLACK | INK_WHITE | BRIGHT);
        zx_print_at(12, 23, "WAVE");
        text_num(17, 23, gs.wave, 2);
    }
}

/* ------------------------------------------------------------ text helpers */

static char numbuf[12];

static const char *num_str(uint32_t v)
{
    char *p = numbuf + sizeof(numbuf) - 1;
    *p = 0;
    do { *--p = '0' + (char)(v % 10); v /= 10; } while (v);
    return p;
}

static void print_center2(uint8_t row, const char *a, const char *b, uint8_t attr)
{
    static char line[33];
    uint8_t n = strlen(a);
    strcpy(line, a);
    strncpy(line + n, b, 32 - n);
    line[32] = 0;
    attr_fill(0, row, 32, 1, attr);
    text_center(row, line);
}

static void big_center(uint8_t row, const char *s, uint8_t attr)
{
    uint8_t n = strlen(s) * 2;
    attr_fill((32 - n) / 2, row, n, 2, attr);
    text_big((32 - n) / 2, row, s);
}

/* ------------------------------------------------------------ screens */

static void draw_wave_intro(void)
{
    static char line[12];
    strcpy(line, "WAVE ");
    strcat(line, num_str(gs.wave));
    big_center(9, line, INK_YELLOW | BRIGHT);                 /* the original: WAVE n, GET READY! */
    attr_fill(11, 12, 10, 1, INK_WHITE | BRIGHT);
    text_center(12, "GET READY!");
    hud(1);
}

static void draw_gameover(void)
{
    screen_clear(INK_WHITE | BRIGHT, INK_BLACK);
    attr_fill(0, 6, 32, 2, INK_RED | BRIGHT);
    big_center(6, "GAME OVER", INK_RED | BRIGHT);
    print_center2(11, "P1 SCORE: ", num_str(gs.score[0]), INK_YELLOW | BRIGHT);
    if (gs.num_players > 1) print_center2(13, "P2 SCORE: ", num_str(gs.score[1]), INK_YELLOW | BRIGHT);
    print_center2(16, "WAVE REACHED: ", num_str(gs.wave), INK_WHITE | BRIGHT);
    attr_fill(0, 20, 32, 1, INK_WHITE);
    text_center(20, "RETURNING TO TITLE...");
}

static void draw_title_menu(void)
{
    attr_fill(0, 9, 32, 1, gs.title_selection == 0 ? (INK_YELLOW | BRIGHT) : INK_WHITE);
    attr_fill(0, 10, 32, 1, gs.title_selection == 1 ? (INK_YELLOW | BRIGHT) : INK_WHITE);
}

static void draw_title(void)
{
    screen_clear(INK_WHITE | BRIGHT, INK_BLACK);
    attr_fill(0, 1, 32, 2, INK_YELLOW | BRIGHT);
    big_center(1, "SKY KNIGHTS", INK_YELLOW | BRIGHT);
    attr_fill(0, 4, 32, 1, INK_WHITE | BRIGHT);
    text_center(4, "AN ARCADE CLASSIC");
    attr_fill(14, 5, 3, 3, A_P1);                  /* the decorative mount */
    text_center(9, "1 PLAYER  - PRESS 1");
    text_center(10, "2 PLAYERS - PRESS 2");
    draw_title_menu();
    attr_fill(0, 12, 32, 1, INK_CYAN | BRIGHT);
    text_center(12, "HIGH SCORES");
    attr_fill(0, 13, 32, 5, INK_WHITE | BRIGHT);
    hs_draw(10, 13);
    attr_fill(0, 19, 32, 4, INK_WHITE);
    text_center(19, "STRIKE FROM ABOVE, GRAB THE PODS");
    text_center(20, "P1: QAOP/SPACE/SINCLAIR 2");
    text_center(21, "P2: KEMPSTON/SINCLAIR 1");
    attr_fill(0, 23, 32, 1, INK_GREEN | BRIGHT);
    text_center(23, "FIRE STARTS  UP/DOWN SELECTS");
}

static const uint8_t *title_img;

static void title_anim(void)
{
    const uint8_t *img = rider_img(0, (gs.frame >> 3) & 1, (gs.frame >> 4) & 1);
    if (img == title_img) return;
    if (title_img) sprc_xor(120, 41, title_img, 2, RIDER_H);
    sprc_xor(120, 41, img, 2, RIDER_H);
    title_img = img;
}

/* ------------------------------------------------------------ sound */

/* the original's melody (channel 3 of Paula, always on) as AY periods */
#define N_G3 565
#define N_A3 504
#define N_C4 424
#define N_D4 377
#define N_E4 336
#define N_F4 317
#define N_G4 283
#define N_A4 252
#define N_C5 212
#define TEMPO 9
static const ay_note melody[] = {
    {N_C4, TEMPO}, {N_E4, TEMPO}, {N_G4, TEMPO}, {N_C5, TEMPO},
    {N_G4, TEMPO}, {N_E4, TEMPO}, {N_C4, TEMPO}, {N_G3, TEMPO},
    {N_A3, TEMPO}, {N_C4, TEMPO}, {N_E4, TEMPO}, {N_G4, TEMPO},
    {N_F4, TEMPO}, {N_E4, TEMPO}, {N_D4, TEMPO}, {N_C4, TEMPO},
    {N_C4, TEMPO}, {N_D4, TEMPO}, {N_E4, TEMPO}, {N_G4, TEMPO},
    {N_A4, TEMPO}, {N_G4, TEMPO}, {N_E4, TEMPO}, {N_C4, TEMPO},
    {N_G3, TEMPO}, {N_A3, TEMPO}, {N_C4, TEMPO}, {N_E4, TEMPO},
    {N_D4, TEMPO}, {N_C4, TEMPO}, {N_G3, TEMPO}, {N_C4, TEMPO},
    {0, 0},
};

/* effects asked for by the game; the AY is written only by the interrupt
   (a register write is a select then a value, which an interrupt could split) */
static volatile uint8_t snd_pending;
static uint8_t flap_gap;                 /* frames to the next wing-beat sound */

static void sound_isr(void)
{
    uint8_t ev = snd_pending;
    snd_pending = 0;
    if (ev & EV_FLAP) ay_sfx(0, 900, 9, 3, -60);
    if (ev & EV_EGG)  ay_sfx(0, 300, 13, 1, -12);   /* the original: an ascending tone */
    if (ev & EV_WAVE) ay_sfx(0, N_C4, 14, 1, 0);
    if (ev & EV_KILL) ay_sfx(1, 0, 15, 1, 0);       /* crunch: noise */
    if (ev & EV_DIE)  ay_sfx(1, 0, 15, 1, 0), ay_sfx(0, 500, 15, 1, 20);
    sound_tick();
}

/* port: Paula played the flap sample every frame the button was held; here
   it is a short wing beat every 6 frames */
static void sound_events(uint8_t ev)
{
    if (ev & EV_FLAP) {
        if (flap_gap) ev &= ~EV_FLAP;
        else flap_gap = 6;
    }
    if (!ev) return;
    if (ay_present) { snd_pending |= ev; return; }
    /* beeper: short and blocking, the most important one */
    if (ev & EV_DIE) { beep_noise(400); beep_sweep(60, 200, 20, 3); }
    else if (ev & EV_KILL) beep_noise(250);
    else if (ev & EV_EGG) beep_sweep(120, 40, 20, 3);
    else if (ev & EV_WAVE) { beep(70, 30); beep(52, 40); beep(42, 50); }
    else if (ev & EV_FLAP) beep(30, 3);
}

/* ------------------------------------------------------------ input: sampled every frame */

/* the interrupt samples both players' controls each frame into a ring, so
   each 50 Hz game tick gets the input of its own frame however slow the
   drawing is (the original read the input every frame) */
#define SRC_ALL (IN2_QAOP | IN2_SINCLAIR1 | IN2_SINCLAIR2 | IN2_KEMPSTON1 | IN2_SPACE | IN2_ENTER)
#define SRC_P1  (IN2_QAOP | IN2_SINCLAIR2 | IN2_SPACE | IN2_ENTER)
#define SRC_P2  (IN2_KEMPSTON1 | IN2_SINCLAIR1)
static uint16_t src_p1 = SRC_ALL, src_p2;
static volatile uint8_t src_changed;
#define RING 64                      /* frames of input kept (a power of two) */
#define ring_p1  ((uint8_t *)MEM_RING)
#define ring_p2  ((uint8_t *)MEM_RING + RING)
#define ring_num ((uint8_t *)MEM_RING + 2 * RING)
static uint16_t ticks_dropped;
volatile uint8_t isr_n;              /* frames sampled (test.py aligns input on it) */
static uint8_t rd_n;

extern uint8_t isr_scan(void);       /* cells.asm: reads the keys and stick into in2_rows/in2_kemp, 1 = changed */

static void isr_work(void)
{
    static uint8_t b1, b2;
    uint8_t i = (uint8_t)(isr_n + 1) & (RING - 1);
    /* decoding is slow in C: only when a key or the stick changed */
    if (isr_scan() || src_changed) {
        src_changed = 0;
        b1 = in2_bits(src_p1);
        b2 = src_p2 ? in2_bits(src_p2) : 0;
    }
    ring_p1[i] = b1;
    ring_p2[i] = b2;
    ring_num[i] = (in2_key(3, 0x01) ? 1 : 0) | (in2_key(3, 0x02) ? 2 : 0);   /* keys 1, 2 */
    isr_n++;
    sound_isr();
}

static UWORD to_inp(uint8_t b)
{
    UWORD v = 0;
    if (b & IN_LEFT) v |= INP_LEFT;
    if (b & IN_RIGHT) v |= INP_RIGHT;
    if (b & (IN_FIRE | IN_UP)) v |= INP_FLAP;     /* the original: up or fire flaps */
    return v;
}

static uint8_t prev_p1, prev_num;
static WORD startup_delay = 30;      /* the original: ignore input just after starting */

/* test hooks (test.py): from the boot on, what every tick took from player
   1's controls is recorded at MEM_TEST_TAPE (until it is full); with
   tick_tape set, ticks play back from there instead of the controls
   (tick_left bytes): tick-exact, whatever the frame timing */
uint8_t *tick_rec = (uint8_t *)MEM_TEST_TAPE, *tick_tape;
uint16_t tick_left = MEM_TEST_TAPE_LEN;

static void tick_input(InputState *in, uint8_t idx)
{
    uint8_t b1 = ring_p1[idx], b2 = ring_p2[idx], num = ring_num[idx], pressed, num_pressed;
    if (tick_tape) {
        b1 = tick_left ? (tick_left--, *tick_tape++) : 0;
        b2 = num = 0;
    }
    else if (tick_rec && tick_left) {
        *tick_rec++ = b1;
        tick_left--;
    }
    pressed = b1 & ~prev_p1;
    num_pressed = num & ~prev_num;
    prev_p1 = b1;
    prev_num = num;
    in->p1 = to_inp(b1);
    in->p2 = to_inp(b2);
    in->sys = 0;
    if (gs.state == STATE_TITLE) {
        if (num_pressed & 1) in->sys |= INP_START1;
        else if (num_pressed & 2) in->sys |= INP_START2;
        else if (pressed & (IN_FIRE | IN_START))
            in->sys |= gs.title_selection ? INP_START2 : INP_START1;
        else if (pressed & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)) in->sys |= INP_SELECT;
    }
    if (startup_delay > 0) {
        startup_delay--;
        in->p1 = in->p2 = in->sys = 0;
    }
}

/* ------------------------------------------------------------ test probe + log */

/* test bots (test.py) read the game state `gs` by symbol: game.h's structs */

static const char *const state_names[5] = {"title", "playing", "intro", "dying", "gameover"};

static void log_tick(uint8_t ev)
{
    static WORD last_state = -1, last_wave = -1, last_lives[2] = {-1, -1};
    static LONG last_score[2] = {-1, -1};
    uint8_t i;
    if (gs.state != last_state) {
        zx_log("sky: state=%s wave=%d players=%d\n", state_names[gs.state], gs.wave, gs.num_players);
        last_state = gs.state;
    }
    if (gs.wave != last_wave) { zx_log("sky: wave=%d\n", gs.wave); last_wave = gs.wave; }
    for (i = 0; i < gs.num_players; i++) {
        if (gs.score[i] != last_score[i]) {
            zx_log("sky: score p=%u s=%ld\n", i + 1, gs.score[i]);
            last_score[i] = gs.score[i];
        }
        if (gs.players[i].lives != last_lives[i]) {
            zx_log("sky: lives p=%u n=%d\n", i + 1, gs.players[i].lives);
            last_lives[i] = gs.players[i].lives;
        }
    }
    if (ev & EV_KILL) zx_log("sky: joust won\n");
    if (ev & EV_EGG) zx_log("sky: pod caught\n");
    if (ev & EV_DIE) zx_log("sky: knight down\n");
}

/* ------------------------------------------------------------ main loop (the original's) */

/* draw.asm and phys.asm read the structs at fixed offsets */
#define OFS(t, f) ((uint16_t)&((t *)0)->f)
static void layout_check(void)
{
    /* mem.h's tables must not overlap */
    if (MEM_PRE_EGG + 8 * 2 * EGG_H > MEM_SX_TAB || MEM_SX_TAB + 2 * (SCREEN_W - WX_MIN + 1) > MEM_SY_TAB ||
        MEM_SY_TAB + SCREEN_H + 1 > MEM_CELLS_BG || MEM_CELLS_BG + 768 > MEM_RING ||
        MEM_RING + 3 * RING > MEM_END)
        zx_log("sky: layout mismatch (mem.h)\n");
    if (sizeof(Slot) != 14 || OFS(Slot, img) != 7 || OFS(Slot, t) != 9 ||
        sizeof(Egg) != 14 || OFS(Egg, timer) != 8 ||
        sizeof(Enemy) != 22 || OFS(Enemy, type) != 8 || OFS(Enemy, anim_frame) != 14 ||
        sizeof(Player) != 26 || OFS(Player, alive) != 10 || OFS(Player, facing) != 14 ||
        OFS(Player, flapping) != 24 || sizeof(Platform) != 6)
        zx_log("sky: layout mismatch\n");
}

static LONG final_score[2];
static uint8_t final_players, menu_drawn;

/* port: high scores (the original has none), each player in turn */
static void enter_scores(void)
{
    uint8_t i;
    for (i = 0; i < final_players; i++) {
        int8_t place = hs_rank((uint32_t)final_score[i]);
        if (place >= 0) hs_enter(place, (uint32_t)final_score[i], "sky");
    }
}

int main(void)
{
    gfx_init();
    map_init();
    art_init();
    ay_init();
    in2_init();
    hs_init(2000);                     /* table 10000 8000 6000 4000 2000 */
    rnd_seed(12345);                   /* the original's rng seed */
    game_init(&gs);
    dr_players = gs.players;
    dr_enemies = gs.enemies;
    dr_eggs = gs.eggs;
    slots_reset();
    layout_check();
    draw_title();
    frame_init();
    frame_hook = isr_work;
    if (ay_present) music_play(2, melody, 10);
    zx_log("sky: ready kempston=%u ay=%u\n", in2_has_kemp[0], ay_present);

    for (;;) {
        WORD prev_state = gs.state;
        uint8_t ticks, ev = 0, rd_end;

        frame_wait();
        intrinsic_di();
        ticks = isr_n - rd_n;
        intrinsic_ei();
        if (ticks > RING - 4) {                  /* far behind: let the oldest go */
            ticks_dropped += ticks - (RING - 4);
            rd_n += ticks - (RING - 4);
            ticks = RING - 4;
        }
        if (ticks > 6) ticks = 6;                /* the rest next time: draw now and then */

        /* draw what the last tick left */
        if (gs.state == STATE_PLAYING) { draw_sprites(); draws++; }
        else if (gs.state == STATE_TITLE) title_anim();
        if (hud_check && (gs.state == STATE_PLAYING || gs.state == STATE_WAVE_INTRO)) hud(0);
        hud_check = 0;

        rd_end = rd_n + ticks;
        while (rd_n != rd_end) {
            InputState in;
            tick_input(&in, ++rd_n & (RING - 1));
            game_update(&gs, &in);
            ev |= (uint8_t)gs.ev_flags;
            if (gs.ev_flags || gs.state != prev_state) log_tick((uint8_t)gs.ev_flags);
            gs.ev_flags = 0;
            if (gs.state == STATE_PLAYING && ++fps_ticks == 250) {   /* 5 s of play */
                zx_log("sky: fps=%u enemies=%d dropped=%u\n", draws / 5, gs.enemies_alive, ticks_dropped);
                fps_ticks = 0;
                draws = 0;
            }
            if (gs.state != prev_state || (gs.state == STATE_TITLE && in.sys & INP_SELECT)) break;
        }
        if (flap_gap) flap_gap--;
        if (ev || gs.state != prev_state) hud_check = 1;      /* scores, lives, wave */
        sound_events(ev);

        if (gs.state != prev_state) {
            switch (gs.state) {
            case STATE_WAVE_INTRO:
                if (prev_state == STATE_TITLE) {
                    intrinsic_di();
                    src_p1 = gs.num_players > 1 ? SRC_P1 : SRC_ALL;
                    src_p2 = gs.num_players > 1 ? SRC_P2 : 0;
                    src_changed = 1;
                    intrinsic_ei();
                    title_img = 0;
                    dr_nplayers = (uint8_t)gs.num_players;
                    slots_reset();
                    draw_playfield();
                    cells_plain = A_SKY;
                    cells_capture();
                }
                else hide_all();
                draw_wave_intro();
                break;
            case STATE_PLAYING:
                slots_reset();
                draw_playfield();
                cells_plain = A_SKY;
                cells_capture();
                hud(1);
                draws = 0;
                fps_ticks = 0;
                break;
            case STATE_GAMEOVER:
                zx_log("sky: gameover s1=%ld s2=%ld wave=%d\n", gs.score[0], gs.score[1], gs.wave);
                final_score[0] = gs.score[0];
                final_score[1] = gs.score[1];
                final_players = (uint8_t)gs.num_players;
                draw_gameover();
                break;
            case STATE_TITLE: {
                enter_scores();
                intrinsic_di();
                src_p1 = SRC_ALL;
                src_p2 = 0;
                src_changed = 1;
                intrinsic_ei();
                draw_title();
                menu_drawn = 0;
                zx_log("sky: title\n");
                intrinsic_di();
                rd_n = isr_n;                  /* drop the frames the entry took */
                intrinsic_ei();
                prev_p1 = 0xFF;                /* a FIRE still held from the entry is no press */
                break;
            }
            }
        }
        else if (gs.state == STATE_TITLE && gs.title_selection != menu_drawn) {
            menu_drawn = (uint8_t)gs.title_selection;
            draw_title_menu();
        }
    }
}
