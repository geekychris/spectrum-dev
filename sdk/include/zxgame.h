/*
 * zxgame.h - the spectrum_dev game layer (48K/128K and Next).
 *
 * Linked from build/zxsdk.lib, so a game only carries what it calls.
 *
 *   fixed point   fix8 = 8.8; fix_mul/fix_div; fix_sin/fix_cos (256 steps per
 *                 turn, table); fix_atan2; isqrt16
 *   random        rnd_seed / rnd16 / rnd_range   (xorshift, deterministic)
 *   frame timer   frame_init() installs IM 2; frame_wait(): HALT on the 50 Hz
 *                 interrupt; counts frames and overruns (missed frames)
 *   input         input_init (detects a Kempston), input_read: keys + Kempston
 *                 + Sinclair/cursor joysticks -> IN_* bits; input_pressed: edges
 *   sound         beeper effects (blocking, short); AY (128K/+2/+3/Next) tone,
 *                 noise and a tiny music player ticked once per frame
 *   48K graphics  screen row table, pre-shifted XOR/OR sprites, rect clear,
 *                 8x8 tiles, attributes, big text, numbers
 *   high scores   a small table with three-letter names
 *
 * Next-only parts are in nextgfx.h.
 */
#ifndef ZXGAME_H
#define ZXGAME_H

#include <stdint.h>

/* ------------------------------------------------------------ fixed point */

typedef int16_t fix8;                       /* 8.8: 256 = 1.0 */
#define FIX(v)        ((fix8)((v) * 256))
#define FIX_INT(f)    ((int16_t)((f) >> 8))
#define FIX_FRAC(f)   ((uint8_t)(f))
extern const int16_t fix_sin_tab[256];
#define fix_sin(a)    (fix_sin_tab[(uint8_t)(a)])
#define fix_cos(a)    (fix_sin_tab[(uint8_t)((a) + 64)])
extern fix8 fix_mul(fix8 a, fix8 b);
extern fix8 fix_div(fix8 a, fix8 b);          /* b != 0 */
extern uint8_t fix_atan2(int16_t dy, int16_t dx);  /* angle 0..255, 0 = +x, 64 = +y */
extern uint16_t isqrt16(uint16_t v);

/* ------------------------------------------------------------ random */

extern void rnd_seed(uint16_t s);          /* 0 is replaced by 1 */
extern uint16_t rnd16(void);
extern uint16_t rnd_range(uint16_t n);     /* 0..n-1 */

/* ------------------------------------------------------------ frame timer */

/* IM 2 with our own handler (the ROM's IM 1 handler needs IY = 0x5C3A, which
   compiled C does not keep). Uses 0xF000-0xF1F3: keep the program below 0xF000. */
extern void frame_init(void);              /* install IM 2, enable interrupts, reset counters */
/* wait for the next frame (HALT); returns how many frames passed since the last
   call (1 = on time). Fixed-timestep games run their logic that many times. */
extern uint8_t frame_wait(void);
/* back to the ROM's IM 1 (I = 0x3F) before returning to BASIC (code run with USR) */
extern void frame_exit(void);
extern volatile uint8_t frame_ticks;       /* incremented by the interrupt */
extern void (*frame_hook)(void);           /* called from the interrupt each frame if set */
extern uint16_t frame_count;               /* frames since frame_init (waits) */
extern uint16_t frame_overruns;            /* frames the game took too long for */

/* ------------------------------------------------------------ input */

#define IN_LEFT   0x01
#define IN_RIGHT  0x02
#define IN_UP     0x04
#define IN_DOWN   0x08
#define IN_FIRE   0x10
#define IN_FIRE2  0x20
#define IN_START  0x40          /* ENTER */
#define IN_QUIT   0x80          /* BREAK (CAPS+SPACE) */

extern uint8_t input_kempston;             /* 1 if a Kempston was detected */
extern void input_init(void);
/* keys: Q A O P / cursor 5 6 7 8 / Sinclair 1 (6..0) / Sinclair 2 (1..5),
   SPACE or M or 0 = fire, Z = fire2, ENTER = start; plus the Kempston */
extern uint8_t input_read(void);
extern uint8_t input_pressed(void);        /* bits that went down since the last read */
/* latch presses between reads: call input_sample() from frame_hook (every
   interrupt) and input_latch(1); then a tap shorter than a slow game frame
   still shows up in the next input_read()/input_pressed() */
extern void input_sample(void);
extern void input_latch(uint8_t on);
extern uint8_t input_last;                 /* state from the last input_read() */
extern uint8_t key_down(uint16_t scancode);/* raw: IN_KEY_SCANCODE_x from <input.h> */
extern char input_letter(void);            /* a letter key held now ('A'..'Z'), else 0 */
/* a letter key that went down since the read before the last input_read()
   (latched ones too, see input_latch), else 0: for typing */
extern char input_letter_pressed(void);

/* ------------------------------------------------------------ sound */

extern uint8_t snd_border;                 /* border colour kept by the beeper */
/* square wave: `cycles` periods of `period` (~ 8*period + 60 T-states each). Blocking. */
extern void beep(uint16_t period, uint16_t cycles);
extern void beep_noise(uint16_t length);  /* white noise burst, ~ 40*length T-states */
extern void beep_sweep(uint16_t from, uint16_t to, uint8_t step, uint8_t cycles);

