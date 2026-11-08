/*
 * game_out.c - the original's outcome(): a landing's or a crash's
 * messages, score, ships and sound (game.c calls it when the rules say so).
 * In bank 24 with the text: neither resident memory nor the rules' bank
 * has room.
 */
#include <string.h>
#include <stdio.h>
#include "zxlog.h"
#include "game.h"
#include "game_rules.h"
#include "sound.h"

static const char *const outcome_names[5] = { "flying", "landed", "offpad", "crashed", "lost" };

/* the original's outcome(): messages, score, ships, sound */
void game_outcome(void)
{
    LdResult *r = &level.result;
    lander.state_timer = 100;
    lander.last_score = r->score;
    lander.score += r->score;
    switch (level.outcome) {
    case OUT_LANDED:
        strcpy(lander.msg, r->pad == 5 ? "PERFECT x5" : r->pad == 2 ? "LANDED x2" : "LANDED");
        sprintf(lander.msg2, "SOFT %ld  FUEL %ld", r->soft, r->fuel_bonus);
        snd_play(SFX_LAND);
        break;
    case OUT_OFFPAD:
        strcpy(lander.msg, "OFF THE PAD");
        strcpy(lander.msg2, "SAFE, BUT NOT ON A PAD");
        snd_play(SFX_BEEP);
        break;
    case OUT_CRASHED:
        strcpy(lander.msg, "CRASHED");
        if (!r->vs_ok) sprintf(lander.msg2, "DOWN AT %ld.%ld M/S (LIMIT %ld.%ld)", FINT(r->vs), ((r->vs * 10) >> 16) % 10,
                               FINT(hv.lim_vs), ((hv.lim_vs * 10) >> 16) % 10);
        else if (!r->hs_ok) sprintf(lander.msg2, "SLIDING AT %ld.%ld M/S", FINT(r->hs), ((r->hs * 10) >> 16) % 10);
        else if (!r->tilt_ok) sprintf(lander.msg2, "TILTED %ld DEGREES", FINT(fmul(r->tilt, FX(57.29578))));
        else strcpy(lander.msg2, "THE GROUND WAS TOO STEEP");
        lander.ships--;
        snd_play(SFX_CRASH);
        break;
    default:
        strcpy(lander.msg, "LOST");
        strcpy(lander.msg2, "LEFT THE LANDING AREA");
        lander.ships--;
        snd_play(SFX_CRASH);
        break;
    }
    zx_log("ld: outcome %s score=%ld total=%ld ships=%d vs=%ld hs=%ld pad=%d\n", outcome_names[level.outcome], r->score,
           lander.score, (int)lander.ships, (r->vs * 100) >> 16, (r->hs * 100) >> 16, r->pad);
    if (lander.ships <= 0) { lander.state_timer = 50; set_state(LS_OVER); }
    else set_state(LS_RESULT);
}

