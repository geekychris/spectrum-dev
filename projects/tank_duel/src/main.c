/*
 * Tank Duel for the Spectrum Next: memory map, start-up and the frame loop.
 *
 *   0x0000-0x3FFF  bank 16: the rules (td_core.c)          } map_rules()
 *   0x4000-0x7FFF  bank 17: the AI, the fixed point        }
 *   0x0000-0x3FFF  bank 18: the renderer, the models       } map_render()
 *   0x4000-0x5FFF  page 40: the renderer's variables       }
 *   0x6000-0x7FFF  Layer 2 pages while drawing (l2poly)
 *   0x0000-0x3FFF  bank 19: the front end (front.c)        } map_front()
 *   0x4000-0x7FFF  bank 5 (the tilemap; nexttile maps it into slot 2)
 *   0x8000-0xEFFF  resident: the game state, the SDK
 *   0xF000-        the IM 2 table, the stack
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include <z80.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "td.h"
#include "duel.h"
#include "front.h"
#include "nextcopper.h"

TDGame game;

/* profiling: raster lines (a line is ~1800 T-states at 28 MHz), counted
   from the frame interrupt's counter and the raster position */
static uint16_t raster(void)
{
    uint8_t f;
    uint16_t r;
    do {
        f = frame_ticks;
        r = ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);
    } while (f != frame_ticks);
    r = r >= 248 ? r - 248 : r + 64;                /* lines since the interrupt (line 248) */
    return (uint16_t)(f & 63) * 312 + r;            /* wraps at 64 frames */
}
uint16_t prof_t, prof[6];
void lap(uint8_t k)
{
    uint16_t t = raster();
    prof[k] += t >= prof_t ? t - prof_t : t + 64 * 312 - prof_t;
    prof_t = t;
}

void map_rules(void)
{
    ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33);
    ZXN_WRITE_MMU2(34); ZXN_WRITE_MMU3(35);
}

/* the renderer's code in bank 18, its variables in page 40 */
static void map_render(void)
{
    ZXN_WRITE_MMU0(36); ZXN_WRITE_MMU1(37);
    ZXN_WRITE_MMU2(40); ZXN_WRITE_MMU3(11);
}

/* the front end (front.c) in bank 19 */
void map_front(void)
{
    ZXN_WRITE_MMU0(38); ZXN_WRITE_MMU1(39);
    ZXN_WRITE_MMU2(10); ZXN_WRITE_MMU3(11);
}

/* the original's 32 pens (0x0RGB, 4 bits each) -> the Next's 9-bit palette */
static const uint16_t pens444[32] = {
    0x000, 0xFFF, 0x122, 0x99A, 0x58A, 0x353, 0x575, 0x887, 0x554, 0xAAB, 0x667, 0xCA6, 0x864,
    0xB83, 0x751, 0x642, 0x6D4, 0x372, 0xE92, 0x952, 0x98B, 0x546, 0x6EF, 0xFF6, 0xF73, 0xE22,
    0xF33, 0xFE4, 0x6F6, 0x5EF, 0xFA3, 0x021
};

static uint16_t pal9[32];

/* port: the view is drawn 96 rows high and shown 192 (render.c): every
   other line the copper moves Layer 2's Y scroll (NextReg 0x17) back a row.
   The sky's pen is transparent, and the copper changes the fallback colour
   (NextReg 0x4A) behind it line by line: the original's copper sky
   (plat_sky: 0x369 at the top to 0xBCD at the horizon). */
#define HORIZON_LINE 88

static uint8_t rgb8(uint16_t c12)
{
    return (uint8_t)((((c12 >> 8) & 15) >> 1) << 5 | (((c12 >> 4) & 15) >> 1) << 2 | (c12 & 15) >> 2);
}

static void copper_list(void)
{
    uint16_t L;
    copper_begin();
    copper_move(0x17, 0);               /* the vertical blank: the top border */
    copper_move(0x4A, 0);
    for (L = 0; L < 192; L += 2) {
        if (L) copper_wait(L - 1, 50);
        copper_move(0x17, (uint8_t)((192 - L / 2) % 192));
        if (L <= HORIZON_LINE) {
            uint16_t a = HORIZON_LINE - L, b = L;
            uint16_t r = (3 * a + 11 * b) / HORIZON_LINE, g = (6 * a + 12 * b) / HORIZON_LINE,
                     bl = (9 * a + 13 * b) / HORIZON_LINE;
            copper_move(0x4A, rgb8(r << 8 | g << 4 | bl));
        }
    }
    copper_wait(191, 50);
    copper_move(0x4A, 0);               /* the bottom border: black */
    copper_move(0x17, 0);
    copper_end();
}

