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

/* the initials editor: typed letters (latched, so a quick tap during a slow
   frame isn't lost), or UP/DOWN to change the letter and FIRE/ENTER to take it */
void hs_edit_begin(hs_editor *e)
{
    memcpy(e->name, "AAA", 4);
    e->pos = 0;
    input_read();                       /* forget what was pressed before */
}

uint8_t hs_edit_step(hs_editor *e)
{
    char k = input_letter_pressed();
    uint8_t p = input_pressed(), r = 0;
    char *c = &e->name[e->pos];
    if (e->pos >= 3) return HS_EDIT_DONE;
    if (k) {                            /* (a letter key may also be a direction: the letter wins) */
        *c = k;
        e->pos++;
        r = HS_EDIT_CHANGED | HS_EDIT_TAKEN;
    }
    else {
        if (p & IN_UP) { *c = *c == 'Z' ? 'A' : *c + 1; r = HS_EDIT_CHANGED; }
        if (p & IN_DOWN) { *c = *c == 'A' ? 'Z' : *c - 1; r = HS_EDIT_CHANGED; }
        if (p & (IN_FIRE | IN_START)) { e->pos++; r = HS_EDIT_CHANGED | HS_EDIT_TAKEN; }
    }
    if (e->pos >= 3) r |= HS_EDIT_DONE;
    return r;
}
