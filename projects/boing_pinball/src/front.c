/*
 * front.c - Boing Pinball's Spectrum Next front end (the original's
 * game.c and render.c, for the Next). It lives in 8K page 46 at 0x6000
 * (Makefile); main.c maps it in.
 *
 *   Layer 2 (320 x 256, banks 30-34) holds the whole table, drawn once on
 *   the host (tools/gen_gfx.py) like the original draws its picture. What
 *   moves is hardware sprites: the Boing balls (8 frames of spin), the
 *   flippers (8 angles, the right one mirrored) and the plunger. Lamps,
 *   drop targets, bumper and sling flashes and the spinners each own a
 *   palette entry, so they light up by changing it. The dot-matrix display
 *   is drawn into Layer 2 when its text changes.
 *   Sound: the original's effects on the AY; the attract mode is the
 *   original's demo player (pb_bot).
 */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include <input.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "pbz.h"
#include "gfx.h"
#include "font.h"

#define L2_BANK  30
#define SPR_PAGE 70                     /* bank 35 */
#define DATA_PAGE 44                    /* the table's data, MMU slot 2 */

extern Table table_0;
Game g;
static Bot bot;
static PbInput in;

/* what the test reads (test.py): the balls, the flippers' tips, the mode */
struct {
    int8_t mode, nballs;
    int16_t bx[PB_MAX_BALLS], by[PB_MAX_BALLS], bvx[PB_MAX_BALLS], bvy[PB_MAX_BALLS];
    int8_t bstate[PB_MAX_BALLS];
} pin_probe;

/* ------------------------------------------------------------ Layer 2 */

/* 320 mode is column-major: column x is 256 bytes in page L2_BANK*2 + x/32.
   Drawing maps that page into slot 2 (the table's data goes back after). */
static uint8_t *l2col(uint16_t x)
{
    ZXN_WRITE_MMU2(L2_BANK * 2 + (x >> 5));
    return (uint8_t *)0x4000 + ((x & 31) << 8);
}

static void l2_done(void) { ZXN_WRITE_MMU2(DATA_PAGE); }

static void l2rect(uint16_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t c)
{
    for (; w; w--, x++) memset(l2col(x) + y, c, h);
    l2_done();
}

/* a character of the 5x7 font, each dot sx x sy pixels with the lower
   `gap` rows of it left dark (the dot-matrix look) */
static void l2char(uint16_t x, uint8_t y, char ch, uint8_t c, uint8_t sx, uint8_t sy, uint8_t gap)
{
    const uint8_t *gl;
    uint8_t col, r, k, j;
    if (ch < FONT_FIRST || ch > FONT_LAST) return;
    gl = font_data[ch - FONT_FIRST];
    for (col = 0; col < FONT_W; col++)
        for (k = 0; k < sx; k++, x++) {
            uint8_t *p = l2col(x) + y;
            for (r = 0; r < FONT_H; r++, p += sy)
                if (gl[r] & (0x80 >> col))
                    for (j = 0; j < sy - gap; j++) p[j] = c;
        }
}

static uint16_t text_w(const char *s, uint8_t sx) { return (uint16_t)strlen(s) * 6 * sx - sx; }

static void l2text(uint16_t x, uint8_t y, const char *s, uint8_t c, uint8_t sx, uint8_t sy, uint8_t gap)
{
    for (; *s; s++, x += 6 * sx) l2char(x, y, *s, c, sx, sy, gap);
    l2_done();
}

/* ------------------------------------------------------------ the display */

static char shown_big[24], shown_small[40];
static uint8_t shown_col;

/* the original's draw_dmd: one big line, or a big and a small one */
static void dmd(const char *big, const char *small, uint8_t flash)
{
    uint8_t col = flash ? P_DMD_MID : P_DMD_ON, sx;
    if (!small) small = "";
    if (col == shown_col && !strcmp(big, shown_big) && !strcmp(small, shown_small)) return;
    strncpy(shown_big, big, sizeof shown_big - 1);
    strncpy(shown_small, small, sizeof shown_small - 1);
    shown_col = col;
    l2rect(DMD_X, DMD_Y, DMD_W, DMD_H, P_DMD_BG);
    if (!small[0]) {
        sx = text_w(big, 3) <= DMD_W ? 3 : text_w(big, 2) <= DMD_W ? 2 : 1;
        l2text(DMD_X + (DMD_W - text_w(big, sx)) / 2, DMD_Y + (DMD_H - 7 * (sx + 1)) / 2, big, col, sx, sx + 1, 1);
    } else {
        sx = text_w(big, 2) <= DMD_W ? 2 : 1;
        l2text(DMD_X + (DMD_W - text_w(big, sx)) / 2, DMD_Y + 5, big, col, sx, 3, 1);
        l2text(DMD_X + (DMD_W - text_w(small, 1)) / 2, DMD_Y + 31, small, col, 1, 2, 1);
    }
}

