/*
 * The port's game logic (src/game.c, room.c, world.c, levels.c, probe.c) on
 * the host, for developing the test bot (tools/bot.py) and for comparing
 * with the original (tools/orig, Q12). Line protocol on stdin/stdout:
 *
 *   in:  "<held> <pressed>" (pad bits, decimal): one logic step
 *        "q": quit
 *   out: "L <log line>" for each keep_log, then "P <probe bytes in hex>"
 *
 *   cc -O2 -Isrc -o build/host/sim tools/sim.c src/game.c src/room.c \
 *      src/world.c src/levels.c src/probe.c
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "keep.h"

void keep_log(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("L %s", buf);
    if (!strchr(buf, '\n')) printf("\n");
}
void snd_play(uint8_t s) { (void)s; }
void snd_music(uint8_t t) { (void)t; }
void snd_duck(int16_t t) { (void)t; }
void snd_music_enable(uint8_t on) { (void)on; }

int main(void)
{
    char line[64];
    unsigned held, pressed;
    size_t i;
    game_init();
    probe_update();
    for (;;) {
        printf("P ");
        for (i = 0; i < sizeof(keep_probe); i++) printf("%02x", ((unsigned char *)&keep_probe)[i]);
        printf("\n");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin) || line[0] == 'q') break;
        if (sscanf(line, "%u %u", &held, &pressed) != 2) continue;
        game_step((uint16_t)held, (uint16_t)pressed);
        probe_update();
    }
    return 0;
}
