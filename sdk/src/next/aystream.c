/* TurboSound register-stream player (see aystream.h). */
#include <stdint.h>
#include <z80.h>
#include <arch/zxn.h>
#include "aystream.h"

#define WIN 0x4000u                 /* MMU slot 2 */

typedef struct {
    uint8_t on, loop, wait, atten;
    uint8_t page, start_page, loop_page;
    uint16_t addr, start_addr, loop_addr;
    uint8_t vol[3];                 /* last volumes written (registers 8..10) */
} Stream;

static Stream st[3];

static void chip(uint8_t n) { z80_outp(0xFFFD, 0xFF - n); }

static void reg(uint8_t r, uint8_t v)
{
    z80_outp(0xFFFD, r);
    z80_outp(0xBFFD, v);
}

static void silence(uint8_t n)
{
    chip(n);
    reg(7, 0x3F);
    reg(8, 0);
    reg(9, 0);
    reg(10, 0);
}

void as_init(void)
{
    uint8_t n;
    for (n = 0; n < 3; n++) {
        st[n].on = 0;
        st[n].atten = 0;
        silence(n);
    }
    chip(0);
}

/* the stream's next byte, through slot 2 (the caller maps and restores) */
static uint8_t next(Stream *s)
{
    uint8_t v;
    ZXN_WRITE_REG(0x52, s->page);
    v = *(volatile uint8_t *)s->addr;
    if (++s->addr == WIN + 0x2000u) {
        s->addr = WIN;
        s->page++;
    }
    return v;
}

/* the position `off` bytes after the start */
static void seek(Stream *s, uint16_t off, uint8_t *page, uint16_t *addr)
{
    uint16_t a = s->start_addr - WIN + off;    /* < 64K: streams are at most ~56K */
    *page = s->start_page + (uint8_t)(a >> 13);
    *addr = WIN + (a & 0x1FFF);
}

void as_play(uint8_t c, uint8_t page, uint16_t addr, uint8_t loop)
{
    Stream *s = &st[c % 3];
    uint8_t saved = ZXN_READ_REG(0x52);
    uint16_t lp;
    s->on = 0;
    s->start_page = s->page = page;
    s->start_addr = s->addr = addr;
    lp = next(s);
    lp |= (uint16_t)next(s) << 8;
    s->loop = loop && lp != 0xFFFF;
    if (s->loop) seek(s, lp, &s->loop_page, &s->loop_addr);
    s->wait = 0;
    s->vol[0] = s->vol[1] = s->vol[2] = 0;
    ZXN_WRITE_REG(0x52, saved);
    s->on = 1;
}

void as_stop(uint8_t c)
{
    st[c % 3].on = 0;
    silence(c % 3);
}

uint8_t as_busy(uint8_t c) { return st[c % 3].on; }

static uint8_t vol_of(Stream *s, uint8_t v)
{
    if (v & 0x10) return v;                    /* envelope */
    return v > s->atten ? v - s->atten : 0;
}

void as_atten(uint8_t c, uint8_t n)
{
    Stream *s = &st[c % 3];
    uint8_t i;
    s->atten = n;
    if (!s->on) return;
    chip(c);
    for (i = 0; i < 3; i++) reg(8 + i, vol_of(s, s->vol[i]));
}

void as_tick(void)
{
    uint8_t saved = ZXN_READ_REG(0x52), c, r, m0, m1, v;
    for (c = 0; c < 3; c++) {
        Stream *s = &st[c];
        if (!s->on) continue;
        if (s->wait) { s->wait--; continue; }
        chip(c);
        m0 = next(s);
        m1 = next(s);
        if (m1 & 0x80) {                       /* end */
            if (s->loop) {
                s->page = s->loop_page;
                s->addr = s->loop_addr;
            } else {
                s->on = 0;
                silence(c);
            }
            continue;
        }
        for (r = 0; r < 8; r++, m0 >>= 1)
            if (m0 & 1) reg(r, next(s));
        for (r = 8; r < 14; r++, m1 >>= 1)
            if (m1 & 1) {
                v = next(s);
                if (r <= 10) {
                    s->vol[r - 8] = v;
                    v = vol_of(s, v);
                }
                reg(r, v);
            }
        if (m1 & 1) s->wait = next(s);         /* bit 6, shifted down to bit 0 */
    }
    chip(0);
    ZXN_WRITE_REG(0x52, saved);
}
