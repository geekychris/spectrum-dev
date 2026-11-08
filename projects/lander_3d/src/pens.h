/* the original's 32 pens (render.c): 0 black, 1 white, 2 panel, 3 grey, 4
   sky, 5-10 ground (light to dark), 11-13 pad rings x1 x2 x5, 14 pad, 15
   shadow, 16-17 gold foil, 18-19 the ascent stage, 20 legs, 21-22 flame, 23
   red, 24 amber, 25 green, 26 cyan, 27 stars, 28 HUD grey, 29 dust, 30
   haze, 31 near black */
#ifndef PENS_H
#define PENS_H
#include <stdint.h>
#define PEN_SKY    4
#define PEN_GROUND 5
#define PEN_RING   11
#define PEN_PAD    14
#define PEN_SHADOW 15
#define PEN_GOLD   16
#define PEN_STAGE  18
#define PEN_LEGS   20
#define PEN_FLAME  21
#define PEN_RED    23
#define PEN_AMBER  24
#define PEN_GREEN  25
#define PEN_CYAN   26
#define PEN_STAR   27
#define PEN_HUD    28
#define PEN_DUST   29
extern const uint16_t ld_palettes[3][32];   /* RGB444, as the original's (vcam.c) */
#endif
