/*
 * Rolling Steel for the Spectrum Next: memory map, start-up, the 50 Hz loop.
 *
 *   0x0000-0x3FFF  bank 14: the rules (game.c); bank 15 (the route, the
 *                  course loader, the sprites) and bank 16 (the physics)
 *                  are swapped in by banks.c's stubs
 *   0x4000-0x5FFF  bank 5's first page (the tilemap); the physics maps a
 *                  course's triangle and grid pages here while it works,
 *                  the view the picture's
 *   0x6000-0xEFFF  the view, sound, SDK, the physics' maths and variables;
 *                  0xF000- the IM 2 table, the stack
 *   pages 18-27    Layer 2 (320 x 256)
 *   pages 36-203   the courses: physics and pictures (tools/gen_course.py)
 * Everything is in the .nex: it needs a 2 MB Next.
 */
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "game.h"
#include "view.h"
#include "rshash.h"
#include <input.h>

extern unsigned long rsmath_selftest(uint16_t n);   /* rsmath_test.asm */

void snd_play(int sfx) { (void)sfx; }
void snd_music(int theme) { (void)theme; }
void snd_roll(fix s) { (void)s; }

static unsigned long hash = 2166136261UL;
static unsigned long steps;
static uint8_t trace;                    /* D held at start: log the marble each race step */
static uint8_t hashing;                  /* H held at start: hash the race (test.py, tools/hostsim.c) */

/* the keys and the joystick as the original's pad (game.h B_*).
   port: SPACE/fire is A (start), ENTER is P (pause); the view's turn, zoom
   and tilt buttons (L, R, B, C) have no keys: the view is fixed */
static unsigned long pad(uint8_t in)
{
    unsigned long b = 0;
    if (in & IN_UP) b |= B_UP;
    if (in & IN_DOWN) b |= B_DOWN;
    if (in & IN_LEFT) b |= B_LEFT;
    if (in & IN_RIGHT) b |= B_RIGHT;
    if (in & IN_FIRE) b |= B_A;
    if (in & IN_START) b |= B_P;
    return b;
}

static uint16_t perf_frames, perf_steps, perf_loops, perf_real;
extern uint16_t pc_tri, pc_sph, pc_plane, pc_edge, pc_segax, pc_segfull, pc_contact;   /* phys.asm counters */

int main(void)
{
    int last_state = -1;
    next_init();
    ZXN_WRITE_MMU0(28);
    ZXN_WRITE_MMU1(29);
    ZXN_WRITE_REG(0x15, 0x1B);               /* sprites on, over the border; tilemap/ULA over sprites over Layer 2 */
    view_init();
    ZXN_WRITE_MMU0(30);                      /* the sprites are in bank 15 */
    ZXN_WRITE_MMU1(31);
    view_sprite_init();
    ZXN_WRITE_MMU0(28);
    ZXN_WRITE_MMU1(29);
    input_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    if (key_down(IN_KEY_SCANCODE_t))         /* T held: check the 32-bit maths (test.py) */
        zx_log("rs: mathtest n=2000 h=%08lX\n", rsmath_selftest(2000));
    trace = key_down(IN_KEY_SCANCODE_d);
    hashing = trace || key_down(IN_KEY_SCANCODE_h);
    zx_log("rs: ready kempston=%u\n", input_kempston);
    game_init();
    for (;;) {
        uint8_t t = frame_wait(), in, pr;
        unsigned long held, pressed;
        perf_real += t;
        if (t > 8) t = 8;
        in = input_read();
        pr = input_pressed();
        held = pad(in);
        pressed = pad(pr);
        perf_frames += t;
        perf_loops++;
        while (t--) {
            game_step(held, pressed, 0, 0);
            pressed = 0;
            if (hashing) hash = rs_hash(hash);
            steps++;
            perf_steps++;
            if (trace && G.state == ST_PLAYING)
                zx_log("tr %lu %ld %ld %ld %ld %ld %ld g%d\n", steps, G.p[0].m.p.x, G.p[0].m.p.y, G.p[0].m.p.z,
                       G.p[0].m.v.x, G.p[0].m.v.y, G.p[0].m.v.z, G.p[0].m.grounded);
            if (hashing && G.state != last_state) {
                last_state = G.state;
                zx_log("rs: hash step=%lu state=%d level=%d h=%08lX\n", steps, G.state, G.level + 1, hash);
            }
        }
        view_frame();
        view_sprites();
        if (perf_frames >= 100) {
            zx_log("rs: perf frames=%u real=%u loops=%u steps=%u cols=%u rows=%u\n", perf_frames, perf_real, perf_loops,
                   perf_steps, view_cols, view_rows);
            zx_log("rs: phys tri=%u sph=%u plane=%u edge=%u segax=%u segfull=%u contact=%u\n", pc_tri, pc_sph, pc_plane,
                   pc_edge, pc_segax, pc_segfull, pc_contact);
            pc_tri = pc_sph = pc_plane = pc_edge = pc_segax = pc_segfull = pc_contact = 0;
            perf_frames = perf_loops = perf_steps = perf_real = 0;
            view_cols = view_rows = 0;
        }
    }
    return 0;
}
