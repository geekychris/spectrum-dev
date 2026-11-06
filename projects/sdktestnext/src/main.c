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
    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
