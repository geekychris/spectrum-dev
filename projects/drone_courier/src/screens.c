/*
 * screens.c - Drone Courier's help pages (the original's render_help()) and
 * the high-score entry (port: new), in bank 20 beside the rules (game.c's
 * page is full). Both use only resident code (hud.c, the SDK).
 */
#include <string.h>
#include <stdio.h>
#include "zxgame.h"
#include "zxlog.h"
#include "dc.h"
#include "game.h"
#include "hud.h"
#include "sound.h"

#define K_YELLOW TX_YELLOW

static const char *const help_lines[2][12] = {
    { "COLLECT EACH PARCEL FROM THE DEPOT'S",
      "BLUE PAD AND LAND IT ON THE CUSTOMER'S",
      "GREEN PAD. THE ARROW AND THE BEACON",
      "SHOW THE WAY.",
      "",
      "TOUCH DOWN GENTLY ON THE PAD. FRAGILE",
      "PARCELS (RED) BREAK IF YOU LAND HARD",
      "OR KNOCK A TOWER.",
      "",
      "THE BATTERY DRAINS - FASTER CLIMBING -",
      "AND FILLS AT THE DEPOT. WIND FROM DAY 2.",
      "3 FAILED JOBS END THE GAME. 5 DAYS." },
    /* port: the Spectrum's keys */
    { "UP / DOWN       FORWARD / BACK",
      "LEFT / RIGHT    TURN",
      "SPACE / FIRE    CLIMB (LET GO: SINK)",
      "  (Q A O P, CURSORS, JOYSTICKS)",
      "ENTER           PAUSE",
      "B               AUTOPILOT",
      "H               THESE PAGES",
      "BREAK           TITLE",
      "",
      "DELIVERY 200 + 5 A SECOND LEFT,",
      "+150 FRAGILE. A DAY DONE: 500 x DAY.",
      "" },
};

/* render.c render_help() */
void hud_help(uint8_t page)
{
    static uint8_t drawn;
    uint8_t k;
    if (screen == SCR_HELP && drawn == page) return;
    layout(SCR_HELP);
    drawn = page;
    hud_fill_panel();
    hud_on_panel = 1;
    hud_big(BIG_TITLE1, page == 1 ? 72 : 96, 14, page == 1 ? "HOW TO PLAY" : "CONTROLS", TX_YELLOW, 2);
    hud_big(BIG_TITLE2, 0, 0, "", 0, 2);
    for (k = 0; k < 12; k++) hud_text(1, 5 + k * 2, help_lines[page - 1][k], TX_WHITE);
    hud_text(1, 30, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", TX_GREY);
    hud_text(18, 30, "H: NEXT  FIRE: PLAY", K_YELLOW);
    hud_on_panel = 0;
}

/* ------------------------------------------------------------ high scores */

void initials(void)
{
    hs_editor ed;
    uint8_t r = HS_EDIT_CHANGED;
    int8_t place = hs_rank((uint32_t)game.score);
    if (place < 0) return;
    zx_log("dc: hiscore rank=%d\n", (int)place);
    layout(SCR_NONE);
    hud_clear();
    hud_panel(5, 9, 30, 12);
    hud_on_panel = 1;
    hud_big(BIG_BANNER, 160 - 9 * 8, 56, "NEW BEST!", TX_YELLOW, 2);
    hud_ctext(11, "A RECORD DELIVERY RUN", TX_WHITE);
    sprintf(tmp, "SCORE %ld", (long)game.score);
    hud_ctext(13, tmp, TX_WHITE);
    hud_ctext(18, "UP/DOWN LETTER, FIRE NEXT", TX_GREY);
    hs_edit_begin(&ed);
    while (!(r & HS_EDIT_DONE)) {
        if (r & HS_EDIT_CHANGED) {
            char s[8];
            s[0] = ed.name[0]; s[1] = ' '; s[2] = ed.name[1]; s[3] = ' '; s[4] = ed.name[2]; s[5] = 0;
            hud_big(BIG_TITLE1, 160 - 5 * 8, 116, s, TX_GREEN, 2);
            hud_text(15, 17, "     ", TX_WHITE);
            hud_text(16 + ed.pos * 2 > 20 ? 20 : 16 + ed.pos * 2, 17, "^", K_YELLOW);
        }
        if (r & HS_EDIT_TAKEN) snd_play(SFX_PICKUP);
        frame_wait();
        snd_tick();
        input_read();
        r = hs_edit_step(&ed);
    }
    hs_insert(place, ed.name, (uint32_t)game.score);
    hud_on_panel = 0;
    zx_log("dc: name=%s\n", ed.name);
    layout(SCR_NONE);
}

