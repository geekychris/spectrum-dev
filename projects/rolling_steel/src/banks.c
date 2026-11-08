/*
 * Calls into the second rules bank. port: game.c's code fills a 16K bank,
 * so the route functions (game_path.c) and the course loader (course_c.c)
 * are in bank 15, mapped at 0x0000 like bank 14 (game.c); these resident
 * stubs map bank 15 for the call and bank 14 back (every caller is game.c
 * or runs while bank 14 is mapped).
 */
#include <arch/zxn.h>
#include "nextgfx.h"
#include "game.h"

void path_at_b(long t, V3 *ap);
long course_progress_b(const Player *p);
void demo_input_b(Player *p, fix *wx, fix *wz);
void demo_resync_b(Player *p);
void respawn_point_b(const Player *p, V3 *rp);
int course_load_b(int index);

#define TO15() do { ZXN_WRITE_MMU0(30); ZXN_WRITE_MMU1(31); } while (0)
#define TO14() do { ZXN_WRITE_MMU0(28); ZXN_WRITE_MMU1(29); } while (0)

void path_at(long t, V3 *ap) { TO15(); path_at_b(t, ap); TO14(); }
long course_progress(const Player *p) { long r; TO15(); r = course_progress_b(p); TO14(); return r; }
void demo_input(Player *p, fix *wx, fix *wz) { TO15(); demo_input_b(p, wx, wz); TO14(); }
void demo_resync(Player *p) { TO15(); demo_resync_b(p); TO14(); }
void respawn_point(const Player *p, V3 *rp) { TO15(); respawn_point_b(p, rp); TO14(); }
int course_load(int index) { int r; TO15(); r = course_load_b(index); TO14(); return r; }

/* the physics' code is in bank 16 (phys.asm), called from either rules bank
   and from the view: map it, then put back whatever was there */
void phys_reset_b(void);
void phys_reset_enemies_b(void);
void phys_world_step_b(void);
void phys_marble_step_b(Ball *b, fix wx, fix wz);
int phys_ground_below_b(const V3 *p, fix *y);

#define PHYS_IN() uint8_t m0 = ZXN_READ_REG(0x50), m1 = ZXN_READ_REG(0x51); ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33)
#define PHYS_OUT() ZXN_WRITE_REG(0x50, m0); ZXN_WRITE_REG(0x51, m1)

void phys_reset(void) { PHYS_IN(); phys_reset_b(); PHYS_OUT(); }
void phys_reset_enemies(void) { PHYS_IN(); phys_reset_enemies_b(); PHYS_OUT(); }
void phys_world_step(void) { PHYS_IN(); phys_world_step_b(); PHYS_OUT(); }
void phys_marble_step(Ball *b, fix wx, fix wz) { PHYS_IN(); phys_marble_step_b(b, wx, wz); PHYS_OUT(); }
int phys_ground_below(const V3 *p, fix *y) { int r; PHYS_IN(); r = phys_ground_below_b(p, y); PHYS_OUT(); return r; }