/* ------------------------------------------------------------ setup */

static void video_init(void)
{
    uint16_t i;
    next_reg(0x68, 0x80);               /* the ULA off (its memory holds the table's data) */
    next_reg(0x70, 0x10);               /* Layer 2: 320 x 256 */
    next_reg(0x12, L2_BANK);
    next_reg(0x1C, 0x01);               /* reset the Layer 2 clip index */
    next_reg(0x18, 0);                  /* clip: x 0..159 (pairs of pixels), y 0..255 */
    next_reg(0x18, 159);
    next_reg(0x18, 0);
    next_reg(0x18, 255);
    next_reg(0x16, 0);                  /* no scroll */
    next_reg(0x17, 0);
    for (i = 0; i < 256; i++) pal_set(1, (uint8_t)i, l2_pal_0[i]);
    z80_outp(0x123B, 0x02);             /* Layer 2 on */
    spr_init();
    for (i = 0; i < 16; i++) pal_set(2, (uint8_t)i, spr_pal[i]);
    /* the patterns, from bank 35 through slot 2 */
    for (i = 0; i < SPR_NPAT; i++) {
        ZXN_WRITE_MMU2(SPR_PAGE + (i >> 5));
        spr_pattern((uint8_t)i, (const uint8_t *)0x4000 + ((i & 31) << 8));
    }
    l2_done();
}

/* ------------------------------------------------------------ what moves */

#define SX(xp)  (VIEW_OX + (int16_t)(mul16((xp), VIEW_KX_P) >> 16))
#define SY(yp, zmm) (VIEW_OY - (int16_t)(mul16((yp), VIEW_KY_P) >> 16) - (int16_t)(((zmm) * VIEW_KZ_MM) >> 8))

static int16_t ball_px[8], ball_py[8];
static uint8_t ball_phase[8];

static void draw_balls(void)
{
    int8_t i;
    uint8_t n = 0;
    for (i = 0; i < g.w.nballs && n < 4; i++) {
        Ball *b = &g.w.balls[i];
        uint8_t id = b->id & 7;
        int16_t moved;
        if (b->state == BS_DRAINED) continue;
        if (b->y < P_MM(22) && b->x < P_MM(474)) continue;              /* under the apron */
        /* the spin: a frame per ~1.3 mm rolled (render.c) */
        moved = ((b->x > ball_px[id] ? b->x - ball_px[id] : ball_px[id] - b->x) +
                 (b->y > ball_py[id] ? b->y - ball_py[id] : ball_py[id] - b->y)) >> 4;
        if (moved < 200) ball_phase[id] += (b->vx + b->vy >= 0 ? 1 : -1) * ((moved * 3) / 4 + (moved ? 1 : 0));
        ball_px[id] = b->x; ball_py[id] = b->y;
        spr_show(n, SX(b->x) - 8, SY(b->y, 13 + b->z) - 8, SPR_BALL + (ball_phase[id] & 7), 0);
        n++;
    }
    for (; n < 4; n++) spr_hide(n);
}

static void draw_flippers(void)
{
    uint8_t i, s = 4;
    for (i = 0; i < 2; i++) {
        Flipper *f = &g.w.flip[i];
        int16_t span = f->d.up - f->d.rest, x0, y0;
        uint8_t fr = (uint8_t)((mul16(f->ang - f->d.rest, 7) + span / 2) / span), p;
        if (fr > 7) fr = 7;
        p = SPR_FLIP + fr * 4;
        y0 = SY(f->d.py, 0) - FLIP_OY;
        if (f->d.side == 0) {
            x0 = SX(f->d.px) - FLIP_OX;
            spr_show(s++, x0, y0, p, 0);
            spr_show(s++, x0 + 16, y0, p + 1, 0);
            spr_show(s++, x0, y0 + 16, p + 2, 0);
            spr_show(s++, x0 + 16, y0 + 16, p + 3, 0);
        } else {                        /* the left one, mirrored */
            x0 = SX(f->d.px) - (31 - FLIP_OX);
            spr_show(s++, x0, y0, p + 1, SPR_MIRROR_X);
            spr_show(s++, x0 + 16, y0, p, SPR_MIRROR_X);
            spr_show(s++, x0, y0 + 16, p + 3, SPR_MIRROR_X);
            spr_show(s++, x0 + 16, y0 + 16, p + 2, SPR_MIRROR_X);
        }
    }
}

