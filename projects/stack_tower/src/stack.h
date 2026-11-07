/*
 * stack.h - Stack Tower's shell on the Next, shared by its files (the
 * original's stack.h): the game state around the rules, the screen, the
 * drawing and the sound.
 */
#ifndef STACK_H
#define STACK_H

#include <stdint.h>
#include "st.h"

enum { SS_TITLE, SS_PLAY, SS_OVER, SS_NAME };    /* port: SS_NAME, initials for the high scores */

typedef struct {
    uint8_t state;
    uint8_t state_timer;
    uint8_t mode;
    int16_t start_level;
    uint16_t view;              /* the column facing you, 8.8 (0..4095), turning towards the piece */
    uint8_t ghost, paused, help;
    const char *flash;
    uint8_t flash_timer;
    uint16_t title_frames;      /* the title: painting, then the demo, in turn */
    int8_t rank;                /* SS_NAME: the high-score place */
} Stack;

extern Stack stack;
extern StGame game;

/* render.c's pens (gfx_data.c game_pal: 0..31 the original's palette) */
#define PEN_BLACK   0
#define PEN_WHITE   1
#define PEN_PANEL   2
#define PEN_GREY    3
#define PEN_CORE    5
#define PEN_GUIDE   6
#define PEN_LINE    7
#define PEN_PIECE   8
#define PEN_GARBAGE 24
#define PEN_YELLOW  27
#define PEN_CYAN    28
#define PEN_GREEN   29
#define lit_pen(v)   ((v) == ST_GARBAGE ? PEN_GARBAGE : PEN_PIECE + 2 * ((v) - 1))
#define shade_pen(v) (lit_pen(v) + 1)

/* screen.c: Layer 2 (320 x 256, double buffered), the title painting, the tilemap */
extern uint8_t scr_back;        /* 0 or 1: the buffer being drawn (l2w_bank is its bank) */
void scr_init(void);
void scr_flip(void);            /* after frame_wait: show the back buffer */
void scr_show_painting(uint8_t on);
void tm_text(uint8_t col, uint8_t row, const char *s, uint8_t pal);       /* over everything, see-through */
void tm_text_box(uint8_t col, uint8_t row, const char *s, uint8_t pal);   /* on black */
void tm_text_centre(uint8_t row, const char *s, uint8_t pal);
void tm_blank(void);
void tm_cover(void);            /* the whole screen black (the help pages) */
enum { TP_WHITE, TP_YELLOW, TP_GREEN, TP_GREY, TP_CYAN, TP_RED };
enum { BIG_YELLOW, BIG_CYAN, BIG_RED, BIG_GREEN };   /* sprite palette offsets of the big words */

/* tower.c */
void tower_draw(uint8_t with_piece, uint8_t guide);
extern uint16_t tower_bricks;   /* blitted in the last frame */
extern uint16_t tower_redraws;  /* frames that drew the whole board */
void tower_invalidate(void);
void tower_copper(void);
void tower_guide_off(void);     /* no guide (before the title painting's palette) */        /* after the flip: the drop guide's copper list for the frame shown */

/* panel.c: the panel and the text over the view, into the back buffer */
void panel_draw(void);
void panel_invalidate(void);
void view_text(void);           /* PAUSED, the game over box, initials */
void big_words(uint8_t show);   /* the title's name, the flash, TOPPED: sprites */
void help_draw(uint8_t page);
void title_painting_text(void);
void title_painting_reset(void);

/* sound.c */
void snd_init(void);
void snd_event(uint8_t type, int16_t a);
void snd_music(uint8_t on);
void snd_tick(void);
uint8_t snd_reg(uint8_t chip, uint8_t reg);

/* text helpers (panel.c) */
char *fmt_u32(char *p, uint32_t v);
char *fmt_str(char *p, const char *s);

/* high scores: one table a mode (port: the original keeps one best a mode) */
void hs_use_mode(uint8_t mode);

#endif
