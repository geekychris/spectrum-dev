/* Bullion Dash - enemy AI (the original's enemy.h) */
#ifndef ENEMY_H
#define ENEMY_H

#include <stdint.h>

void enemy_update_all(void);
uint8_t enemy_check_collision(void);
void enemy_trap_check(int16_t x, int16_t y);
/* the guards' movement rules, for level.c's pathfinding flags */
uint8_t enemy_can_enter(uint8_t t);
uint8_t enemy_has_support(int16_t gx, int16_t gy);

#endif
