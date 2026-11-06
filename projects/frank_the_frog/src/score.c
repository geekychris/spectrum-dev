/* Frank the Frog - score, lives, level (from score.c; HUD drawing in render.c) */
#include "game.h"
#include "score.h"

static long current_score = 0;
static long high_score = 0;
static int lives = 3;
static int level = 1;

void score_init(void)
{
    current_score = 0;
    lives = 3;
    level = 1;
}

void score_add(int points)
{
    current_score += points;
    if (current_score > high_score) {
        high_score = current_score;
    }
}

long score_get(void)   { return current_score; }
long score_high(void)  { return high_score; }
int  score_lives(void) { return lives; }
int  score_level(void) { return level; }

void score_lose_life(void)
{
    if (lives > 0) lives--;
}

void score_next_level(void)
{
    level++;
    score_add(1000);
}
