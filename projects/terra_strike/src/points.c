/*
 * Terra Strike - shots, shells and particles as hardware sprites.
 *
 * port: the original draws them as small squares (1-5 pixels, by depth)
 * sorted in with the polygons; here each is a sprite, always in front.
 * Five 8-bit patterns (squares of 1..5 pixels, the rest see-through), each
 * pixel 0: its colour is the sprite palette's entry 16 * k for the sprite's
 * palette offset k, one offset per pen used (main.c loads them).
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include <z80.h>
#include "nextgfx.h"
#include "models.h"
#include "render.h"
#include "points.h"

#define FIRST_PATTERN 32                /* patterns 32..36 */
#define SPR_X0 32                       /* the view's top left in sprite coordinates */
#define SPR_Y0 32

static const uint8_t offset_pens[] = { PEN_YELLOW, PEN_RED, PEN_DKRED, PEN_ORANGE, 4, 5, PEN_WHITE, PEN_CRAFT };
static uint8_t pen_offset[32];
static uint8_t shown;                   /* sprites shown last frame */

RPoint render_points[RENDER_MAX_POINTS];   /* render.c fills it (here: main memory) */
uint8_t render_npoints;

uint8_t points_offset_of_pen(uint8_t pen)
{
    return pen_offset[pen & 31];
}

uint8_t points_pen_of_offset(uint8_t k)
{
    return k < sizeof(offset_pens) ? offset_pens[k] : PEN_WHITE;
}

void points_init(void)
{
    static uint8_t pat[256];
    uint8_t s, y, x;
    for (s = 1; s <= 5; s++) {
        memset(pat, 0xE3, sizeof(pat));
        for (y = 0; y < s; y++)
            for (x = 0; x < s; x++) pat[y * 16 + x] = 0;
        spr_pattern(FIRST_PATTERN + s - 1, pat);
    }
    memset(pat, 0xE3, sizeof(pat));                     /* 37: the radar's craft, 1 x 4 */
    for (y = 0; y < 4; y++) pat[y * 16] = 0;
    spr_pattern(FIRST_PATTERN + 5, pat);
    memset(pen_offset, 6, sizeof(pen_offset));
    for (s = 0; s < sizeof(offset_pens); s++) pen_offset[offset_pens[s]] = s;
}

void points_show(void)
{
    const RPoint *p = render_points;
    uint8_t i;
    for (i = 0; i < render_npoints; i++, p++) {
        uint8_t w = p->size;                    /* the original's size in pixels, at 0.8 */
        int16_t x = SPR_X0 + p->x - (w >> 1);
        int16_t y = SPR_Y0 + 2 * p->y - (w >> 1);
        if (w > 4) w = 4;
        z80_outp(0x303B, i);
        z80_outp(0x57, (uint8_t)x);
        z80_outp(0x57, (uint8_t)y);
        z80_outp(0x57, (uint8_t)(pen_offset[p->col & 31] << 4) | ((x >> 8) & 1));
        z80_outp(0x57, 0xC0 | (FIRST_PATTERN + w - 1));
        z80_outp(0x57, (y >> 8) & 1);
    }
    for (; i < shown; i++) spr_hide(i);
    shown = render_npoints;
}
