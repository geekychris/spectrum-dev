/*
 * sdktest - exercises the game layer (zxgame.h) on a 48K/128K and logs the
 * results; test.py checks them against Python and the screen.
 */
#include <stdint.h>
#include <z80.h>
#include <arch/zx.h>
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"

#define FRAMES (*(volatile uint8_t *)23672)

/* a 16x4 test sprite: a bar with a notch */
static const uint8_t bar[8] = {0xFF, 0xFF, 0x81, 0x81, 0x81, 0x81, 0xFF, 0xFF};
static uint8_t bar_pre[8 * 3 * 4];

static const ay_note tune[] = {{0, 0}};

int main(void)
{
    uint8_t i, f0;
    int8_t r;

    gfx_init();
    screen_clear(PAPER_WHITE | INK_BLACK, INK_BLUE);
    frame_init();
    input_init();
    ay_init();

    zx_log("sdk: fix_mul=%d fix_div=%d sin64=%d cos0=%d sin192=%d sin32=%d\n",
           fix_mul(FIX(1.5), FIX(2)), fix_div(FIX(3), FIX(2)), fix_sin(64), fix_cos(0),
           fix_sin(192), fix_sin(32));
    zx_log("sdk: fix_div2=%d,%d,%d\n", fix_div(FIX(-3), FIX(2)), fix_div(FIX(1), FIX(4)), fix_div(100, -50));
    zx_log("sdk: atan11=%u atan-1-1=%u atan01=%u atan-10=%u isqrt=%u,%u,%u\n",
           fix_atan2(1, 1), fix_atan2(-1, -1), fix_atan2(1, 0), fix_atan2(0, -1),
           isqrt16(10000), isqrt16(65535), isqrt16(2));
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
