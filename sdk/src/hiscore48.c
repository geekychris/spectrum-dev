/* High-score table on the 48K screen (see zxgame.h); in its own file so that
   games that draw their own (the Next ones) don't link the 48K graphics. */
#include <stdint.h>
#include <string.h>
#include "zxgame.h"
#include "zxtext.h"
#include <arch/zx.h>
#include "zxlog.h"

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
    hs_editor ed;
    uint8_t r = HS_EDIT_CHANGED;
    if (place < 0) return;
    screen_clear(INK_WHITE | BRIGHT, INK_BLACK);
    attr_fill(0, 3, 32, 2, INK_YELLOW | BRIGHT);
    text_big(5, 3, "NEW HI SCORE");
    text_num(13, 7, score, 6);
    text_center(10, "TYPE YOUR INITIALS");
    text_center(11, "(OR UP/DOWN + FIRE)");
    zx_log("%s: hiscore rank=%d\n", game, place);
    hs_edit_begin(&ed);
    while (!(r & HS_EDIT_DONE)) {
        if (r & HS_EDIT_CHANGED) {
            attr_fill(14, 14, 3, 1, INK_WHITE | BRIGHT);
            if (ed.pos < 3) attr_set(14 + ed.pos, 14, INK_BLACK | PAPER_YELLOW | BRIGHT);
            zx_print_at(14, 14, ed.name);
        }
        if (r & HS_EDIT_TAKEN) beep(40, 8);
        frame_wait();
        input_read();
        r = hs_edit_step(&ed);
    }
    hs_insert(place, ed.name, score);
    zx_log("%s: name=%s\n", game, ed.name);
}
