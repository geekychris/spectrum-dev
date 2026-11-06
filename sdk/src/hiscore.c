/* High-score table (see zxgame.h). */
#include <stdint.h>
#include <string.h>
#include "zxgame.h"
#include "zxtext.h"

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

void hs_draw(uint8_t col, uint8_t row)
{
    uint8_t i;
    char line[2] = {0, 0};
    for (i = 0; i < HS_ENTRIES; i++) {
        line[0] = '1' + i;
        zx_print_at(col, row + i * 2, line);
        zx_print_at(col + 2, row + i * 2, hs_table[i].name);
        text_num(col + 7, row + i * 2, hs_table[i].score, 6);
    }
}
