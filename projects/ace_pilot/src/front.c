/*
 * front.c - Ace Pilot's front end on the Next (resident): the original's
 * render_title, render_hud, render_gameover and sound.c.
 *
 * port: the HUD's text is on the tilemap over Layer 2 (40 x 32 cells over
 * the 320 x 256 display; the 256 x 192 view starts at cell 4, 4), so the
 * view never redraws it and only what changed is written. The title's big
 * letters and GAME OVER are drawn on Layer 2 with the SDK's 5 x 7 font, as
 * the original draws its own. The original's sounds are Paula samples made
 * by sound.c (gunfire, explosion, hit, death) and a ProTracker tune; here
 * they are AY effects of the same kind and there is no tune.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "game.h"
#include "front.h"
#include "modplay.h"

#define RGB9(r, g, b) (((r) << 6) | ((g) << 3) | (b))

/* palette offsets = text colours: the original's pens */
enum { C_WHITE, C_YELLOW, C_GREEN, C_RED, C_CYAN, C_GREY, C_ORANGE, C_DIM };
static const uint16_t fg[8] = {
    RGB9(7, 7, 7), RGB9(7, 7, 0), RGB9(0, 7, 0), RGB9(7, 0, 0),
    RGB9(0, 7, 7), RGB9(5, 5, 5), RGB9(7, 4, 0), RGB9(3, 3, 3),
};

#define T_FONT 32                       /* character c is tile c (shadowed) */

static uint8_t tile[32];

static void put_px(uint8_t x, uint8_t y, uint8_t pen)
{
    uint8_t *b = &tile[y * 4 + (x >> 1)];
    if (x & 1) *b = (*b & 0xF0) | pen;
    else *b = (*b & 0x0F) | (pen << 4);
}

void front_init(void)
{
    uint8_t c, x, y, k;
    static uint8_t g[8];
    const uint8_t *font = (const uint8_t *)0x3D00;      /* the ROM's */
    tm_init();
    memset(tile, 0, 32);
    tm_def(0, tile, 1);
    for (c = 0; c < 96; c++) {
        memcpy(g, font + c * 8, 8);
        memset(tile, 0, 32);
        for (y = 0; y < 7; y++)
            for (x = 0; x < 7; x++)
                if (g[y] & (0x80 >> x)) put_px(x + 1, y + 1, 2);
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                if (g[y] & (0x80 >> x)) put_px(x, y, 1);
        tm_def(T_FONT + c, tile, 1);
    }
    for (k = 0; k < 16; k++) {
        tm_pal(k, 1, fg[k & 7]);
        tm_pal(k, 2, RGB9(0, 0, 0));
    }
    ZXN_WRITE_REG(0x15, 0x10);          /* tilemap (ULA layer) over Layer 2, no sprites */
}

static void text(uint8_t col, uint8_t row, const char *s, uint8_t c) { tm_print(col, row, s, T_FONT, c); }
static uint8_t slen(const char *s) { uint8_t n = 0; while (s[n]) n++; return n; }
static void ctext(uint8_t row, const char *s, uint8_t c) { text((uint8_t)(20 - slen(s) / 2), row, s, c); }

/* v as `digits` digits (zero-padded) or, with pad = ' ', right-aligned */
static char *num(char *p, uint32_t v, uint8_t digits, char pad)
{
    char *e = p + digits;
    *e = 0;
    do { *--e = (char)('0' + v % 10); v /= 10; } while (v && e > p);
    while (e > p) *--e = pad;
    return p;
}

/* label + v left-aligned in a field of w characters */
static char *lnum(char *p, const char *label, uint32_t v, uint8_t w)
{
    static char d[11];
    uint8_t n = 0, k = 0;
    while (label[n]) { p[n] = label[n]; n++; }
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) p[n++] = d[--k];
    while (n < w) p[n++] = ' ';
    p[n] = 0;
    return p;
}

/* ---- title */

void front_title_players(uint8_t players)
{
    text(5, 24, "1 ONE PLAYER", players == 1 ? C_CYAN : C_DIM);
    text(21, 24, "2 TWO PLAYERS", players == 2 ? C_CYAN : C_DIM);
}

static void title_picture(void)
{
    uint32_t seed = 4242;
    uint8_t i;
    l2_clear(0);
    for (i = 0; i < 40; i++) {          /* the original's star field (x 0.8) */
        uint16_t sx, sy;
        seed = seed * 1103515245UL + 12345UL;
        sx = (uint16_t)((seed >> 16) % 320) * 4 / 5;
        sy = (uint16_t)((seed >> 3) % 256) * 3 / 4;
        l2_plot((uint8_t)sx, (uint8_t)sy, 0x08);
    }
    l2_text5_center(128, 6, "ACE", 0xE0, 5);
    l2_text5_center(128, 44, "PILOT", 0xFF, 5);
    l2_hline(24, 83, 208, 0xE0);
    l2_hline(24, 85, 208, 0xB0);
}

void front_title(uint8_t players)
{
    title_picture();
    l2_flip();
    title_picture();                    /* both buffers: the title isn't redrawn */
    tm_clear();
    ctext(15, "ACE OF THE SKIES", C_YELLOW);
    text(5, 17, "Q A O P, JOYSTICK   FLY", C_GREY);
    text(5, 18, "SPACE, FIRE         SHOOT", C_GREY);
    text(5, 19, "W / S               THROTTLE", C_GREY);
    text(5, 20, "C   CLASSIC OR COLOUR", C_GREY);
    text(5, 21, "B   AUTOPILOT       M   MUSIC", C_GREY);
    text(5, 22, "BREAK               TITLE", C_GREY);
    front_title_players(players);
    ctext(25, "PLAYER 2: 6-0 OR IJKL ENTER", C_DIM);
    ctext(27, "PRESS FIRE TO START", C_YELLOW);
}

