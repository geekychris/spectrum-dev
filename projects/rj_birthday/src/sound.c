/*
 * sound.c - RJ's Birthday Bash's sound (the original's sound.c and the
 * music in its main.c) on the Next's TurboSound.
 *
 * Music: the original's MODs (modplay, tools/gen_sound.py; the songs in
 * page 55, mapped into slot 2 around modplay's calls) on chips 0 and 1:
 * party.mod on the title and in play, birthday.mod on the win, game over
 * and credits screens (and after them until the title), as the original's
 * main loop switches them.
 *
 * Effects and the arcade cabinet's voices: AY streams made from the
 * original's samples (gen_sound.py), on chip 2. As the original's
 * mt_playfx: an effect doesn't cut off one of higher priority. A voice
 * pauses the music for the clip's length (the original's
 * size / 200 + 10 frames) and the music goes on where it was.
 */
#include <stdint.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "aystream.h"
#include "modplay.h"
#include "game.h"
#include "sound.h"
#include "sound_data.h"

extern const mod_song party_song, bday_song;

#define SONG_PAGE 55                         /* the songs (Makefile): at 0x4000 while modplay reads them */
#define SONGS(call) do { uint8_t s2_ = ZXN_READ_REG(0x52); ZXN_WRITE_REG(0x52, SONG_PAGE); call; ZXN_WRITE_REG(0x52, s2_); } while (0)

static const mod_song *playing;
static uint8_t cur_pri;
static ULONG snd_rng = SND_RNG_AFTER_INIT;
static WORD voice_active = -1, voice_frames_left;

static WORD snd_rand(void)
{
    snd_rng = snd_rng * 1103515245UL + 12345UL;
    return (WORD)((snd_rng >> 16) & 0x7FFF);
}

void snd_init(void)
{
    ay_init();
    as_init();
}

static void play(uint8_t k)
{
    if (as_busy(2) && snd_pri[k] < cur_pri) return;
    cur_pri = snd_pri[k];
    as_play(2, SND_PAGE, snd_addr[k], 0);
}

static void music(const mod_song *s)
{
    if (s == playing) return;
    playing = s;
    SONGS(modplay_start(s));
}

/* the original's main loop: party.mod on the title and in play, birthday.mod on the end screens */
void music_for_state(int16_t state)
{
    if (state == GS_WIN || state == GS_GAMEOVER || state == GS_CREDITS) music(&bday_song);
    else if (state == GS_TITLE || state == GS_PLAYING) music(&party_song);
}

/* the music's channels silent (chips 0 and 1, A and B) */
static void hush(void)
{
    uint8_t c;
    for (c = 0; c < 2; c++) {
        z80_outp(0xFFFD, 0xFF - c);
        z80_outp(0xFFFD, 8);
        z80_outp(0xBFFD, 0);
        z80_outp(0xFFFD, 9);
        z80_outp(0xBFFD, 0);
    }
    z80_outp(0xFFFD, 0xFF);
}

void snd_tick(void)
{
    if (voice_active >= 0) {                  /* arcade_voice_check_done */
        if (--voice_frames_left <= 0) voice_active = -1;
    }
    else SONGS(modplay_tick());
    as_tick();
}

/* register r of chip c, read back (for the log) */
uint8_t snd_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}

void sfx_doorbell(void) { play(SND_DOORBELL); }
void sfx_slide(void) { play(SND_SLIDE); }
void sfx_chomp(void) { play(SND_CHOMP); }
void sfx_ding(void) { play(SND_DING); }
void sfx_buzzer(void) { play(SND_BUZZER); }
void sfx_cheer(void) { play(SND_CHEER); }
void sfx_pickup(void) { play(SND_PICKUP); }
void sfx_crash(void) { play(SND_CRASH); }
void sfx_party(void) { play(SND_PARTY); }
void sfx_arcade_a(void) { play(SND_ARCADE_A); }

void arcade_voice_load(void) {}
void arcade_voice_cleanup(void) {}

void arcade_voice_play_random(void)
{
    WORD pick = snd_rand() % 4;                /* (all four clips are there: no fallback) */
    hush();
    cur_pri = 0;
    play(SND_VOICE0 + (uint8_t)pick);
    voice_active = pick;
    voice_frames_left = (WORD)(voice_size[pick] / 200) + 10;
}

WORD arcade_voice_is_playing(void) { return voice_active >= 0; }
WORD arcade_voice_current(void) { return voice_active; }
void arcade_voice_check_done(void) {}          /* in snd_tick */
