/* One tile map with a row table (see tmap.h). Builds on the host too. */
#include <stdint.h>
#include "tmap.h"
#include "zxrle.h"

uint8_t *tmap_cells;
uint8_t tmap_w, tmap_h;
uint8_t *tmap_row[TMAP_MAX_H];

void tmap_init(uint8_t *cells, uint8_t w, uint8_t h)
{
    uint8_t r;
    tmap_cells = cells;
    tmap_w = w;
    tmap_h = h;
    for (r = 0; r < h; r++, cells += w) tmap_row[r] = cells;
}

uint16_t tmap_load(const uint8_t *packed)
{
    return rle_unpack(tmap_cells, packed);
}

uint8_t tmap_at(int16_t c, int16_t r, uint8_t outside)
{
    if (c < 0 || r < 0 || c >= tmap_w || r >= tmap_h) return outside;
    return tmap_row[r][c];
}

uint8_t tmap_find(uint8_t id, uint8_t *col, uint8_t *row)
{
    uint8_t r, c;
    for (r = 0; r < tmap_h; r++) {
        const uint8_t *p = tmap_row[r];
        for (c = 0; c < tmap_w; c++)
            if (p[c] == id) { *col = c; *row = r; return 1; }
    }
    return 0;
}

uint16_t tmap_count(uint8_t id)
{
    uint16_t n = 0, i = (uint16_t)tmap_w * tmap_h;
    const uint8_t *p = tmap_cells;
    while (i--) if (*p++ == id) n++;
    return n;
}

uint16_t tmap_replace(uint8_t from, uint8_t to)
{
    uint16_t n = 0, i = (uint16_t)tmap_w * tmap_h;
    uint8_t *p = tmap_cells;
    for (; i--; p++)
        if (*p == from) { *p = to; n++; }
    return n;
}
