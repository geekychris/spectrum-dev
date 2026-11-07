/* Lunar Rider: the Spectrum Next's picture (view.c). */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
void view_init(void);
void view_play(void);
void view_frame(void);
void view_commit(void);
void view_gameover(void);
void view_hide_sprites(void);
void view_title(uint8_t blink);
void view_title_blink(uint8_t on);
/* name = 0: draw the screen; else the three letters, pos the one edited */
void view_initials(const char *name, uint8_t pos, long score);
#endif
