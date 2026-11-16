/*
 * Orbital Patrol - the original main loop's state machine (main.c's switch),
 * one 50 Hz frame a call. The drawing that went with each state is in
 * view.c; the title and the high scores are main.c's.
 */
#include "flow.h"

void flow_start(GameState *gs)
{
    LONG hi = gs->hiscore;
    game_init(gs, 1);
    gs->hiscore = hi;               /* port: main.c sets it from the score table */
    gs->state = STATE_LEVEL_START;
    gs->state_timer = 45;
    game_spawn_wave(gs);
}

void flow_step(GameState *gs, WORD input)
{
    switch (gs->state) {
    case STATE_LEVEL_START:
        game_update(gs, 0); /* no input during level start */
        gs->state_timer--;
        if (gs->state_timer <= 0)
            gs->state = STATE_PLAYING;
        break;

    case STATE_PLAYING:
        game_update(gs, input);
        break;

    case STATE_DYING:
        game_update(gs, 0);
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            if (gs->lives > 0) {
                gs->state = STATE_RESPAWNING;
                gs->state_timer = 60;
                gs->ship.alive = 1;
                gs->ship.wy = TO_FP(PLAY_TOP + (PLAY_BOT - PLAY_TOP) / 2);
                gs->ship.vx = 0;
                gs->ship.vy = 0;
                gs->ship.invuln_timer = 120;
            } else {
                gs->state = STATE_GAMEOVER;
                gs->state_timer = 180;
            }
        }
        break;

    case STATE_RESPAWNING:
        game_update(gs, input);
        gs->state_timer--;
        if (gs->state_timer <= 0)
            gs->state = STATE_PLAYING;
        break;

    case STATE_LEVEL_CLEAR:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->score += gs->humans_alive * SCORE_WAVE_BONUS;
            if (gs->score > gs->hiscore)
                gs->hiscore = gs->score;
            gs->level++;
            gs->state = STATE_LEVEL_START;
            gs->state_timer = 45;
            game_spawn_wave(gs);
        }
        break;

    case STATE_GAMEOVER:
        gs->state_timer--;
        if (gs->state_timer <= 0 || (input & (INPUT_START | INPUT_FIRE)))
            gs->state = STATE_HISCORE_ENTRY;
        break;
    }

    /* Frame counter */
    gs->frame++;
}
