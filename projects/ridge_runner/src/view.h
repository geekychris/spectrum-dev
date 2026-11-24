/*
 * view.h - Ridge Runner's 3D view on the Next (the original's render.c).
 *
 * The scene is built as the original builds it: the chase camera, the
 * ground window around and ahead of the bike (each cell shaded by its
 * slope), the track ribbon with its kerbs, the gates' pylons and banners,
 * the bikes' shadows, the far ridges; polygons sorted far to near by the
 * original's depth keys and filled on Layer 2. The bikes are hardware
 * sprites (view_sprites).
 *
 * port: the picture is drawn at half height (256 x 96) and shown line
 * doubled by the copper; the ground's heights and the track's
 * cross-sections are cached as the bike moves (the original works them
 * out every frame); grid points are projected incrementally; pylons are
 * flat on to the camera (two shaded halves and a cap) instead of prisms.
 */
#ifndef RR_VIEW_H
#define RR_VIEW_H

#include <stdint.h>
#include "rr.h"

/* port: Layer 2's 256 x 192 (row spans, the SDK's l2poly), drawn at half
   height and line doubled: the original's 320 x 256 view scaled by 4/5 across
   and 3/4 down; the border round it holds the HUD */
#define VIEW_W   256
#define VIEW_H   96                     /* rows drawn: the 192 shown, line doubled */

/* the pens (the original's palette slots) */
#define PEN_BLACK  0
#define PEN_WHITE  1
#define PEN_PANEL  2
#define PEN_GREY   3
#define PEN_SKY    4
#define PEN_GROUND 5                    /* 5..10, light to dark */
#define PEN_TRACK  11                   /* 11..13 */
#define PEN_KERB   14
#define PEN_SHADOW 15
#define PEN_BIKE   16                   /* 16..23: four bikes, two pens each */
#define PEN_PYLON  24
#define PEN_BANNER 25
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29
#define PEN_RIDGE  30
#define PEN_FLAME  31
#define PEN_PYLON_DARK 32               /* port: the pylon's shaded side */

/* what the view needs from the game shell */
typedef struct {
    int32_t cam_yaw;                    /* the chase camera's heading (eases after the bike's) */
    uint8_t show_player;
} ViewIn;

void view_new_track(const RRGame *g);   /* forget the caches (a new track or seed) */
/* build the scene for g; returns the number of polygons */
uint16_t view_build(const RRGame *g, const ViewIn *in);
/* fill the built polygons through fill() (Next: the asm filler; host: a framebuffer) */
typedef void (*ViewFill)(const int16_t *xy, uint8_t n, uint8_t pen);
void view_draw(ViewFill fill);
int16_t view_horizon(void);             /* the horizon's row (0..VIEW_H) */

/* the bikes as sprites: where each is on the full-height screen, its
   heading relative to the camera, its size, and the row below which a
   crest in front hides it */
typedef struct {
    uint8_t on;
    int16_t x, y;                       /* screen position of its origin (full height) */
    uint16_t scale;                     /* fov / depth, 8.8 */
    int16_t yaw;                        /* relative to the camera (16-bit angle) */
    int16_t lean;
    int16_t cut;                        /* rows from here down are hidden (full height) */
    uint8_t colour, flame;
} ViewBike;
extern ViewBike view_bikes[RR_RIVALS + 1];      /* [0] the player */

/* the far ridges' profile drift (the original's sky()) */
extern const uint16_t view_themes[3][33];
extern const uint16_t view_sky_top[3], view_sky_bottom[3];
uint8_t view_theme(int32_t track);

#endif
