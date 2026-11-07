/* td_int.h - what td_core.c shares with td_ai.c (port: the original's
   statics, one file there) */
#ifndef TD_INT_H
#define TD_INT_H
#include "td.h"

#define SHIELD_ARC 6370                 /* the bulwark's shield: 35 degrees each side of ahead */
extern const fix vmax[TK_COUNT];
fix fabs_(fix a);
int iabs(int a);
int adiff(int a, int b);                /* a - b, the short way round */
fix dist(fix dx, fix dz);
int near(fix dx, fix dz, fix r);        /* port: dist(dx, dz) < r, quick when far */
int beyond(fix dx, fix dz, fix r);      /* port: dist(dx, dz) > r, likewise */
void drive(TDGame *g, Tank *t, int want, fix speed);
void lob(TDGame *g, int tank);
void think(TDGame *g, int k);           /* an enemy's mind (td_ai.c) */

#endif
