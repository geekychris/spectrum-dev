/* The MOD player (see modplay.h). */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include "zxgame.h"
#include "modplay.h"

extern uint8_t ay_mixer;                /* sound.c: chip 0's register 7 */

typedef struct { const mod_inst *in; uint16_t period; uint8_t vol, t; } voice_state;
static const mod_song *song;
static voice_state vs[4];
static uint16_t row;
static uint8_t tick, mixer[3];
/* the chips the song's routes use: a 128K ignores the chip select, so
   touching chips 1 and 2 there would overwrite chip 0 (all tones off) */
static uint8_t chips;

static void reg(uint8_t chip, uint8_t r, uint8_t v)
{
    z80_outp(0xFFFD, 0xFF - chip);      /* TurboSound: select the chip (a 128K ignores it) */
    z80_outp(0xFFFD, r);
    z80_outp(0xBFFD, v);
}

void modplay_start(const mod_song *s)
{
    uint8_t c, r;
    song = s;
    row = 0;
    tick = 0;
    memset(vs, 0, sizeof vs);
    mixer[1] = mixer[2] = 0x3F;
    chips = 1;
    for (c = 0; c < 4; c++)
        if (s->route[c] != 0xFF) chips |= 1 << (s->route[c] >> 4);
    for (c = 1; c < 3; c++)
        if (chips & (1 << c))
            for (r = 0; r < 14; r++) reg(c, r, r == 7 ? 0x3F : 0);
    z80_outp(0xFFFD, 0xFF);
}

void modplay_stop(void)
{
    uint8_t c;
    if (!song) return;
    for (c = 0; c < 4; c++)
        if (song->route[c] != 0xFF) reg(song->route[c] >> 4, 8 + (song->route[c] & 3), 0);
    song = 0;
    z80_outp(0xFFFD, 0xFF);
}

void modplay_tick(void)
{
    uint8_t c, chip, ch, m;
    if (!song || !ay_present) return;
    if (tick == 0) {
        const uint8_t *r = song->rows + row * 4;
        for (c = 0; c < 4; c++)
            if (r[c] && song->route[c] != 0xFF) {
                const mod_voice *v = &song->voice[c][r[c] - 1];
                vs[c].in = &song->inst[v->inst];
                vs[c].period = v->period;
                vs[c].vol = vs[c].in->vol;
                vs[c].t = 0;
            }
        if (++row == song->nrows) row = 0;
    }
    if (++tick >= song->speed) tick = 0;
    mixer[0] = ay_mixer;
    for (c = 0; c < 4; c++) {
        voice_state *v = &vs[c];
        const mod_inst *in = v->in;
        if (song->route[c] == 0xFF) continue;
        chip = song->route[c] >> 4;
        ch = song->route[c] & 3;
        m = mixer[chip] | (9 << ch);            /* this channel: tone and noise off */
        if (in && v->vol) {
            if (in->kind == MI_NOISE) {
                reg(chip, 6, (uint8_t)in->param);
                m &= ~(8 << ch);
            }
            else {
                reg(chip, ch * 2, (uint8_t)v->period);
                reg(chip, ch * 2 + 1, (uint8_t)(v->period >> 8) & 0x0F);
                m &= ~(1 << ch);
                if (in->kind == MI_KICK) v->period += in->param;
            }
            reg(chip, 8 + ch, v->vol);
            if (in->decay && ++v->t >= in->decay) {
                v->t = 0;
                v->vol = v->vol > in->step ? v->vol - in->step : 0;
            }
        }
        else reg(chip, 8 + ch, 0);
        mixer[chip] = m;
    }
    ay_mixer = mixer[0];
    for (c = 0; c < 3; c++)
        if (chips & (1 << c)) reg(c, 7, mixer[c]);
    z80_outp(0xFFFD, 0xFF);
}