static void draw_plunger(void)
{
    /* render.c: its knob at 30 - 22 pull mm, 8 mm up */
    int16_t top = 30 - (int16_t)((g.pull * 22) / 1000);
    spr_show(12, SX(table_0.plunger_x) - 8, SY(P_MM(top - 6), 8) - 1, SPR_PLUNGER, 0);
}

/* lamps etc.: palette entries, changed only when they change */
static uint8_t lamp_shown[L_LAMPS], drop_shown[8], spin_shown[2];
static uint8_t bumper_flash[PB_MAX_CIRC], sling_flash[2], flash_shown[PB_MAX_CIRC + 2];
static uint16_t frame;

static void set_pal(uint8_t idx, uint16_t c) { pal_set(1, idx, c); }

static void draw_lamps(void)
{
    uint8_t on[L_COUNT], i;
    pb_lamps(&g, (frame / 10) & 1, (uint8_t)(frame / 8), on);
    for (i = 0; i < L_LAMPS; i++)
        if (lamp_used_0[i] && (on[i] != 0) != lamp_shown[i]) {
            lamp_shown[i] = on[i] != 0;
            set_pal(I_LAMP + i, on[i] ? lamp_on_0[i] : lamp_off_0[i]);
        }
    for (i = 0; i < table_0.ndrop && i < 8; i++) {
        uint8_t up = table_0.seg[table_0.drop_seg[i]].up;
        if (up != drop_shown[i]) { drop_shown[i] = up; set_pal(I_DROP + i, up ? l2_pal_0[P_ORANGE] : l2_pal_0[P_BLACK]); }
    }
    for (i = 0; i < table_0.ncirc; i++) {
        uint8_t f = bumper_flash[i] > 0;
        if (bumper_flash[i]) bumper_flash[i]--;
        if (f != flash_shown[i]) { flash_shown[i] = f; set_pal(I_BUMPER + i, f ? l2_pal_0[P_FLASH] : l2_pal_0[P_RED]); }
    }
    for (i = 0; i < 2; i++) {
        uint8_t f = sling_flash[i] > 0;
        if (sling_flash[i]) sling_flash[i]--;
        if (f != flash_shown[PB_MAX_CIRC + i]) { flash_shown[PB_MAX_CIRC + i] = f; set_pal(I_SLING + i, f ? l2_pal_0[P_FLASH] : l2_pal_0[P_RUBBER]); }
    }
    for (i = 0; i < table_0.nspin; i++) {
        uint8_t face = g.w.spin[i].ang < 6434;            /* the blade facing up */
        if (face != spin_shown[i]) { spin_shown[i] = face; set_pal(I_SPIN + i, face ? l2_pal_0[P_METAL_L] : l2_pal_0[P_METAL_D]); }
    }
}

static void forget_lamps(void)
{
    memset(lamp_shown, 2, sizeof lamp_shown);
    memset(drop_shown, 2, sizeof drop_shown);
    memset(spin_shown, 2, sizeof spin_shown);
    memset(flash_shown, 2, sizeof flash_shown);
}

/* ------------------------------------------------------------ sound */

