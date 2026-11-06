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
#include "font5.h"

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

/* the SDK's 320 mode helpers (l2w_*, nextgfx.h), through slot 2: the
   table's data goes back there after drawing */
#define l2rect(x, y, w, h, c)  l2w_fill(x, y, w, h, c)
#define l2text  l2w_text5
#define text_w  font5_width

/* ------------------------------------------------------------ the display */

/* Layer 2 has two palettes; NextReg 0x43 bit 2 picks the one shown. Entries
   are written to both (keeping that bit), so the display can switch in one
   write (dmd_update). */
static uint8_t l2pal_shown;

static void l2pal(uint8_t second, uint8_t idx, uint16_t c)
{
    ZXN_WRITE_REG(0x43, (second ? 0x50 : 0x10) | (l2pal_shown << 2));
    ZXN_WRITE_REG(0x40, idx);
    ZXN_WRITE_REG(0x44, (uint8_t)(c >> 1));
    ZXN_WRITE_REG(0x44, (uint8_t)(c & 1));
}

static void set_pal(uint8_t idx, uint16_t c)
{
    l2pal(0, idx, c);
    l2pal(1, idx, c);
}

/* The original's draw_dmd (one big line, or a big and a small one),
   double buffered through the palette: the dots of the text being shown
   are drawn in one pen, the next text's in another whose colour is the
   background's (dots in both: a third pen, always lit). The next text is
   drawn a pixel column at a time, only columns that differ, at most
   DMD_BUDGET a frame, right after the frame's interrupt; when every column
   is ready, the colours for the swapped pens go into the palette that
   isn't shown and one register write shows it: the whole display changes
   at once. A column is composed and copied so every pixel goes straight to
   its final pen (a visible dot only ever turns into the always-lit pen):
   it is never seen half drawn, whenever you look. Flashing is the palette. */
#define DMD_BUDGET 16                   /* ~4.4K T-states a column: done before the beam reaches row 58 */
#define PEN_A  88
#define PEN_B  89
#define PEN_AB 90

typedef struct { const char *s; int16_t x0; uint8_t n, sx, y, sy; } dmd_line;
typedef struct { uint8_t layout; dmd_line l[2]; } dmd_geo;
static char want_big[24], want_small[40];
char dmd_vis_big[24], dmd_vis_small[40];        /* the text that is visible (test.py) */
static dmd_geo geo_want, geo_vis, geo_hid;       /* where each pen's dots are */
static uint8_t pen_vis = PEN_A, pen_hid = PEN_B, dmd_cursor, dmd_pending, dmd_flash;
/* per column: the dots the next text needs, and what each pen holds there
   (line 0's dots | line 1's << 7); initialised: in the front end's page */
static uint16_t col_want[DMD_W] = {0}, sig_a[DMD_W] = {0}, sig_b[DMD_W] = {0};
static uint16_t *sig_vis = sig_a, *sig_hid = sig_b;
static uint8_t colbuf[DMD_H] = {0};

static void dmd_set_line(dmd_line *l, const char *s, uint8_t sx, uint8_t y, uint8_t sy)
{
    l->s = s;
    l->n = (uint8_t)strlen(s);
    l->sx = sx;
    l->x0 = (DMD_W - (int16_t)text_w(s, sx)) / 2;
    l->y = y;
    l->sy = sy;
}

/* a line's dots into col_want, column by column (no divisions) */
static void dmd_columns(const dmd_line *l, uint8_t shift)
{
    int16_t x = l->x0;
    uint8_t k, c, r, j;
    for (k = 0; k < l->n; k++) {
        const uint8_t *g = font5_glyph(l->s[k]);
        for (c = 0; c < 6; c++) {
            uint16_t bits = 0;
            if (g && c < 5)
                for (r = 0; r < 7; r++)
                    if (g[r] & (0x80 >> c)) bits |= 1 << r;
            bits <<= shift;
            for (j = 0; j < l->sx; j++, x++)
                if (x >= 0 && x < DMD_W) col_want[x] |= bits;
        }
    }
}

/* the pens' colours in palette `which` (0 / 1) */
static void dmd_colours(uint8_t which)
{
    uint16_t on = l2_pal_0[dmd_flash ? P_DMD_MID : P_DMD_ON];
    l2pal(which, pen_vis, on);
    l2pal(which, PEN_AB, on);
    l2pal(which, pen_hid, l2_pal_0[P_DMD_BG]);
}

static void dmd(const char *big, const char *small, uint8_t flash)
{
    uint8_t sx;
    if (!small) small = "";
    if (flash != dmd_flash) { dmd_flash = flash; dmd_colours(l2pal_shown); }
    if (!strcmp(big, want_big) && !strcmp(small, want_small)) return;
    strncpy(want_big, big, sizeof want_big - 1);
    strncpy(want_small, small, sizeof want_small - 1);
    if (!small[0]) {
        sx = text_w(big, 3) <= DMD_W ? 3 : text_w(big, 2) <= DMD_W ? 2 : 1;
        dmd_set_line(&geo_want.l[0], want_big, sx, (DMD_H - 7 * (sx + 1)) / 2, sx + 1);
        geo_want.l[1].n = 0;
        geo_want.layout = sx;
    } else {
        sx = text_w(big, 2) <= DMD_W ? 2 : 1;
        dmd_set_line(&geo_want.l[0], want_big, sx, 5, 3);
        dmd_set_line(&geo_want.l[1], want_small, 1, 31, 2);
        geo_want.layout = 4 + sx;
    }
    memset(col_want, 0, sizeof col_want);
    dmd_columns(&geo_want.l[0], 0);
    if (geo_want.l[1].n) dmd_columns(&geo_want.l[1], 7);
    if (geo_want.layout != geo_hid.layout) {     /* the hidden pen's dots move: redo every column */
        uint8_t x;
        geo_hid = geo_want;
        for (x = 0; x < DMD_W; x++) sig_hid[x] = 0xFFFF;
    }
    dmd_cursor = 0;
    dmd_pending = 1;
}

