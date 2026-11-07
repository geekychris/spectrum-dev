/*
 * sdktest - exercises the game layer (zxgame.h) on a 48K/128K and logs the
 * results; test.py checks them against Python and the screen.
 */
#include <stdint.h>
#include <z80.h>
#include <arch/zx.h>
#include "zxgame.h"
#include "zxlog.h"
#include "fixmath.h"
#include "zxtext.h"
#include "line48.h"
#include "vec48.h"
#include "hit48.h"
#include "sprclip.h"

#define FRAMES frame_ticks       /* counted by the SDK's IM 2 handler */

/* a 16x4 test sprite: a bar with a notch */
static const uint8_t bar[8] = {0xFF, 0xFF, 0x81, 0x81, 0x81, 0x81, 0xFF, 0xFF};
static uint8_t bar_pre[8 * 3 * 4];

static const ay_note tune[] = {{0, 0}};

/* line48 / vec48: test.py compares the screen with a model of the same
   Bresenham, then the program clears it and goes on */
static const vec_pt octagon[8] = {{-6, -14}, {6, -16}, {16, -6}, {14, 3}, {10, 14}, {-3, 16}, {-14, 8}, {-16, -5}};
static const vec_pt arrow[5] = {{10, 0}, {-7, -6}, {-4, -2}, {-4, 2}, {-7, 6}};
static const uint8_t fan_end[16][2] = {
    {90, 157}, {88, 170}, {82, 182}, {68, 187}, {60, 187}, {50, 186}, {35, 175}, {30, 160},
    {30, 157}, {31, 140}, {40, 130}, {55, 127}, {60, 127}, {72, 128}, {85, 136}, {89, 150},
};
static const uint8_t dots[8] = {5, 5, 6, 5, 250, 190, 7, 200};   /* the last is off screen */

