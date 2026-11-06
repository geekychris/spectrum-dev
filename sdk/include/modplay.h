/*
 * modplay.h - plays a ProTracker MOD converted for the AY by
 * sdk/tools/mod2ay.py: up to four MOD channels, each routed to a channel of
 * one of the Next's three TurboSound chips (or chip 0 on a 128K), with
 * simple instruments: a tone that holds or decays, a noise drum, a kick
 * (a falling tone). Channels it doesn't use stay free for effects (ay_sfx
 * on chip 0; the player keeps chip 0's mixer bits of the others).
 *
 *   modplay_start(&my_song);       after ay_init()
 *   modplay_tick();                once a frame (leaves chip 0 selected)
 */
#ifndef MODPLAY_H
#define MODPLAY_H
#include <stdint.h>

enum { MI_TONE, MI_NOISE, MI_KICK };
typedef struct {
    uint8_t kind, vol;                  /* MI_*, start volume 0..15 */
    uint8_t decay, step;                /* every `decay` frames the volume drops by `step` (decay 0: holds) */
    uint16_t param;                     /* MI_NOISE: noise period; MI_KICK: period added each frame */
} mod_inst;
typedef struct { uint16_t period; uint8_t inst; } mod_voice;   /* AY period (kick: the start) */
typedef struct {
    uint16_t nrows;
    uint8_t speed;                      /* frames a row */
    const uint8_t *rows;                /* nrows x 4: 0 = nothing new, else voice index + 1 */
    const mod_voice *voice[4];          /* each MOD channel's notes */
    const mod_inst *inst;
    uint8_t route[4];                   /* chip << 4 | AY channel 0..2; 0xFF = not played */
} mod_song;

extern void modplay_start(const mod_song *s);
extern void modplay_stop(void);
extern void modplay_tick(void);
#define MOD_ROUTE(chip, ch) ((uint8_t)((chip) << 4 | (ch)))

#endif
