/*
 * a3cam.h - the amiga3d engine's camera, angles and hash (a3d.h,
 * a3d_math.c) bit for bit, so an amiga3d game's rules that project through
 * its camera (what the sight is on, what is in view) give the original's
 * results on the Z80. On top of fix16.h (a3_sin, a3_cos).
 *
 *   a3_atan_ratio(num, den)   angle of (num, den), den > 0, |num| <= den
 *   a3_atan2(x, y)            angle of (x, y) from +y towards +x, 0..65535
 *   a3_hash(x)                the engine's stateless scramble
 *   A3Cam                     x, y, z (whole units), yaw, pitch (16-bit
 *                             angles); cam_view sets the projection's centre
 *                             and scale, cam_setup caches the trig
 *   cam_xform(c, wx, wy, wz, out)   world -> camera space
 *   cam_project(c, p, &sx, &sy)     camera space -> screen (p->z >= A3_NEAR_Z)
 *   cam_horizon(c)                  the horizon's screen row
 *
 * Not ported: cam_place / cam_for (16.16 cameras): A3Cam is the whole-unit
 * camera (CAM_U 1). The engine's int is 32 bits: here every value that can
 * pass 16 bits is int32_t. (Plain C: z88dk's 32-bit helpers already use the
 * Z80N's MUL; an asm 16-bit path measured slower.) Tests: projects/geomtest
 * ("a3cam") and projects/canyon_rail's tools/check_trace.sh (whole games
 * against the original's).
 */
#ifndef ZX_A3CAM_H
#define ZX_A3CAM_H
#include <stdint.h>
#include "fix16.h"

#define A3_NEAR_Z 12

typedef struct { int32_t x, y, z; } A3V3;

typedef struct {
    int32_t x, y, z;                    /* world units */
    uint16_t yaw, pitch;
    int32_t cy, sy, cp, sp;             /* cos / sin of yaw and pitch, 16.16 (cam_setup) */
    int16_t cx, cyc, fov;               /* projection centre and scale (cam_view) */
    int16_t vw, vh;
} A3Cam;

int16_t a3_atan_ratio(int32_t num, int32_t den);
uint16_t a3_atan2(int32_t x, int32_t y);
uint32_t a3_hash(uint32_t x);

void cam_view(A3Cam *c, int16_t vw, int16_t vh, int16_t cx, int16_t cy, int16_t fov);
void cam_setup(A3Cam *c);
void cam_xform(const A3Cam *c, int32_t wx, int32_t wy, int32_t wz, A3V3 *out);
void cam_project(const A3Cam *c, const A3V3 *p, int32_t *sx, int32_t *sy);
int16_t cam_horizon(const A3Cam *c);

#endif
