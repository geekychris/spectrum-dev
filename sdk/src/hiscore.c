/* High-score table (see zxgame.h). */
#include <stdint.h>
#include <string.h>
#include "zxgame.h"

hs_entry hs_table[HS_ENTRIES];

void hs_init(uint32_t base)
{
    static const char *names[HS_ENTRIES] = {"ZXD", "SPC", "NXT", "ACE", "BOB"};
    uint8_t i;
    for (i = 0; i < HS_ENTRIES; i++) {
        memcpy(hs_table[i].name, names[i], 4);
        hs_table[i].score = base * (HS_ENTRIES - i);
    }
}

int8_t hs_rank(uint32_t score)
{
    int8_t i;
    for (i = 0; i < HS_ENTRIES; i++)
        if (score > hs_table[i].score) return i;
    return -1;
}

void hs_insert(int8_t place, const char *name, uint32_t score)
{
    int8_t i;
    if (place < 0 || place >= HS_ENTRIES) return;
    for (i = HS_ENTRIES - 1; i > place; i--) hs_table[i] = hs_table[i - 1];
    memcpy(hs_table[place].name, name, 3);
    hs_table[place].name[3] = 0;
    hs_table[place].score = score;
}