static void sound_event(const Event *e)
{
    uint8_t i;
    switch (e->type) {
    case EV_BUMPER:
        for (i = 0; i < table_0.ncirc; i++)
            if (table_0.circ[i].bumper && table_0.circ[i].id == e->id) bumper_flash[i] = 6;
        ay_sfx(2, 180, 14, 2, 12);
        break;
    case EV_SLING: sling_flash[e->id & 1] = 5; ay_sfx(2, 320, 13, 3, -10); break;
    case EV_FLIPSTOP: if (e->up) ay_sfx(1, 0, 9, 3, 0); break;
    case EV_DROP: ay_sfx(2, 500, 13, 2, 30); break;
    case EV_SPIN: ay_sfx(1, 90, 8, 4, 0); break;
    case EV_RAMP: case EV_LOCK: ay_sfx(2, 400, 14, 1, -12); break;
    case EV_SAUCER: ay_sfx(2, 700, 13, 1, -20); break;
    case EV_KICK: ay_sfx(1, 0, 12, 2, 0); break;
    case EV_PLUNGE: ay_sfx(1, 0, 13, 2, 0); break;
    case EV_DRAIN: ay_sfx(2, 200, 14, 1, 25); break;
    case EV_JACKPOT: case EV_MULTIBALL: case EV_SUPERNOVA: case EV_MISSIONDONE: case EV_SKILL:
        ay_sfx(0, 120, 15, 1, -2); break;
    case EV_TILT: case EV_TILTWARN: ay_sfx(0, 900, 15, 1, 0); break;
    }
}

/* ------------------------------------------------------------ input */

static uint8_t kemp(void) { return input_kempston ? z80_inp(0x001F) : 0; }

static void read_player(PbInput *p, uint8_t nudge_edge)
{
    uint8_t k = kemp();
    memset(p, 0, sizeof *p);
    p->pull_to = -1;
    p->left = key_down(IN_KEY_SCANCODE_z) || key_down(IN_KEY_SCANCODE_CAPS) || key_down(IN_KEY_SCANCODE_o) || (k & 0x02);
    p->right = key_down(IN_KEY_SCANCODE_m) || key_down(IN_KEY_SCANCODE_SYM) || key_down(IN_KEY_SCANCODE_p) || (k & 0x01);
    p->plunger = key_down(IN_KEY_SCANCODE_SPACE) || key_down(IN_KEY_SCANCODE_ENTER) || key_down(IN_KEY_SCANCODE_a) || (k & 0x14);
    if (nudge_edge) p->nudge_y = 1;
}

static uint8_t start_pressed(void)
{
    return key_down(IN_KEY_SCANCODE_SPACE) || key_down(IN_KEY_SCANCODE_ENTER) || (kemp() & 0x10);
}

/* ------------------------------------------------------------ high scores */

static void enter_name(int8_t place)
{
    char name[4] = "A__", small[24];
    uint8_t pos = 0, held = 1;
    zx_log("pinball: hiscore rank=%d\n", place);
    input_read();
    while (pos < 3) {
        char k;
        uint8_t p;
        frame_wait();
        sound_tick();
        strcpy(small, "INITIALS  ");
        strcat(small, name);
        dmd("HIGH SCORE", small, 0);
        input_read();
        p = input_pressed();
        k = input_letter();
        if (k && !held) {
            name[pos++] = k;
            if (pos < 3) name[pos] = 'A';
            held = 1;
            ay_sfx(2, 120, 12, 3, 0);
            continue;
        }
        if (!k) held = 0;
        if (p & IN_UP) name[pos] = name[pos] == 'Z' ? 'A' : name[pos] + 1;
        if (p & IN_DOWN) name[pos] = name[pos] == 'A' ? 'Z' : name[pos] - 1;
        if (p & (IN_FIRE | IN_START)) {
            pos++;
            if (pos < 3) name[pos] = 'A';
            ay_sfx(2, 120, 12, 3, 0);
        }
    }
    hs_insert(place, name, g.score);
    zx_log("pinball: name=%s\n", name);
}

/* ------------------------------------------------------------ the game */

enum { M_ATTRACT, M_PLAY, M_OVER };
static uint8_t mode;
static uint16_t mode_t;

static void new_game(uint8_t demo, uint32_t seed)
{
    uint8_t i;
    pb_game_init(&g, &table_0, seed);
    pb_game_start(&g);
    memset(&bot, 0, sizeof bot);
    bot.rng = seed * 7 + 1;
    mode = demo ? M_ATTRACT : M_PLAY;
    mode_t = 0;
    for (i = 0; i < 8; i++) { ball_px[i] = ball_py[i] = 0; }
    forget_lamps();
    zx_log(demo ? "pinball: title\n" : "pinball: start\n");
}

/* the attract mode's display: the name, the prompt, the best scores in turn */
static void attract_dmd(void)
{
    uint8_t page = (uint8_t)((mode_t / 150) % (HS_ENTRIES + 1));
    if (page == 0) dmd("BOING PINBALL", "PRESS FIRE - COMET TOUR", 0);
    else {
        char big[16], small[24];
        hs_entry *h = &hs_table[page - 1];
        big[0] = '0' + page; big[1] = ' '; strcpy(big + 2, h->name);
        pb_fmt(small, (int32_t)h->score);
        dmd(big, small, 0);
    }
}

