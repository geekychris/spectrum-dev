/*
 * vcam.c - the view's camera (the original's render.c camera()), worked out
 * while the rules' bank is in (it reads the ground's height), and the
 * worlds' palettes. Resident.
 *
 * port: the camera's easing towards its distance runs once a logic tick
 * (the original: once a frame, at 25 frames a second), so it moves as the
 * original's whatever the frame rate.
 */
#include "fix16.h"
#include "ld.h"
#include "view.h"
#include "pens.h"

#define U(f) ((int32_t)((f) >> 12))

/* render.c palettes[] */
const uint16_t ld_palettes[3][32] = {
    { 0x000, 0xFFF, 0x223, 0x99A, 0x000, 0xCCC, 0xAAA, 0x999, 0x777, 0x555, 0x333,
      0x3E5, 0xFE3, 0xE3E, 0x445, 0x111, 0xEB3, 0x973, 0xCCD, 0x889, 0x444, 0xFF8, 0xF82,
      0xF33, 0xFB2, 0x4E4, 0x3DF, 0x888, 0x99A, 0xBBB, 0x334, 0x111 },
    { 0x000, 0xFFF, 0x322, 0xA99, 0xC85, 0xD96, 0xC75, 0xA64, 0x853, 0x642, 0x431,
      0x3E5, 0xFE3, 0xE3E, 0x544, 0x211, 0xEB3, 0x973, 0xCCD, 0x889, 0x444, 0xFF8, 0xF82,
      0xF33, 0xFB2, 0x4E4, 0x3DF, 0xA88, 0xA99, 0xC97, 0xB86, 0x111 },
    { 0x000, 0xFFF, 0x223, 0x99A, 0x000, 0xA98, 0x987, 0x876, 0x765, 0x543, 0x332,
      0x3E5, 0xFE3, 0xE3E, 0x444, 0x111, 0xEB3, 0x973, 0xCCD, 0x889, 0x444, 0xFF8, 0xF82,
      0xF33, 0xFB2, 0x4E4, 0x3DF, 0x888, 0x99A, 0xA98, 0x333, 0x111 },
};

ViewCam vcam;                            /* (resident: the view's bank isn't in when it is made) */
uint16_t view_polys;
uint8_t view_compass;
HudVals hv;
static int32_t cam_back = 480, cam_up = 160;

void view_reset(void) { cam_back = 480; cam_up = 160; }

void view_tick(const Level *L)
{
    fix agl = ld_agl(L);
    /* closer in as the ground comes up */
    int32_t back = agl > FROMI(40) ? 480 : 224 + (int32_t)FINT(agl) * 6;
    int32_t up = agl > FROMI(40) ? 160 : 72 + (int32_t)FINT(agl) * 2;
    if (L->outcome) { back = 300; up = 110; }
    cam_back += (back - cam_back) / 8;
    cam_up += (up - cam_up) / 8;
}

void view_camera(const Level *L)
{
    int32_t g;
    vcam.world = (uint8_t)L->world;
    vcam.x = U(L->c.p[0]);
    vcam.y = U(L->c.p[1]) + cam_up;
    vcam.z = U(L->c.p[2]) - cam_back;
    /* don't go into a hill */
    g = U(ld_height(&L->T, (fix)vcam.x << 12, (fix)vcam.z << 12)) + 40;
    if (vcam.y < g) vcam.y = g;
    vcam.pitch = a3_atan2(vcam.y - U(L->c.p[1]) + 40, cam_back);     /* looking down at the craft */
    if (vcam.pitch < 600) vcam.pitch = 600;
    vcam.cp = (int16_t)(a3_cos(vcam.pitch) >> 4);
    vcam.sp = (int16_t)(a3_sin(vcam.pitch) >> 4);
    vcam.shadow_y = U(ld_height(&L->T, L->c.p[0], L->c.p[2]));
    ld_up(&L->c, vcam.up);
    ter_done();
}