/* a pen's dots into a column being composed: `bits` rows of the line at y,
   sy apart; where the other text has a dot too, the always-lit pen */
static void dots(uint8_t *p, uint8_t bits, uint8_t sy, uint8_t pen)
{
    uint8_t j;
    for (; bits; bits >>= 1, p += sy)
        if (bits & 1)
            for (j = 0; j < sy - 1; j++) p[j] = p[j] == P_DMD_BG ? pen : PEN_AB;
}

/* the beam's video line (0 = the first line of the 256 x 192 paper area;
   Layer 2's 320 x 256 row r is line r - 32). The display is lines 26-74;
   its work (at most ~50 lines' worth) starts only below it, so the beam
   never passes it while it changes, even after a slow frame. */
static uint8_t beam_clear_of_dmd(void)
{
    uint16_t line = ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);
    return line >= DMD_Y + DMD_H - 32 && line <= 280;
}

static void dmd_update(void)
{
    uint8_t n = 0;
    if (!dmd_pending || !beam_clear_of_dmd()) return;
    if (dmd_cursor >= DMD_W) {                   /* all drawn: swap at the frame's start */
        uint16_t *t = sig_vis;
        dmd_geo g = geo_vis;
        uint8_t p = pen_vis;
        pen_vis = pen_hid; pen_hid = p;
        sig_vis = sig_hid; sig_hid = t;
        geo_vis = geo_hid; geo_hid = g;
        dmd_colours(l2pal_shown ^ 1);           /* the hidden palette, then show it: one write */
        l2pal_shown ^= 1;
        ZXN_WRITE_REG(0x43, 0x10 | (l2pal_shown << 2));
        strcpy(dmd_vis_big, want_big);
        strcpy(dmd_vis_small, want_small);
        dmd_pending = 0;
        return;
    }
    while (dmd_cursor < DMD_W && n < DMD_BUDGET) {
        uint8_t x = dmd_cursor++;
        uint16_t w = col_want[x], v = sig_vis[x];
        uint8_t *p;
        if (w == sig_hid[x]) continue;
        sig_hid[x] = w;
        n++;
        memset(colbuf, P_DMD_BG, DMD_H);
        if (v & 0x7F) dots(colbuf + geo_vis.l[0].y, v & 0x7F, geo_vis.l[0].sy, pen_vis);
        if (v >> 7) dots(colbuf + geo_vis.l[1].y, v >> 7, geo_vis.l[1].sy, pen_vis);
        if (w & 0x7F) dots(colbuf + geo_hid.l[0].y, w & 0x7F, geo_hid.l[0].sy, pen_hid);
        if (w >> 7) dots(colbuf + geo_hid.l[1].y, w >> 7, geo_hid.l[1].sy, pen_hid);
        p = l2w_column(DMD_X + x) + DMD_Y;
        memcpy(p, colbuf, DMD_H);
    }
    if (n) l2w_done();
}

/* ------------------------------------------------------------ setup */

static void video_init(void)
{
    uint16_t i;
    next_reg(0x68, 0x80);               /* the ULA off (its memory holds the table's data) */
    l2w_slot = 2;
    l2w_restore = DATA_PAGE;
    l2w_init(L2_BANK);
    pal_load(1, l2_pal_0, 256);
    spr_init();
    pal_load(2, spr_pal, 16);
    spr_patterns_from_pages(SPR_PAGE, 2, 0, SPR_NPAT, DATA_PAGE);
    for (i = 0; i < 256; i++) l2pal(1, (uint8_t)i, l2_pal_0[i]);   /* the second palette: the same */
    dmd_colours(0);
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
        /* the right one is the left one mirrored */
        x0 = SX(f->d.px) - (f->d.side == 0 ? FLIP_OX : 31 - FLIP_OX);
        spr_show_quad(s, x0, y0, p, f->d.side != 0);
        s += 4;
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
    hs_editor ed;
    char small[24];
    uint8_t r = HS_EDIT_CHANGED;
    zx_log("pinball: hiscore rank=%d\n", place);
    hs_edit_begin(&ed);
    while (!(r & HS_EDIT_DONE)) {
        if (r & HS_EDIT_CHANGED) {
            strcpy(small, "INITIALS  ");
            strcat(small, ed.name);
            dmd("HIGH SCORE", small, 0);
        }
        if (r & HS_EDIT_TAKEN) ay_sfx(2, 120, 12, 3, 0);
        frame_wait();
        dmd_update();
        sound_tick();
        input_read();
        r = hs_edit_step(&ed);
    }
    hs_insert(place, ed.name, g.score);
    zx_log("pinball: name=%s\n", ed.name);
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
            want_big[0] = 0;
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
            dmd(d.big, d.small, d.flash && (frame & 4));     /* (drawn after the frame's interrupt) */
        }
        update_probe();
        ticks = frame_wait();
        dmd_update();                           /* first: the beam is above the display */
        draw_balls();
        draw_flippers();
        draw_plunger();
    }
}
