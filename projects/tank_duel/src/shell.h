/* shell.h - the game flow (shell.c, resident) shared with the front end */
#ifndef SHELL_H
#define SHELL_H
#include <stdint.h>

void map_rules(void);                   /* main.c: the rules' banks at 0x0000-0x7FFF */
void map_front(void);                   /* main.c: the front end's bank at 0x0000 */
void sfx(uint8_t k);
void flash(const char *s, uint8_t pen);
void start_game(uint8_t round);         /* (map the rules, then the front end back) */
void to_title(void);
enum { SFX_FIRE, SFX_EFIRE, SFX_LOB, SFX_BOOM, SFX_HIT, SFX_GLANCE, SFX_BOUNCE, SFX_HURT, SFX_CRATE, SFX_CLEAR, SFX_OVER, SFX_WIN };
enum { C_WHITE, C_YELLOW, C_GREEN, C_RED, C_CYAN, C_GREY, C_ORANGE, C_DARK };
extern uint8_t fire_lock, play_in, hud_dirty;

#endif
