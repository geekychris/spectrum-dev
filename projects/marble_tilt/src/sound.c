/* sound.c - placeholder until the streams are made */
#include <z80.h>
#include "sound.h"
void snd_init(void) {}
void snd_music(uint8_t m) { (void)m; }
void snd_play(uint8_t sfx) { (void)sfx; }
void snd_tick(void) {}
uint8_t snd_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}
