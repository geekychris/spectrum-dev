/* Spectrum Next graphics (see nextgfx.h). Only built for TARGET := zxn. */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"

void next_reg(uint8_t reg, uint8_t val) { ZXN_WRITE_REG(reg, val); }
uint8_t next_reg_read(uint8_t reg) { return ZXN_READ_REG(reg); }

void next_init(void)
{
    /* 28 MHz. Write another speed first: ZEsarUX only applies register 7 when
       it changes, and its .nex loader sets 28 MHz while the PC is in ROM, where
       its default "deny turbo in ROM" caps it at 14 MHz (harmless on a Next) */
    ZXN_WRITE_REG(0x07, 0x02);
    ZXN_WRITE_REG(0x07, 0x03);
    ZXN_WRITE_REG(0x15, 0x01);           /* sprites visible, order S L U */
    ZXN_WRITE_REG(0x14, L2_TRANSPARENT); /* global transparency */
    ZXN_WRITE_REG(0x4B, L2_TRANSPARENT); /* sprite transparency index */
}

void spr_init(void)
{
    uint8_t n;
    ZXN_WRITE_REG(0x15, ZXN_READ_REG(0x15) | 0x03);     /* visible, over the border */
    for (n = 0; n < 128; n++) spr_hide(n);
}

void spr_pattern(uint8_t slot, const uint8_t *pixels256)
{
    uint16_t i;
    z80_outp(0x303B, slot & 63);
    for (i = 0; i < 256; i++) z80_outp(0x5B, pixels256[i]);
}

void spr_show(uint8_t n, int16_t x, int16_t y, uint8_t pattern, uint8_t flags)
{
    z80_outp(0x303B, n & 127);
    z80_outp(0x57, (uint8_t)x);
    z80_outp(0x57, (uint8_t)y);
    z80_outp(0x57, (flags & 0x0E) | ((x >> 8) & 1));
    z80_outp(0x57, 0xC0 | (pattern & 63));              /* visible, 5th byte follows */
    z80_outp(0x57, (y >> 8) & 1);                       /* 8 bpp, no scaling, Y8 */
}

void spr_hide(uint8_t n)
{
    z80_outp(0x303B, n & 127);
    z80_outp(0x57, 0);
    z80_outp(0x57, 0);
    z80_outp(0x57, 0);
    z80_outp(0x57, 0);                                  /* not visible, 4-byte form */
}

/* 32 x 32 from four patterns pat..pat+3 (top left, top right, bottom left,
   bottom right); mirrored: the halves swap and each is mirrored */
void spr_show_quad(uint8_t first, int16_t x, int16_t y, uint8_t pat, uint8_t mirror_x)
{
    uint8_t f = mirror_x ? SPR_MIRROR_X : 0, a = mirror_x ? 1 : 0;
    spr_show(first, x, y, pat + a, f);
    spr_show(first + 1, x + 16, y, pat + (a ^ 1), f);
    spr_show(first + 2, x, y + 16, pat + 2 + a, f);
    spr_show(first + 3, x + 16, y + 16, pat + 2 + (a ^ 1), f);
}

/* patterns from memory pages: 32 to an 8K page, read through MMU slot `slot` */
void spr_patterns_from_pages(uint8_t page, uint8_t slot, uint8_t first, uint8_t count, uint8_t restore)
{
    uint8_t i;
    for (i = 0; i < count; i++) {
        ZXN_WRITE_REG(0x50 + slot, page + (i >> 5));
        spr_pattern(first + i, (const uint8_t *)((uint16_t)slot << 13) + ((uint16_t)(i & 31) << 8));
    }
    ZXN_WRITE_REG(0x50 + slot, restore);
}

static const uint8_t pal_sel[3] = {0x00, 0x10, 0x20};  /* ULA, L2, sprites (first) */

void pal_set8(uint8_t which, uint8_t index, uint8_t rrrgggbb)
{
    ZXN_WRITE_REG(0x43, pal_sel[which % 3]);
    ZXN_WRITE_REG(0x40, index);
    ZXN_WRITE_REG(0x41, rrrgggbb);
}

void pal_set(uint8_t which, uint8_t index, uint16_t rgb333)
{
    ZXN_WRITE_REG(0x43, pal_sel[which % 3]);
    ZXN_WRITE_REG(0x40, index);
    ZXN_WRITE_REG(0x44, (rgb333 >> 1) & 0xFF);          /* RRRGGGBB */
    ZXN_WRITE_REG(0x44, rgb333 & 1);                    /* low blue bit */
}

void pal_load(uint8_t which, const uint16_t *rgb333, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++) pal_set(which, (uint8_t)i, rgb333[i]);
}

void ay_chip(uint8_t n)
{
    z80_outp(0xFFFD, 0xFF - (n % 3));                   /* 0xFF, 0xFE, 0xFD */
}
