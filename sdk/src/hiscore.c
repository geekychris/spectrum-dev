/* High-score table (see zxgame.h). */
#include <stdint.h>
#include <string.h>
#include "zxgame.h"
#include "zxtext.h"
#include <arch/zx.h>
#include "zxlog.h"

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
        zx_print_at(col, row + i, line);
        zx_print_at(col + 2, row + i, hs_table[i].name);
        text_num(col + 7, row + i, hs_table[i].score, 6);
    }
}

void hs_enter(int8_t place, uint32_t score, const char *game)
{
    char name[4] = "AAA";
    uint8_t pos = 0, held = 1;
    if (place < 0) return;
    screen_clear(INK_WHITE | BRIGHT, INK_BLACK);
    attr_fill(0, 3, 32, 2, INK_YELLOW | BRIGHT);
    text_big(5, 3, "NEW HI SCORE");
    text_num(13, 7, score, 6);
    text_center(10, "TYPE YOUR INITIALS");
    text_center(11, "(OR UP/DOWN + FIRE)");
    zx_log("%s: hiscore rank=%d\n", game, place);
    input_read();
    while (pos < 3) {
        char k;
        uint8_t p;
        frame_wait();
        attr_fill(14, 14, 3, 1, INK_WHITE | BRIGHT);
        attr_set(14 + pos, 14, INK_BLACK | PAPER_YELLOW | BRIGHT);
        zx_print_at(14, 14, name);
        input_read();
        p = input_pressed();
        k = input_letter();
        if (k && !held) {
            name[pos++] = k;
            held = 1;
            beep(40, 8);
            continue;
        }
        if (!k) held = 0;
        if (p & IN_UP) name[pos] = name[pos] == 'Z' ? 'A' : name[pos] + 1;
        if (p & IN_DOWN) name[pos] = name[pos] == 'A' ? 'Z' : name[pos] - 1;
        if (p & (IN_FIRE | IN_START)) { pos++; beep(40, 8); }
    }
    hs_insert(place, name, score);
    zx_log("%s: name=%s\n", game, name);
}
