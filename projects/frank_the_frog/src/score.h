#ifndef SCORE_H
#define SCORE_H
void score_init(void);
void score_add(int points);
long score_get(void);
long score_high(void);
int  score_lives(void);
void score_lose_life(void);
int  score_level(void);
void score_next_level(void);
#endif
