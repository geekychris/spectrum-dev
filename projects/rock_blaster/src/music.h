/* the original's MOD on the 128K's AY (src/music_song.c, tools/make_music.sh) */
#ifndef RB_MUSIC_H
#define RB_MUSIC_H
#include "modplay.h"
extern const mod_song rb_song;
extern void rb_music_init(void);       /* unpacks the rows (lowmem.h) */
#define rb_music_start() modplay_start(&rb_song)
#define rb_music_tick()  modplay_tick()
#define rb_music_stop()  modplay_stop()
#endif