/* ---- HUD */

typedef struct { int32_t score; int16_t lives, alt, spd, wave, ready; } shown_t;
static shown_t shown[2];
static uint8_t shown_auto;

void front_play(void)
{
    tm_clear();
    memset(shown, 0xFF, sizeof shown);  /* everything changed */
    shown_auto = 0xFF;
}

static void hud_player(const GameWorld *w, uint8_t pi, uint8_t row, uint8_t split)
{
    const PlayerState *p = &w->players[pi];
    shown_t *s = &shown[pi];
    static char b[12];
    uint8_t col = C_YELLOW, i;
    int16_t alt = (int16_t)p->cam.pos.y, ready;
    if (g_tune.display_mode == DISPLAY_CLASSIC) col = C_GREEN;
    if (p->score != s->score) {
        s->score = p->score;
        text(5, row, num(b, (uint32_t)p->score, 7, '0'), col);
    }
    if (p->lives != s->lives) {
        s->lives = p->lives;
        for (i = 0; i < 5; i++) tm_put(5 + i, row + 1, i < p->lives ? '>' : ' ', col);
    }
    if (w->wave != s->wave) {
        s->wave = w->wave;
        text(30, row, lnum(b, "W", (uint32_t)w->wave, 4), col);
    }
    if (alt != s->alt) {
        s->alt = alt;
        text(27, row + 1, lnum(b, "ALT:", (uint32_t)alt, 8), col);
    }
    if (p->speed != s->spd) {
        s->spd = p->speed;
        text(27, row + 2, lnum(b, "SPD:", (uint32_t)p->speed, 8), col);
    }
    if (split && s->ready == -1) text(19, row, pi ? "P2" : "P1", col);
    ready = !p->alive && p->lives > 0 && w->state != STATE_GAMEOVER;
    if (ready != s->ready) {
        s->ready = ready;
        text(15, (uint8_t)(row + (split ? 5 : 11)), ready ? "GET READY" : "         ", C_RED);
    }
}

void front_hud(const GameWorld *w, uint8_t autopilot)
{
    if (g_tune.num_players >= 2) {
        hud_player(w, 0, 4, 1);
        hud_player(w, 1, 17, 1);
    } else hud_player(w, 0, 5, 0);
    if (autopilot != shown_auto) {
        shown_auto = autopilot;
        text(18, 27, autopilot ? "AUTO" : "    ", C_CYAN);
    }
}

/* ---- game over: on the view, every frame it shows */

void front_overlay(const GameWorld *w)
{
    static char b[20];
    if (w->state != STATE_GAMEOVER) return;
    l2_text5_center(128, 40, "GAME OVER", 0xE0, 4);
    l2_text5_center(128, 118, lnum(b, "P1 SCORE  ", (uint32_t)w->players[0].score, 0), 0xFF, 1);
    if (g_tune.num_players >= 2)
        l2_text5_center(128, 130, lnum(b, "P2 SCORE  ", (uint32_t)w->players[1].score, 0), 0xFF, 1);
    if (w->state_timer <= 0) l2_text5_center(128, 150, "PRESS FIRE", 0xFD, 1);
}

/* ---- sound: the original's four effects (sound.c) as AY effects on chip
   0, its MOD (the Blue Danube, src/music_song.c) on chips 1 and 2. All of
   it runs in the frame interrupt (frame_hook), so the music keeps its tempo
   however slow the picture is; game.c's calls only queue an effect (a write
   to the AY from the program could be cut in two by the interrupt's) */

extern const mod_song ace_song;
static volatile uint8_t sfx_req;
volatile uint8_t music_want = 1;
uint8_t music_on;                       /* the interrupt's: what plays (test.py reads it) */

static void queue(uint8_t bit)
{
    uint8_t r = sfx_req;                /* port note: through a local (zsdcc and |= on volatiles) */
    r |= bit;
    sfx_req = r;
}

void sfx_gunfire(void) { queue(1); }
void sfx_hit(void) { queue(2); }
void sfx_explosion(void) { queue(4); }
void sfx_die(void) { queue(8); }

static void isr_sound(void)
{
    uint8_t r = sfx_req;
    sfx_req = 0;
    if (r & 1) ay_sfx(0, 0, 12, 3, 0);                 /* gunfire: a short noise crack */
    if (r & 2) ay_sfx(1, 300, 13, 2, 12);              /* hit: a falling clank */
    if (r & 4) ay_sfx(2, 0, 15, 1, 0);                 /* explosion: a long noise burst */
    if (r & 8) { ay_sfx(1, 120, 15, 1, 6); ay_sfx(2, 0, 15, 1, 0); }   /* death: a falling wail */
    sound_tick();
    if (music_want != music_on) {
        music_on = music_want;
        if (music_on) modplay_start(&ace_song);
        else modplay_stop();
        ay_chip(0);
    }
    if (music_on) modplay_tick();                       /* leaves chip 0 selected */
}

void front_sound_init(void)
{
    ay_init();
    frame_hook = isr_sound;
}
