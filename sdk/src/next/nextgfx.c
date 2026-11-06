/* Spectrum Next graphics (see nextgfx.h). Only built for TARGET := zxn. */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"

#define SLOT_ADDR ((uint8_t *)0x6000)    /* MMU slot 3 */
#define BANK_A 9
#define BANK_B 12

uint8_t l2_back = BANK_B;
static uint8_t l2_front = BANK_A;
static uint8_t slot_saved;

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

/* the 8K page holding back-buffer line y, mapped at 0x6000; returns the line */
static uint8_t *line_ptr(uint8_t y)
{
    ZXN_WRITE_REG(0x53, l2_back * 2 + (y >> 5));
    return SLOT_ADDR + ((uint16_t)(y & 31) << 8);
}

static void map_begin(void) { slot_saved = ZXN_READ_REG(0x53); }
static void map_end(void) { ZXN_WRITE_REG(0x53, slot_saved); }

static void clear_bank(uint8_t bank, uint8_t colour)
{
    uint8_t p;
    map_begin();
    for (p = 0; p < 6; p++) {
        ZXN_WRITE_REG(0x53, bank * 2 + p);
        memset(SLOT_ADDR, colour, 8192);
    }
    map_end();
}

void l2_init(void)
{
    clear_bank(BANK_A, 0);
    clear_bank(BANK_B, 0);
    l2_front = BANK_A;
    l2_back = BANK_B;
    ZXN_WRITE_REG(0x12, l2_front);
    z80_outp(0x123B, 0x02);              /* Layer 2 visible */
}

void l2_flip(void)
{
    uint8_t t = l2_front;
    l2_front = l2_back;
    l2_back = t;
    ZXN_WRITE_REG(0x12, l2_front);
}

void l2_copy_front_to_back(void)
{
    static uint8_t buf[256];
    uint8_t y = 0;
    map_begin();
    do {
        ZXN_WRITE_REG(0x53, l2_front * 2 + (y >> 5));
        memcpy(buf, SLOT_ADDR + ((uint16_t)(y & 31) << 8), 256);
        memcpy(line_ptr(y), buf, 256);
    } while (++y < 192);
    map_end();
}

void l2_clear(uint8_t colour)
{
    clear_bank(l2_back, colour);
}

void l2_plot(uint8_t x, uint8_t y, uint8_t colour)
{
    if (y >= 192) return;
    map_begin();
    line_ptr(y)[x] = colour;
    map_end();
}

void l2_fill(uint8_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t colour)
{
    if (x + w > 256) w = 256 - x;
    map_begin();
    while (h-- && y < 192) memset(line_ptr(y++) + x, colour, w);
    map_end();
}

void l2_hline(uint8_t x, uint8_t y, uint16_t w, uint8_t colour) { l2_fill(x, y, w, 1, colour); }

void l2_rect(uint8_t x, uint8_t y, uint16_t w, uint8_t h, uint8_t colour)
{
    l2_fill(x, y, w, 1, colour);
    l2_fill(x, y + h - 1, w, 1, colour);
    l2_fill(x, y, 1, h, colour);
    l2_fill(x + w - 1, y, 1, h, colour);
}

void l2_blit(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *src, uint16_t key)
{
    uint8_t n = (uint16_t)x + w > 256 ? 256 - x : w, i;
    map_begin();
    while (h-- && y < 192) {
        uint8_t *d = line_ptr(y++) + x;
        if (key > 0xFF) memcpy(d, src, n);
        else for (i = 0; i < n; i++) if (src[i] != (uint8_t)key) d[i] = src[i];
        src += w;
    }
    map_end();
}

void l2_text(uint8_t x, uint8_t y, const char *s, uint8_t colour, uint8_t scale)
{
    map_begin();
    for (; *s; s++, x += 8 * scale) {
        const uint8_t *g = (const uint8_t *)0x3C00 + (uint16_t)(uint8_t)*s * 8;
        uint8_t r, b, sy, sx;
        for (r = 0; r < 8; r++)
            for (sy = 0; sy < scale; sy++) {
                uint8_t yy = y + r * scale + sy, *d;
                if (yy >= 192) continue;
                d = line_ptr(yy);
                for (b = 0; b < 8; b++)
                    if (g[r] & (0x80 >> b))
                        for (sx = 0; sx < scale; sx++) {
                            uint16_t xx = (uint16_t)x + b * scale + sx;
                            if (xx < 256) d[xx] = colour;
                        }
            }
    }
    map_end();
}

void l2_num(uint8_t x, uint8_t y, uint32_t v, uint8_t digits, uint8_t colour, uint8_t scale)
{
    char buf[11];
    uint8_t i = digits > 10 ? 10 : digits;
    buf[i] = 0;
    while (i--) { buf[i] = '0' + (uint8_t)(v % 10); v /= 10; }
    l2_text(x, y, buf, colour, scale);
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

void ay_chip(uint8_t n)
{
    z80_outp(0xFFFD, 0xFF - (n % 3));                   /* 0xFF, 0xFE, 0xFD */
}