static void line_tests(void)
{
    uint8_t i, f0, f1;
    vec_pt rot[5];
    line48_init();
    line_mode = LINE_OR;
    for (i = 0; i < 16; i++) line_draw(60, 157, fan_end[i][0], fan_end[i][1]);
    line_draw(100, 125, 100, 125);                               /* one pixel */
    line_clip(-40, 100, 300, 110);                               /* clipped both ends */
    line_clip(-10, -10, -1, 50);                                 /* off screen */
    line_clip(250, 180, 270, 200);                               /* clipped at the corner */
    plot_list(dots, 4);
    line_mode = LINE_XOR;
    line_poly(128, 2, octagon, 8, 1);                            /* wraps to the bottom */
    line_poly(190, 128, octagon, 8, 1);
    line_poly(196, 132, octagon, 8, 1);                          /* overlaps, then both go */
    line_poly(190, 128, octagon, 8, 1);
    line_poly(196, 132, octagon, 8, 1);
    line_poly(220, 60, arrow, 5, 0);                             /* open polyline */
    {
        static uint8_t oct_c[POLY_BYTES(8)];
        static poly_at shown[3], want[3];
        poly_compile(octagon, 8, oct_c);
        line_poly(60, 50, octagon, 8, 1);
        poly_draw(60, 50, oct_c);                                /* the same pixels: gone */
        want[0].x = 100; want[0].y = 40; want[0].poly = oct_c;
        want[2].x = 30; want[2].y = 30;                          /* no polygon */
        poly_sync(shown, want, 3);
        want[0].x = 104; want[0].y = 44;
        poly_sync(shown, want, 3);                               /* moved */
        poly_sync(shown, want, 3);                               /* nothing changed */
    }
    vec_rotate(arrow, 5, 32, 205, rot);
    line_mode = LINE_OR;
    line_poly(160, 60, rot, 5, 1);
    text_xor(0, 3, "XOR", 1);
    text_xor(0, 3, "XOR", 1);
    text_xor(4, 3, "AB", 0);
    zx_log("sdk: rot=%d,%d,%d,%d,%d,%d\n", rot[0].x, rot[0].y, rot[1].x, rot[1].y, rot[4].x, rot[4].y);
    {
        vec_pt tab[4 * 5];
        vec_rot_table(arrow, 5, 256, 2, tab);
        zx_log("sdk: rottab=%d,%d,%d,%d at250=%d\n", tab[5].x, tab[5].y, tab[10].x, tab[15].y,
               (int)(vec_rot_at(tab, 5, 2, 250) - tab));
    }
    {
        static const hit_circle circ[4] = {{100, 100, 10, 0}, {300, -50, 20, 1}, {-200, 80, 5, 2}, {150, 100, 8, 3}};
        static const int16_t q[8][3] = {{105, 104, 2}, {325, -50, 4}, {320, -45, 4}, {-200, 80, 0},
                                        {156, 106, 2}, {1000, 1000, 5}, {-197, 84, 0}, {400, 100, 3}};
        uint8_t r[8];
        for (i = 0; i < 8; i++) r[i] = hit_first(q[i][0], q[i][1], (uint8_t)q[i][2], circ, 4);
        zx_log("sdk: hit=%u,%u,%u,%u,%u,%u,%u,%u\n", r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    }
    zx_log("sdk: lines drawn\n");
    /* cost: 50 XOR octagons */
    line_mode = LINE_XOR;
    frame_wait();
    f0 = frame_ticks;
    for (i = 0; i < 50; i++) line_poly(60, 60, octagon, 8, 1);
    f1 = frame_ticks;
    zx_log("sdk: 50 octagons frames=%u\n", (uint8_t)(f1 - f0));
}

int main(void)
{
    uint8_t i, f0;
    int8_t r;

    gfx_init();
    screen_clear(PAPER_WHITE | INK_BLACK, INK_BLUE);
    frame_init();
    line_tests();
    screen_clear(PAPER_WHITE | INK_BLACK, INK_BLUE);
    input_init();
    ay_init();
    if (ay_present) input_fast();            /* the 128K pass checks the table decoder */

    zx_log("sdk: fix_mul=%d fix_div=%d sin64=%d cos0=%d sin192=%d sin32=%d\n",
           fix_mul(FIX(1.5), FIX(2)), fix_div(FIX(3), FIX(2)), fix_sin(64), fix_cos(0),
           fix_sin(192), fix_sin(32));
    zx_log("sdk: fix_div2=%d,%d,%d\n", fix_div(FIX(-3), FIX(2)), fix_div(FIX(1), FIX(4)), fix_div(100, -50));
    zx_log("sdk: atan11=%u atan-1-1=%u atan01=%u atan-10=%u isqrt=%u,%u,%u\n",
           fix_atan2(1, 1), fix_atan2(-1, -1), fix_atan2(1, 0), fix_atan2(0, -1),
           isqrt16(10000), isqrt16(65535), isqrt16(2));
    {   /* fixmath.h: signs, shifts, square roots */
        int32_t m1 = mul16(-300, 1234), m2 = mul16(-32768, -32768), m3 = mul16(32767, -2);
        int16_t q1 = mulq14(-16384, 5), q2 = mulq14(12345, 6789), q3 = mulq8(-1000, 77);
        uint16_t s1 = isqrt32(4294967295UL), s2 = isqrt32(90000), s3 = isqrt32(2);
        int16_t d1 = div_q14(-3, 4), d2 = div_q14(100, 100);
        zx_log("sdk: fixmath %ld %ld %ld %d %d %d %u %u %u %d %d\n", m1, m2, m3, q1, q2, q3, s1, s2, s3, d1, d2);
    }
    rnd_seed(1);
    {   /* sdcc evaluates arguments right to left: call in order first */
        uint16_t r1 = rnd16(), r2 = rnd16(), r3 = rnd16(), r4 = rnd16(), r5 = rnd_range(10);
        zx_log("sdk: rnd=%u,%u,%u,%u range=%u\n", r1, r2, r3, r4, r5);
    }

    hs_init(1000);
    r = hs_rank(4500);
    hs_insert(r, "BOT", 4500);
    zx_log("sdk: hs rank=%d top=%s,%lu second=%s,%lu last=%lu low=%d\n", r,
           hs_table[0].name, hs_table[0].score, hs_table[1].name, hs_table[1].score,
           hs_table[4].score, hs_rank(10));

    /* graphics: sprite at (13,21) xor'd twice at (100,50) (= erased), big text */
    spr_preshift(bar, 2, 4, bar_pre);
    spr_or(13, 21, bar_pre, 2, 4);
    spr_xor(100, 50, bar_pre, 2, 4);
    spr_xor(100, 50, bar_pre, 2, 4);
    spr_xor(250, 60, bar_pre, 2, 4);         /* clipped at the right edge */
    /* sprclip.h: signed x, clipped at both edges and the bottom */
    sprc_xor(-5, 70, bar_pre, 2, 4);
    sprc_xor(250, 80, bar_pre, 2, 4);
    sprc_xor(120, 90, bar_pre, 2, 4);
    sprc_xor(120, 90, bar_pre, 2, 4);        /* erased again */
    sprc_xor(-24, 100, bar_pre, 2, 4);       /* all off the left: nothing */
    sprc_xor(256, 100, bar_pre, 2, 4);       /* all off the right: nothing */
    sprc_xor(60, 190, bar_pre, 2, 4);        /* two lines left at the bottom */
    text_big(2, 10, "BIG");
    text_num(2, 14, 1234567, 8);
    gfx_fill_rect(20, 100, 4, 8, 0xAA);
    attr_fill(20, 12, 4, 1, PAPER_RED | INK_YELLOW | BRIGHT);
    tile_draw(30, 20, bar);
    gfx_plot(200, 150);
    zx_log("sdk: gfx point=%u,%u\n", gfx_point(200, 150), gfx_point(201, 150));
    hs_draw(18, 2);

    /* beeper: ~2 * 200 * (100*26 + 40) T-states = ~15 frames */
    f0 = FRAMES;
    beep(100, 200);
    zx_log("sdk: beep frames=%u\n", (uint8_t)(FRAMES - f0));
    f0 = FRAMES;
    beep_noise(2000);
    zx_log("sdk: noise frames=%u\n", (uint8_t)(FRAMES - f0));

    /* AY: present on 128K machines */
    zx_log("sdk: ay=%u kempston=%u\n", ay_present, input_kempston);
    if (ay_present) {
        ay_tone(0, 284, 12);
        z80_outp(0xFFFD, 0); i = z80_inp(0xFFFD);
        z80_outp(0xFFFD, 8);
        zx_log("sdk: ay r0=%u r8=%u\n", i, z80_inp(0xFFFD));
        ay_sfx(1, 500, 15, 3, 10);
        for (i = 0; i < 3; i++) { frame_wait(); sound_tick(); }
        z80_outp(0xFFFD, 9);
        zx_log("sdk: ay sfx vol=%u\n", z80_inp(0xFFFD));
        music_play(2, tune, 10);
        music_stop();
        ay_off(0);
    }

    /* frame timer: 50 waits = 50 frames, then an overrun */
    frame_wait();
    f0 = FRAMES;
    for (i = 0; i < 50; i++) frame_wait();
    zx_log("sdk: frames waited=%u overruns=%u\n", (uint8_t)(FRAMES - f0), frame_overruns);
    beep(200, 300);                          /* > 2 frames of beeping */
    frame_wait();
    zx_log("sdk: overruns after a long beep=%u\n", frame_overruns);

    /* the initials editor, latched: the loop only reads every 3 frames, the
       test taps keys for 1 frame */
    frame_hook = input_sample;
    input_latch(1);
    {
        hs_editor ed;
        uint8_t r = 0;
        zx_log("sdk: hs edit ready\n");
        hs_edit_begin(&ed);
        while (!(r & HS_EDIT_DONE)) {
            frame_wait(); frame_wait(); frame_wait();
            input_read();
            r = hs_edit_step(&ed);
        }
        zx_log("sdk: hs edit name=%s\n", ed.name);
    }
    input_latch(0);
    frame_hook = 0;

    zx_log("sdk: input ready\n");
    for (;;) {
        uint8_t b, p;
        frame_wait();
        b = input_read();
        p = input_pressed();
        if (p) zx_log("sdk: input=%02x pressed=%02x letter=%c\n", b, p, input_letter() ? input_letter() : '-');
        if (p & IN_START) break;
    }
    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
