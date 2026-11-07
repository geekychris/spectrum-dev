/* Big text from hardware sprites (see sprtext.h). */
#include <stdint.h>
#include <string.h>
#include "nextgfx.h"
#include "nextspr.h"
#include "font5.h"
#include "sprtext.h"

static uint8_t slot_of[FONT5_CHARS];     /* pattern of each font character, 0xFF: none */

void sprtext_init(uint8_t first, const char *chars)
{
    static uint8_t pat[256];
    uint8_t r, c;
    memset(slot_of, 0xFF, sizeof(slot_of));
    for (; *chars; chars++) {
        const uint8_t *g = font5_glyph(*chars);
        if (!g) continue;
        memset(pat, SPRG_TRANSPARENT, sizeof(pat));
        for (r = 0; r < FONT5_H; r++)
            for (c = 0; c < FONT5_W; c++)
                if (g[r] & (0x80 >> c)) {
                    uint8_t *p = &pat[(r + 1) * 16 + c + 1];
                    if (*p != 1) *p = 2;            /* the shadow, unless glyph */
                    pat[r * 16 + c] = 1;
                }
        slot_of[*chars - FONT5_FIRST] = first;
        spr_pattern(first++, pat);
    }
}

void sprtext_colour(uint8_t pal, uint16_t rgb9)
{
    pal_set(2, (pal << 4) | 1, rgb9);
    pal_set(2, (pal << 4) | 2, 0);
}

uint8_t sprtext_show(uint8_t first, int16_t x, int16_t y, const char *s, uint8_t pal, uint8_t zoom)
{
    for (; *s; s++, x += 6 << zoom) {
        uint8_t ch = (uint8_t)*s;
        if (ch < FONT5_FIRST || ch > FONT5_LAST || slot_of[ch - FONT5_FIRST] == 0xFF) continue;
        first = sprg_show_ex(first, x, y, 1, 1, slot_of[ch - FONT5_FIRST], 0, pal, zoom);
    }
    return first;
}

uint16_t sprtext_width(const char *s, uint8_t zoom)
{
    return font5_width(s, 1 << zoom);
}
