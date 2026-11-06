/*
 * sdktestnext - exercises the Next part of the game layer (nextgfx.h) plus the
 * common parts on the Next; test.py checks the layers and the log.
 */
#include <stdint.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "fixmath.h"
#include "modplay.h"

static uint8_t ball[256];

int main(void)
{
    uint8_t i, x, y;

    next_init();
    frame_init();
    input_init();
    ay_init();
    zx_log("sdk: speed=%u ay=%u\n", next_reg_read(0x07) & 3, ay_present);

    l2_init();
    /* back buffer: a frame, a filled box, text, a transparent-keyed blit */
    l2_clear(RGB8(0, 0, 1));
    l2_rect(0, 0, 256, 192, RGB8(7, 7, 3));
    l2_fill(20, 30, 40, 20, RGB8(7, 0, 0));
    l2_text(80, 30, "NEXT", RGB8(0, 7, 0), 2);
    l2_text5(150, 60, "FONT5", RGB8(0, 7, 3), 2);       /* the SDK's 5x7 font */
    for (i = 0; i < 64; i++) ball[i] = (i & 1) ? RGB8(7, 7, 0) : L2_TRANSPARENT;
    l2_blit(20, 100, 8, 8, ball, L2_TRANSPARENT);
    l2_plot(200, 150, RGB8(7, 0, 7));
    zx_log("sdk: front before flip=%u\n", next_reg_read(0x12));
    frame_wait();
    l2_flip();
    zx_log("sdk: front after flip=%u back=%u\n", next_reg_read(0x12), l2_back);

    /* palette entry 1 of Layer 2 = pure blue, read back */
    pal_set8(1, 1, RGB8(0, 0, 3));
    next_reg(0x43, 0x10);
    next_reg(0x40, 1);
    zx_log("sdk: l2 palette[1]=%02x\n", next_reg_read(0x41));
    next_reg(0x43, 0x00);

    /* sprites: a 16x16 ball, two copies, one mirrored */
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            int8_t dx = (int8_t)(x * 2) - 15, dy = (int8_t)(y * 2) - 15;
            ball[y * 16 + x] = dx * dx + dy * dy <= 225 ? (x < 8 ? RGB8(0, 7, 3) : RGB8(7, 3, 0)) : L2_TRANSPARENT;
        }
    spr_init();
    spr_pattern(0, ball);
    spr_show(0, 100, 80, 0, 0);
    spr_show(1, 200, 120, 0, SPR_MIRROR_X);
    spr_show(2, 300, 200, 0, 0);
    spr_hide(2);

    /* TurboSound: chip 1 */
    ay_chip(1);
    ay_init();
    ay_tone(0, 400, 10);
    z80_outp(0xFFFD, 0);
    zx_log("sdk: ts chip1 r0=%u\n", z80_inp(0xFFFD));
    ay_chip(0);

    /* modplay: a two-row song, a tone on chip 1 A and noise on chip 0 B */
    {
        static const mod_inst ins[2] = {{MI_TONE, 12, 0, 1, 0}, {MI_NOISE, 9, 1, 1, 7}};
        static const mod_voice v0[1] = {{300, 0}}, v1[1] = {{0, 1}};
        static const uint8_t rows[8] = {1, 1, 0, 0, 0, 0, 0, 0};
        static const mod_song song = {2, 3, rows, {v0, v1, 0, 0}, ins, {MOD_ROUTE(1, 0), MOD_ROUTE(0, 1), 0xFF, 0xFF}};
        uint8_t r0, r8, r7, b9, a7;
        ay_chip(0);
        ay_init();
        ay_sfx(2, 500, 15, 1, 0);                       /* an effect on chip 0 C: its mixer bit stays */
        modplay_start(&song);
        modplay_tick();
        modplay_tick();
        ay_chip(1);
        z80_outp(0xFFFD, 0); r0 = z80_inp(0xFFFD);
        z80_outp(0xFFFD, 8); r8 = z80_inp(0xFFFD);
        z80_outp(0xFFFD, 7); r7 = z80_inp(0xFFFD);
        ay_chip(0);
        z80_outp(0xFFFD, 9); b9 = z80_inp(0xFFFD);
        z80_outp(0xFFFD, 7); a7 = z80_inp(0xFFFD);
        zx_log("sdk: modplay c1a=%u,%u mix1=%02x c0b=%u mix0=%02x\n", r0, r8, r7, b9, a7);
        modplay_stop();
    }
    {   /* fixmath.h: signs, shifts, square roots */
        int32_t m1 = mul16(-300, 1234), m2 = mul16(-32768, -32768), m3 = mul16(32767, -2);
        int16_t q1 = mulq14(-16384, 5), q2 = mulq14(12345, 6789), q3 = mulq8(-1000, 77);
        uint16_t s1 = isqrt32(4294967295UL), s2 = isqrt32(90000), s3 = isqrt32(2);
        int16_t d1 = div_q14(-3, 4), d2 = div_q14(100, 100);
        zx_log("sdk: fixmath %ld %ld %ld %d %d %d %u %u %u %d %d\n", m1, m2, m3, q1, q2, q3, s1, s2, s3, d1, d2);
    }
    {   /* zxnDMA: fill then copy a buffer */
        static uint8_t a[300], b[300];
        uint16_t k, ok = 0;
        for (k = 0; k < 300; k++) a[k] = (uint8_t)(k * 7);
        dma_fill(b, 0x5A, 300);
        ok += b[0] == 0x5A && b[299] == 0x5A;
        dma_copy(b + 1, a, 298);
        ok += b[0] == 0x5A && b[1] == 0 && b[2] == 7 && b[298] == (uint8_t)(297 * 7) && b[299] == 0x5A;
        zx_log("sdk: dma ok=%u\n", ok);
    }
    rnd_seed(1);
    {
        uint16_t r1 = rnd16(), r2 = rnd16();
        zx_log("sdk: rnd=%u,%u fix_div=%d sin32=%d\n", r1, r2, fix_div(FIX(3), FIX(2)), fix_sin(32));
    }
    frame_wait();
    i = 0;
    {
        uint16_t f0 = frame_count;
        for (i = 0; i < 25; i++) frame_wait();
        zx_log("sdk: frames=%u\n", frame_count - f0);
    }
    zx_log("sdk: input ready\n");
    for (;;) {
        uint8_t p;
        frame_wait();
        input_read();
        p = input_pressed();
        if (p) zx_log("sdk: pressed=%02x\n", p);
        if (p & IN_START) break;
    }
    /* Layer 2 320 x 256 (l2w_*), a 32 x 32 quad sprite, pal_load */
    {
        static const uint16_t pal4[4] = {0x000, 0x1C0, 0x038, 0x007};   /* black red green blue */
        uint8_t q;
        l2w_init(9);
        pal_load(1, pal4, 4);
        l2w_fill(0, 0, 320, 256, 0);
        l2w_fill(290, 200, 20, 30, 1);                  /* right of the 256-wide area, in the border */
        l2w_text5(10, 10, "WIDE", 2, 2, 2, 0);
        for (q = 0; q < 4; q++) {                       /* four patterns: quadrant colours */
            uint16_t i;
            for (i = 0; i < 256; i++) ball[i] = q == 0 ? RGB8(7, 0, 0) : q == 1 ? RGB8(0, 7, 0) : q == 2 ? RGB8(0, 0, 3) : RGB8(7, 7, 0);
            spr_pattern(10 + q, ball);
        }
        spr_show_quad(20, 100, 100, 10, 0);
        spr_show_quad(24, 200, 100, 10, 1);
        zx_log("sdk: wide ready\n");
        for (q = 0; q < 10; q++) frame_wait();
    }
    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
