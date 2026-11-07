// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * copper.c - the palettes (set_palette) and the copper list: the 3D view's
 * line doubling and the sky's
 * gradient. In page 42, mapped at 0x6000 while main.c calls render_copper()
 * (with the renderer's bank mapped: route_sky is there).
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "sc.h"
#include "render.h"
#include "nextgfx.h"
#include "game.h"

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

static uint16_t rgb9(uint16_t c12)             /* 0xRGB (4 bits each) -> RGB333 */
{
    uint16_t r = (((c12 >> 8) & 15) * 7 + 7) / 15, g = (((c12 >> 4) & 15) * 7 + 7) / 15,
             b = ((c12 & 15) * 7 + 7) / 15;
    return r << 6 | g << 3 | b;
}

static uint16_t mix12(uint16_t a, uint16_t b, uint16_t num, uint16_t den)
{
    uint16_t r = ((a >> 8) & 15) * (den - num) + ((b >> 8) & 15) * num,
             g = ((a >> 4) & 15) * (den - num) + ((b >> 4) & 15) * num,
             c = (a & 15) * (den - num) + (b & 15) * num;
    return (r / den) << 8 | (g / den) << 4 | (c / den);
}

static uint16_t glared(uint16_t c, int glare)
{
    return glare > 0 ? mix12(c, 0xFFE, glare * 10, 16 * 16) : c;
}

/* the sprites' 16-colour layout (tools/gen_sprites.py): body lit, shaded, then
   glass, shadow, white x2, red x2, yellow, orange, brown x2, rock, dirt, light */
static const uint8_t spr_pens[13] = { 28, 31, 24, 25, 16, 17, 18, 19, 29, 30, 15, 13, 16 };
static const uint8_t spr_body[8][2] = {
    { 22, 23 }, { 24, 25 }, { 18, 19 }, { 26, 29 }, { 16, 17 }, { 3, 2 }, { 24, 25 }, { 24, 25 }
};

/* (call with the renderer's bank mapped: route_palette is there)
   Layer 2: the route's 32 pens, and three copies fading into the haze (port:
   256 colours); the sprites: a 16-colour bank for each body colour */
void set_palette(int route, int glare)
{
    const uint16_t *p = route_palette[route];
    uint8_t f, i;
    for (f = 0; f < 4; f++)
        for (i = 0; i < 32; i++) {
            uint16_t c = p[i];
            if (f && i != PEN_SKY) c = mix12(c, p[PEN_HAZE], f * 2, 8);
            if (i >= 2) c = glared(c, glare);
            pal_set(1, f * 32 + i, i == PEN_SKY ? 0xE3 << 1 | 1 : rgb9(c));    /* the sky: see render_copper */
        }
    for (f = 0; f < 8; f++) {
        pal_set(2, f * 16 + 1, rgb9(glared(p[spr_body[f][0]], glare)));
        pal_set(2, f * 16 + 2, rgb9(glared(p[spr_body[f][1]], glare)));
        for (i = 0; i < 13; i++) {
            uint8_t pen = i == 12 && f == 7 ? 22 : spr_pens[i];      /* the ambulance's blue */
            pal_set(2, f * 16 + 3 + i, rgb9(glared(p[pen], glare)));
        }
    }
}


/* the effects (game.c sfx(): queued, started here so only the interrupt writes
   to the AY), on chip 0; the music on chips 1 and 2 */
