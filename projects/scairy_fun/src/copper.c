// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * copper.c - the copper list: the 3D view's line doubling and the sky's
 * gradient. In page 42, mapped at 0x6000 while main.c calls render_copper()
 * (with the renderer's bank mapped: route_sky is there).
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "sc.h"
#include "render.h"

/* ---------- the copper ---------- */
static void cop(uint8_t hi, uint8_t lo)
{
    ZXN_WRITE_REG(0x60, hi);
    ZXN_WRITE_REG(0x60, lo);
}

static void cop_wait(uint16_t line, uint8_t h)
{
    cop(0x80 | h << 1 | (uint8_t)(line >> 8), (uint8_t)line);
}

static uint8_t rgb8(uint16_t c12)        /* 0xRGB -> RRRGGGBB */
{
    return (uint8_t)((((c12 >> 8) & 15) >> 1) << 5 | (((c12 >> 4) & 15) >> 1) << 2 | (c12 & 15) >> 2);
}

/* port: the 3D view is drawn 80 rows high (Layer 2 rows 0..79) and shown 160:
   every other line the copper moves Layer 2's Y scroll (NextReg 0x17) back a
   row, so each row is shown twice; from line 160 (the HUD) rows are 1:1.
   The sky pen is transparent; behind it the copper changes the fallback
   colour (NextReg 0x4A) line by line: the original's copper sky (plat_sky). */
void render_copper(int route, int view)
{
    uint16_t top = route_sky[route][0], bot = route_sky[route][1], L;
    int16_t hzd = render_horizon(view) * 2;
    ZXN_WRITE_REG(0x62, 0x00);                  /* stop, index 0 */
    ZXN_WRITE_REG(0x61, 0x00);
    cop(0x17, 0);                               /* (the copper restarts in the vertical blank) */
    for (L = 0; L < VIEW_H; L += 2) {
        if (L) cop_wait(L - 1, 50);             /* the end of the line before */
        cop(0x17, (uint8_t)((192 - L / 2) % 192));
        if ((int16_t)L <= hzd) {
            uint16_t r = ((top >> 8) & 15) * (hzd - L) + ((bot >> 8) & 15) * L,
                     g = ((top >> 4) & 15) * (hzd - L) + ((bot >> 4) & 15) * L,
                     b = (top & 15) * (hzd - L) + (bot & 15) * L;
            cop(0x4A, rgb8((r / hzd) << 8 | (g / hzd) << 4 | (b / hzd)));
        }
    }
    cop_wait(VIEW_H - 1, 50);
    cop(0x17, 0);
    cop(0xFF, 0xFF);                            /* wait for a line that never comes */
    ZXN_WRITE_REG(0x62, 0xC0);                  /* run, from the top every frame */
}