extern uint8_t ay_present;                 /* set by ay_init() */
extern void ay_init(void);                 /* detect + silence (also selects chip 0 on the Next) */
extern void ay_write(uint8_t reg, uint8_t val);
extern void ay_tone(uint8_t ch, uint16_t period, uint8_t vol);  /* ch 0..2, vol 0..15 */
extern void ay_noise(uint8_t ch, uint8_t period, uint8_t vol);
extern void ay_off(uint8_t ch);
/* effect on a channel that decays by `decay` volume steps per frame */
extern void ay_sfx(uint8_t ch, uint16_t period, uint8_t vol, uint8_t decay, int8_t sweep);

/* music: a list of notes per channel; note = AY period (0 = rest), len in frames,
   a {0,0} entry loops back to the start. */
typedef struct { uint16_t period; uint8_t len; } ay_note;
extern void music_play(uint8_t ch, const ay_note *track, uint8_t vol);
extern void music_stop(void);
extern void sound_tick(void);              /* call once per frame: music + effects */
#define AY_NOTE(n)    (ay_note_periods[(n)])     /* n = MIDI note 24..95 */
extern const uint16_t ay_note_periods[96];

/* ------------------------------------------------------------ 48K graphics */

extern uint8_t *zx_row[192];               /* screen address of each pixel line */
extern void gfx_init(void);                /* fills zx_row */
/* pre-shift a sprite w bytes wide, h lines: writes 8 shifted copies of
   (w+1)-byte lines to dst (8*(w+1)*h bytes). */
extern void spr_preshift(const uint8_t *src, uint8_t w, uint8_t h, uint8_t *dst);
/* XOR / OR / AND-NOT / store a pre-shifted sprite at pixel (x, y). x 0..255;
   clips at the right edge and at y >= 192. spr_put overwrites the bytes (for
   solid objects that clear their own trail). */
extern void spr_xor(uint8_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h);
extern void spr_or(uint8_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h);
extern void spr_clear(uint8_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h);
extern void spr_put(uint8_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h);
/* clear / fill pixel lines y..y+h-1 between byte columns c0..c0+wc-1 */
extern void gfx_clear_rect(uint8_t c0, uint8_t y, uint8_t wc, uint8_t h);
extern void gfx_fill_rect(uint8_t c0, uint8_t y, uint8_t wc, uint8_t h, uint8_t pattern);
extern const uint8_t gfx_bit[8];           /* 0x80 >> n */
extern void gfx_plot(uint8_t x, uint8_t y);
extern void gfx_unplot(uint8_t x, uint8_t y);
extern uint8_t gfx_point(uint8_t x, uint8_t y);
extern void gfx_hline(uint8_t x0, uint8_t x1, uint8_t y);
/* 8x8 tile (8 bytes) at a character cell */
extern void tile_draw(uint8_t col, uint8_t row, const uint8_t *tile);
extern void attr_set(uint8_t col, uint8_t row, uint8_t attr);
extern void attr_fill(uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t attr);
extern void screen_clear(uint8_t attr, uint8_t border);
/* text in the ROM font (zxtext.h has zx_print_at); big = 2x2 cells per char */
extern void text_big(uint8_t col, uint8_t row, const char *s);
extern void text_num(uint8_t col, uint8_t row, uint32_t v, uint8_t digits);  /* zero-padded */
extern void text_center(uint8_t row, const char *s);
/* ROM-font text XORed onto pixel line y (any line, 0..191) at character column
   col: drawing it twice removes it, so it can sit over XOR graphics; tall = 1
   doubles every font line (8x16). text_xor.c */
extern void text_xor(uint8_t col, uint8_t y, const char *s, uint8_t tall);

/* ------------------------------------------------------------ high scores */

#define HS_ENTRIES 5
typedef struct { char name[4]; uint32_t score; } hs_entry;
extern hs_entry hs_table[HS_ENTRIES];
extern void hs_init(uint32_t base);        /* defaults: base*5, base*4, ... */
extern int8_t hs_rank(uint32_t score);     /* place 0..4, or -1 */
extern void hs_insert(int8_t place, const char *name, uint32_t score);
extern void hs_draw(uint8_t col, uint8_t row);   /* 48K screen */
/* the initials editor, for any screen: hs_edit_begin(), then each frame
   input_read() and hs_edit_step(), redraw on HS_EDIT_CHANGED, stop on
   HS_EDIT_DONE (name[] is the three letters, pos the one being edited) */
typedef struct { char name[4]; uint8_t pos; } hs_editor;
#define HS_EDIT_CHANGED 1               /* name or pos changed */
#define HS_EDIT_TAKEN   2               /* a letter was taken (a sound) */
#define HS_EDIT_DONE    4
extern void hs_edit_begin(hs_editor *e);
extern uint8_t hs_edit_step(hs_editor *e);
/* initials entry screen (48K screen): typed letters, or UP/DOWN + FIRE; logs
   "<game>: hiscore rank=N" and "<game>: name=XYZ", then inserts the entry */
extern void hs_enter(int8_t place, uint32_t score, const char *game);

#endif
