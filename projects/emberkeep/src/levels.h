/* Emberkeep - the chambers (the original's levels.h) */
#ifndef EMBERKEEP_LEVELS_H
#define EMBERKEEP_LEVELS_H

#include <stdint.h>

/*
 * ASCII cavern grid - 20 * 15 chars, row-major:
 *   .  floor    #  wall    R  rune (key)    E  portal (exit)
 *   O  oil flask    ^  hazard    P  player spawn (floor)
 */
typedef struct {
    uint8_t axis;            /* 0 = horizontal, 1 = vertical, 0xFF = terminator */
    uint8_t colour;
    int8_t dx, dy;
    uint16_t x, y;
    uint16_t min_pos, max_pos;
} EnemySpec;

#define NUM_LEVELS 4
extern const char *const level_tiles[NUM_LEVELS];
extern const EnemySpec *const level_enemies[NUM_LEVELS];
extern const char *const level_names[NUM_LEVELS];

#endif
