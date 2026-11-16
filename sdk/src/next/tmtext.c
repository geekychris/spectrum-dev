/* Tilemap text slots and big letters (see tmtext.h). */
#include <stdint.h>
#include <string.h>
#include "nexttile.h"
#include "tmtext.h"

#define POOL_MAX 40

static tmt_slot *slot;
static uint8_t nslots, font_tile, pool_first;
static const uint8_t *font;
static uint8_t pool_ch[POOL_MAX], pool_sc[POOL_MAX], pool_sh[POOL_MAX], pool_tile[POOL_MAX], pool_n;
static uint16_t pool_next;

void tmt_init(tmt_slot *slots, uint8_t n, const uint8_t *f, uint8_t ft, uint8_t pool)
{
    slot = slots;
    nslots = n;
    font = f;
    font_tile = ft;
    pool_first = pool;
    tmt_clear();
}

void tmt_clear(void)
{
    tm_clear();
    memset(slot, 0, sizeof(tmt_slot) * nslots);
    pool_n = 0;
    pool_next = pool_first;
}

/* the first of a big letter's sc x sc tiles; 0 if the pool is full */
static uint8_t big_tile(uint8_t ch, uint8_t sc, uint8_t sh)
{
    uint8_t k, t;
    for (k = 0; k < pool_n; k++)
        if (pool_ch[k] == ch && pool_sc[k] == sc && pool_sh[k] == sh) return pool_tile[k];
    if (pool_n == POOL_MAX || pool_next + sc * sc > 256 || ch < 32 || ch > 127) return 0;
    t = (uint8_t)pool_next;
    tm_glyphnx(t, font + ((uint16_t)(ch - 32) << 3), sc, 1, sh ? 2 : 0, 2);
    pool_ch[pool_n] = ch;
    pool_sc[pool_n] = sc;
    pool_sh[pool_n] = sh;
    pool_tile[pool_n++] = t;
    pool_next += sc * sc;
    return t;
}

static void draw(const tmt_slot *p, uint8_t erase)
{
    uint8_t k, col = p->col, w = p->sc ? p->sc : 1;
    if (erase) {
        tm_fill(col, p->row, (uint8_t)(p->len * w), w, 0, 0);
        return;
    }
    if (w == 1) {
        tm_print(col, p->row, p->s, font_tile, p->pal);
        return;
    }
    for (k = 0; k < p->len; k++, col += w) {
        uint8_t t, r, c;
        if (p->s[k] == ' ') continue;
        t = big_tile((uint8_t)p->s[k], w, p->sh);
        if (!t) continue;
        for (r = 0; r < w; r++)
            for (c = 0; c < w; c++) tm_put(col + c, p->row + r, t + r * w + c, p->pal);
    }
}

void tmt_say(uint8_t id, uint8_t col, uint8_t row, const char *s, uint8_t pal, uint8_t sc, uint8_t sh)
{
    tmt_slot *p = &slot[id];
    uint8_t len = (uint8_t)strlen(s), w = sc ? sc : 1;
    if (len > TMT_LEN) len = TMT_LEN;
    if (col == TMT_CENTRE) col = (uint8_t)((40 - len * w) / 2);
    if (p->col == col && p->row == row && p->pal == pal && p->sc == sc && p->sh == sh && p->len == len &&
        !strncmp(p->s, s, len)) return;
    if (p->len) draw(p, 1);
    memcpy(p->s, s, len);
    p->s[len] = 0;
    p->col = col; p->row = row; p->len = len; p->sc = sc; p->pal = pal; p->sh = sh;
    if (len) draw(p, 0);
}

void tmt_quiet(uint8_t id) { tmt_say(id, 0, 0, "", 0, 1, 0); }

char *tmt_num(char *d, int32_t v)
{
    char t[11];
    uint8_t n = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0) *d++ = '-';
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}