static void log_events(void)
{
    uint8_t i;
    for (i = 0; i < g.nev; i++) {
        const Event *e = &g.ev[i];
        sound_event(e);
        if (mode != M_PLAY) continue;
        switch (e->type) {
        case EV_DRAIN: zx_log("pinball: drain score=%ld\n", (long)g.score); break;
        case EV_ENDBALL: zx_log("pinball: endball ball=%d score=%ld\n", g.ball_no, (long)g.score); break;
        case EV_BUMPER: zx_log("pinball: bumper\n"); break;
        case EV_RAMP: zx_log("pinball: ramp\n"); break;
        case EV_SAUCER: zx_log("pinball: saucer\n"); break;
        case EV_DROP: zx_log("pinball: drop\n"); break;
        case EV_MULTIBALL: zx_log("pinball: multiball\n"); break;
        case EV_SAVE: zx_log("pinball: save\n"); break;
        case EV_TILT: zx_log("pinball: tilt\n"); break;
        case EV_GAMEOVER: zx_log("pinball: gameover score=%ld overruns=%u\n", (long)g.score, frame_overruns); break;
        }
    }
    g.nev = 0;
}

static void update_probe(void)
{
    int8_t i;
    pin_probe.mode = mode;
    pin_probe.nballs = g.w.nballs;
    for (i = 0; i < g.w.nballs; i++) {
        pin_probe.bx[i] = g.w.balls[i].x; pin_probe.by[i] = g.w.balls[i].y;
        pin_probe.bvx[i] = g.w.balls[i].vx; pin_probe.bvy[i] = g.w.balls[i].vy;
        pin_probe.bstate[i] = g.w.balls[i].state;
    }
}

void front_main(void)
{
    uint8_t acc = 0, ticks = 1, nudge_was = 0, start_was = 1, n;
    int32_t last_score = -1;
    video_init();
    input_init();
    ay_init();
    hs_init(20000);
    new_game(1, 1);
    zx_log("pinball: ready ay=%u kempston=%u\n", ay_present, input_kempston);

    for (;;) {
        /* the simulation: 120 steps a second, 12 every 5 frames */
        uint8_t nudge, start;
        PbInput player;
        nudge = key_down(IN_KEY_SCANCODE_q) || (kemp() & 0x08);
        start = start_pressed();
        read_player(&player, nudge && !nudge_was);
        nudge_was = nudge;
        acc += 12 * ticks;
        for (n = 0; acc >= 5 && n < 7; n++, acc -= 5) {
            if (mode == M_PLAY) pb_game_tick(&g, &player);
            else if (mode == M_ATTRACT) { pb_bot(&g, &in, &bot); pb_game_tick(&g, &in); }
            else { PbInput none; memset(&none, 0, sizeof none); none.pull_to = -1; pb_game_tick(&g, &none); }
            player.nudge_y = 0;
            log_events();
            sound_tick();
        }
        if (acc > 10) acc = 10;                 /* behind: let it go (the game slows a little) */
        frame++;
        mode_t++;

        if (mode == M_ATTRACT) {
            if (g.state == GS_OVER) new_game(1, frame);
            if (start && !start_was) new_game(0, frame | 1);
        } else if (mode == M_PLAY && g.state == GS_OVER) {
            mode = M_OVER;
            mode_t = 0;
        } else if (mode == M_OVER && mode_t > 150 && start && !start_was) {
            int8_t place = hs_rank((uint32_t)g.score);
            if (place >= 0) enter_name(place);
            shown_big[0] = 0;
            new_game(1, frame);
        }
        start_was = start;
        if (mode == M_PLAY && g.score != last_score) {
            zx_log("pinball: score=%ld ball=%d\n", (long)g.score, g.ball_no);
            last_score = g.score;
        }

        draw_lamps();
        if (mode == M_ATTRACT) attract_dmd();
        else {
            Dmd d;
            pb_dmd(&g, &d);
            dmd(d.big, d.small, d.flash && (frame & 4));
        }
        update_probe();
        ticks = frame_wait();
        draw_balls();
        draw_flippers();
        draw_plunger();
    }
}
