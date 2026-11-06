/* Frank the Frog - player control (port of frank.h; drawing in render.c) */
#ifndef FRANK_H
#define FRANK_H
void frank_init(void);
void frank_move(int dx, int dy);
int frank_col(void);
int frank_row(void);
int frank_x(void);
int frank_x16(void);
void frank_set_x16(int x16);
int frank_die_tick(void);
void frank_start_death(void);
int frank_is_dying(void);
int frank_death_timer(void);
int frank_highest_row(void);
#endif
