/* The original's get_obj_color and tunables (game.c), in main memory: the
   rules and the renderer, each in a bank of its own, use them. */
#include "game.h"

/* Default tunables */
Tunables g_tune = {
    60,     /* enemy_speed (base * 10) */
    50,     /* enemy_accuracy (1-100) */
    60,     /* enemy_fire_rate (frames between shots) */
    100,    /* spawn_rate (frames between spawns) */
    1,      /* difficulty */
    3,      /* start_lives */
    10000,  /* extra_life_score */
    80,     /* player_speed (base * 10) */
    1,      /* display_mode: color by default */
    0,      /* god_mode */
    0,      /* invert_pitch */
    1       /* num_players */
};


/* Get color for object type based on display mode and depth */
WORD get_obj_color(WORD obj_type, LONG depth)
{
    if (g_tune.display_mode == DISPLAY_CLASSIC) {
        /* Classic green-on-black with depth shading */
        if (depth < 200)  return 2;   /* bright green (near) */
        if (depth < 800)  return 4;   /* green (medium) */
        if (depth < 1500) return 5;   /* dark green (far) */
        return 13;                     /* dim green (very far) */
    }

    /* Color mode */
    switch (obj_type) {
        case OBJ_ENEMY:    return (depth < 500) ? 8 : 11;   /* red / dark red */
        case OBJ_BLIMP:    return 3;   /* cyan */
        case OBJ_GROUND:   return 6;   /* brown/tan */
        case OBJ_GRID:     return 5;   /* dark green */
        case OBJ_BULLET_P: return 1;   /* white */
        case OBJ_BULLET_E: return 8;   /* red */
        case OBJ_HUD:      return 1;   /* white */
        default:           return 7;   /* grey */
    }
}

