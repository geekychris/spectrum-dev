/*
 * engine3d.h - the original's engine3d.h types and constants (Ace Pilot).
 * port: only what the rules use is here (the trig tables, Vec3, Camera,
 * FOV and clip planes); the Next's renderer is render.c.
 */
#ifndef ENGINE3D_H
#define ENGINE3D_H

#include "types.h"

/* Fixed-point 16.16 */
typedef LONG Fixed;

/* Angle: 0..255 -> 0..360 degrees */
#define ANGLE_COUNT 256

extern const Fixed sin_tab[ANGLE_COUNT];
extern const Fixed cos_tab[ANGLE_COUNT];

/* 3D vector (integer world coords) */
typedef struct { LONG x, y, z; } Vec3;

/* Camera */
typedef struct {
    Vec3 pos;
    WORD pitch;  /* 0-255 */
    WORD yaw;    /* 0-255 */
} Camera;

#define FOV_SCALE  200
#define NEAR_CLIP  20
#define FAR_CLIP   4000

#endif
