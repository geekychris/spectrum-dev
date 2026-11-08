/*
 * The original's rules on the host: game.c, phys.c and course.c from
 * tools/orig, unchanged, built with a 32-bit long (long32.h).
 *
 *   cc -O2 -std=gnu89 -include tools/long32.h -Itools/orig -o build/hostsim \
 *      tools/hostsim.c tools/orig/game.c tools/orig/phys.c tools/orig/course.c
 *   build/hostsim [steps] [-v]
 *
 * It runs game_step() from game_init() with no input: after 25 s the title
 * starts the attract demo, the autopilot plays a course, and the title moves
 * on to the next course; so one run plays all six. A hash of the race state
 * after each step (rs_hash, the same words the Next hashes: hash.h) is printed at
 * each event, and the Next must log the same values.
 */
#include "game.h"
#include "../src/rshash.h"

static int verbose;
static unsigned long hash = 2166136261u;
static long steps;

void rs_log(const char *fmt, ...)
{
    va_list ap;
    char buf[256];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("%6ld %08lX %s", steps, (unsigned long)(hash & 0xFFFFFFFFu), buf);
}

void *rs_load(const char *name, long *size)
{
    char path[256];
    FILE *f;
    unsigned char *buf;
    long n, i;
    snprintf(path, sizeof path, "tools/orig/data/%s", name);
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); return 0; }
    fclose(f);
    for (i = 0; i + 3 < n; i += 4) {              /* big-endian words */
        unsigned char t = buf[i]; buf[i] = buf[i + 3]; buf[i + 3] = t;
        t = buf[i + 1]; buf[i + 1] = buf[i + 2]; buf[i + 2] = t;
    }
    for (i = 4; i < 36; i += 4) {                 /* but the name is bytes */
        unsigned char t = buf[i]; buf[i] = buf[i + 3]; buf[i + 3] = t;
        t = buf[i + 1]; buf[i + 1] = buf[i + 2]; buf[i + 2] = t;
    }
    *size = n;
    return buf;
}
void *rs_alloc(long bytes) { return malloc(bytes); }
void rs_free(void *p) { free(p); }
void rs_save_progress(void) {}
void rs_load_progress(void) {}
int snd_init(void) { return 1; }
void snd_play(int sfx) { if (verbose) printf("%6ld sfx %d\n", steps, sfx); }
void snd_music(int theme) { (void)theme; }
void snd_music_enable(int on) { (void)on; }
void snd_roll(fix s) { (void)s; }
int render_mark(void) { return 0; }
void render_scene(Cam *c, const RBall *b, int nb) { (void)c; (void)b; (void)nb; }

int main(int argc, char **argv)
{
    long n = argc > 1 ? atol(argv[1]) : 50L * 60 * 6;
    verbose = argc > 2;
    int last_state = -1;
    game_init();
    for (steps = 1; steps <= n; steps++) {
        game_step(0, 0, 0, 0);
        hash = rs_hash(hash);
        if (getenv("TRACE") && G.state == ST_PLAYING)
            printf("tr %d %d %d %d %d %d %d g%d\n", (int)steps, (int)G.p[0].m.p.x, (int)G.p[0].m.p.y,
                   (int)G.p[0].m.p.z, (int)G.p[0].m.v.x, (int)G.p[0].m.v.y, (int)G.p[0].m.v.z, G.p[0].m.grounded);
        if (G.state != last_state) {
            last_state = G.state;
            printf("rs: hash step=%lu state=%d level=%d h=%08lX\n", (unsigned long)steps, G.state, G.level + 1,
                   (unsigned long)(hash & 0xFFFFFFFFu));
        }
    }
    printf("end steps=%ld hash=%08lX\n", n, (unsigned long)(hash & 0xFFFFFFFFu));
    return 0;
}