static void palette(void)
{
    uint16_t *p = pal9;
    uint8_t i;
    for (i = 0; i < 32; i++) {
        uint16_t c = pens444[i];
        uint8_t r = (uint8_t)(((c >> 8) & 15) * 7 + 7) / 15, g = (uint8_t)(((c >> 4) & 15) * 7 + 7) / 15,
                b = (uint8_t)((c & 15) * 7 + 7) / 15;
        p[i] = (uint16_t)r << 6 | (uint16_t)g << 3 | b;
    }
    p[PEN_SKY] = 0xE3 << 1;             /* the global transparent colour: the copper's sky behind */
    pal_load(1, p, 32);
}

#ifdef TD_TRACE
/* the original's long autopilot fight (tests/test_cases.c): seed 1234 from
   round 1, 9000 ticks; its state hash is 7CFAB335 on the host and the Amiga */
int main(void)
{
    uint16_t t;
    TDEvent e;
    next_init();
    frame_init();
    map_rules();
    td_new_game(&game, 1234, 1);
    for (t = 0; t < 9000 && !game.over && !game.won; t++) {
        td_tick(&game, td_bot(&game));
        while (td_pop_event(&game, &e)) {}
    }
    zx_log("td: trace round=%d score=%ld kills=%d hp=%d frames=%u hash=%08lx\n", game.round, (long)game.score,
           game.kills, game.tk[0].hp, frame_count, (unsigned long)td_state_hash(&game));
    for (;;) frame_wait();
}
#else
/* the ROM's font, copied into page 41 while the ROM is still at 0x0000 */
#define FONT_PAGE 41

static void on_frame(void)
{
    input_sample();
}

int main(void)
{
    uint8_t ticks, acc = 0, frames = 0;
    uint16_t polys = 0, t50 = 0;
    ZXN_WRITE_MMU3(FONT_PAGE);
    memcpy((void *)0x6000, (const void *)0x3D00, 768);
    ZXN_WRITE_MMU3(11);
    next_init();
    ZXN_WRITE_REG(0x15, 0x13);          /* tilemap over sprites over Layer 2, sprites visible */
    ZXN_WRITE_REG(0x4A, 0);             /* the fallback colour: black */
    z80_outp(0xFE, 0);                  /* the border: black */
    palette();
    pal_load(2, pal9, 32);              /* the sprites (the radar's blips) in the same pens */
    l2_init();
    spr_init();
    copper_list();
    frame_init();
    input_init();
    input_fast();
    input_latch(1);
    frame_hook = on_frame;
    ay_init();
    map_front();
    ZXN_WRITE_MMU3(FONT_PAGE);
    front_ui_init((const uint8_t *)0x6000);    /* (tm_def maps bank 5 into slot 2 only) */
    ZXN_WRITE_MMU3(11);
    front_sprites_init();
    map_render();
    render_init();
    map_front();
    front_init();
    zx_log("td: ready kempston=%u\n", input_kempston);
    for (;;) {
        ticks = frame_wait();
        l2_flip();
        prof_t = raster();
        t50 += ticks;
        if (ticks > 12) ticks = 12;     /* (the rules keep the original's speed down to ~4 fps) */
        acc += ticks;
        while (ticks--) front_sound();
        map_front();
        front_input();
        map_rules();
        while (acc >= 2) {               /* the original's 25 ticks a second */
            acc -= 2;
            front_tick();
        }
        lap(0);
        if (!du.help && !front_entering) {
            map_render();
            render_scene();
            polys += render_polys;
        }
        map_front();
        front_screen();
        front_radar();
        frames++;
        if (t50 >= 100) {
            uint8_t v[3], k;
            for (k = 0; k < 3; k++) { z80_outp(0xFFFD, 8 + k); v[k] = z80_inp(0xFFFD) & 15; }
            zx_log("td: fps=%u polys=%u tick=%ld ay=%u,%u,%u lines: logic %u grid %u collect %u items %u\n", frames / 2,
                   polys / frames, (long)game.tick, v[0], v[1], v[2], prof[0] / frames, prof[1] / frames,
                   prof[2] / frames, prof[3] / frames);
            prof[0] = prof[1] = prof[2] = prof[3] = 0;
            frames = 0;
            polys = 0;
            t50 -= 100;
        }
    }
}
#endif
