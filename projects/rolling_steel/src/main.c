/*
 * Rolling Steel for the Spectrum Next: memory map, start-up, the 50 Hz loop.
 *
 *   0x0000-0x3FFF  bank 14: the rules (game.c, course_c.c); bank 15 (the
 *                  route, the HUD) and bank 16 (the physics)
 *                  are swapped in by banks.c's stubs
 *   0x4000-0x5FFF  bank 5's first page: the tilemap (hud.c); the physics
 *                  maps a course's triangle and grid pages here while it
 *                  works, the view the picture's, the sound its streams'
 *   0x6000-0xEFFF  the view, the HUD, sound, SDK, the physics' maths and
 *                  variables; 0xF000- the IM 2 table, the stack
 *   pages 18-27    Layer 2 (320 x 256)
 *   pages 36-203   the courses: physics and pictures (tools/gen_course.py)
 *   pages 204-207  the music and effects (tools/gen_sound.py)
 * Everything is in the .nex: it needs a 2 MB Next.
 *
 * The loop: the rules at 50 steps a second (fixed timestep: as many steps
 * as frames went by), then one picture. port: the score and the high-score
 * table (the SDK's) are this port's; the original keeps best times.
 */
#include <string.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "palfx.h"
#include "zxlog.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "sound.h"
#include "rshash.h"

extern unsigned long rsmath_selftest(uint16_t n);   /* rsmath_test.asm */
extern uint16_t pc_tri, pc_sph, pc_plane, pc_edge, pc_segax, pc_segfull, pc_contact;   /* phys.asm counters */

uint8_t rom_font[768];                   /* the ROM's font (hud.c) */
uint32_t hud_score;                      /* port: the score */
uint8_t hud_entry;                       /* port: the initials editor is up */
char hud_initials[4];
uint8_t hud_initials_pos;
uint8_t hud_dark = 4;
static uint8_t dark_shown, dark_pal;
static unsigned long hash = 2166136261UL;
static unsigned long steps;
static uint8_t trace;                    /* D held at start: log the marble each race step */
static uint8_t hashing;                  /* H held at start: hash the race (test.py, tools/hostsim.c) */
uint8_t autopilot;                       /* port: B lets the original's autopilot drive (game.c) */

/* the keys and the joystick as the original's pad (game.h B_*).
   port: SPACE/fire is A (start), ENTER is P (pause), S is C (music on the
   title); the view's turn, zoom and tilt (L, R, B, C + up/down) have no
   keys: the view is fixed */
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

/* ---- the score (port) ---- */
static int last_state = -1;
static hs_editor ed;
static int8_t ed_place;

static void score_step(void)
{
    if (G.state == last_state) return;
    if (G.state == ST_PLAYING && last_state == ST_TITLE) hud_score = 0;
    if (G.state == ST_CLEAR && !G.demo) {
        /* port: 1000 a course times its number, the clock left in tenths, a medal */
        static const uint16_t medal[4] = { 0, 100, 300, 500 };
        hud_score += 1000UL * (G.level + 1) + (unsigned long)(G.p[0].time_left / 5) + medal[G.last_medal];
        zx_log("rs: score %lu course=%d medal=%d\n", hud_score, G.level + 1, G.last_medal);
    }
    if (hashing)
        zx_log("rs: hash step=%lu state=%d level=%d h=%08lX\n", steps, G.state, G.level + 1, hash);
    zx_log("rs: state %d level=%d\n", G.state, G.level + 1);
    last_state = G.state;
}

/* the death flash: the original adds a colour over everything (render_flash);
   here Layer 2's palette brightens towards it */
static uint8_t flashing;

static void flash_frame(void)
{
    const Player *p = &G.p[0];
    if (p->flash > 0 && G.state != ST_TITLE) {
        uint8_t r = (uint8_t)(((p->flash_r * p->flash) >> 7) * 7 / 31);
        uint8_t g = (uint8_t)(((p->flash_g * p->flash) >> 7) * 7 / 31);
        uint8_t b = (uint8_t)(((p->flash_b * p->flash) >> 7) * 7 / 31);
        palfx_load_fx(PAL_L2, 0, view_npal, view_pal, 8, RGB9(r, g, b));
        flashing = 1;
    } else if (flashing) {
        palfx_load_fx(PAL_L2, 0, view_npal, view_pal, 8, 0);
        flashing = 0;
    }
}

int main(void)
{
    memcpy(rom_font, (const void *)0x3D00, sizeof(rom_font));
    next_init();
    ZXN_WRITE_MMU0(28);
    ZXN_WRITE_MMU1(29);
    ZXN_WRITE_REG(0x15, 0x13);               /* sprites over the border; order U S L: tilemap over sprites over Layer 2 */
    view_init();
    view_sprite_init();
    hud_init();
    snd_init();
    input_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    if (key_down(IN_KEY_SCANCODE_t))         /* T held: check the 32-bit maths (test.py) */
        zx_log("rs: mathtest n=2000 h=%08lX\n", rsmath_selftest(2000));
    trace = key_down(IN_KEY_SCANCODE_d);
    hashing = trace || key_down(IN_KEY_SCANCODE_h);
    hs_init(2000);
    zx_log("rs: ready kempston=%u\n", input_kempston);
    game_init();
    for (;;) {
        uint8_t t = frame_wait(), in, pr, k;
        unsigned long held, pressed;
        perf_real += t;
        if (t > 8) t = 8;
        flash_frame();
        if (hud_dark != dark_shown || view_palgen != dark_pal) {   /* the HUD's shade (hud.c), in the vertical blank */
            palfx_load_fx(PAL_L2 | PAL_SECOND, 0, view_npal, view_pal, hud_dark, 0);
            dark_shown = hud_dark;
            dark_pal = view_palgen;
        }
        in = input_read();
        held = pressed = 0;
        if (!hud_entry) {                    /* (the initials editor reads the presses itself) */
            pr = input_pressed();
            held = pad(in);
            pressed = pad(pr);
            if (key_down(IN_KEY_SCANCODE_s)) held |= B_C;
            if (pr && key_down(IN_KEY_SCANCODE_s)) pressed |= B_C;
            if (G.state == ST_PLAYING && !G.demo && input_letter_pressed() == 'B') {
                autopilot = !autopilot;
                zx_log("rs: autopilot %u\n", autopilot);
            }
        }
        /* port: game over or all clear with a high score: the initials first */
        if (hud_entry) {
            uint8_t r = hs_edit_step(&ed);
            memcpy(hud_initials, ed.name, 4);
            hud_initials_pos = ed.pos;
            if (r & HS_EDIT_TAKEN) snd_play(SFX_CLACK);
            if (r & HS_EDIT_DONE) {
                hs_insert(ed_place, ed.name, hud_score);
                zx_log("rs: hiscore %s %lu\n", ed.name, hud_score);
                hud_entry = 0;
                pressed = B_A;                   /* on to the title, as the original's A */
            } else
                held = pressed = 0;
        } else if ((G.state == ST_GAMEOVER || G.state == ST_WON) && !G.demo && G.state_timer > 50 &&
                   (pressed & (B_A | B_P))) {
            ed_place = hs_rank(hud_score);
            if (ed_place >= 0) {
                hs_edit_begin(&ed);
                memcpy(hud_initials, ed.name, 4);
                hud_initials_pos = 0;
                hud_entry = 1;
                zx_log("rs: initials rank=%d score=%lu\n", ed_place, hud_score);
                held = pressed = 0;
            }
        }
        if (G.state == ST_TITLE) autopilot = 0;
        perf_frames += t;
        perf_loops++;
        for (k = 0; k < t; k++) {
            game_step(held, pressed, 0, 0);
            pressed = 0;
            if (hashing) hash = rs_hash(hash);
            steps++;
            perf_steps++;
            if (trace && G.state == ST_PLAYING)
                zx_log("tr %lu %ld %ld %ld %ld %ld %ld g%d\n", steps, G.p[0].m.p.x, G.p[0].m.p.y, G.p[0].m.p.z,
                       G.p[0].m.v.x, G.p[0].m.v.y, G.p[0].m.v.z, G.p[0].m.grounded);
            score_step();
            snd_frame();
        }
        view_frame();
        view_sprites();
        hud_frame();
        if (perf_frames >= 100) {
            zx_log("rs: perf frames=%u real=%u loops=%u steps=%u cols=%u rows=%u\n", perf_frames, perf_real,
                   perf_loops, perf_steps, view_cols, view_rows);
            zx_log("rs: phys tri=%u sph=%u plane=%u edge=%u segfull=%u contact=%u\n", pc_tri, pc_sph, pc_plane,
                   pc_edge, pc_segfull, pc_contact);
            pc_tri = pc_sph = pc_plane = pc_edge = pc_segax = pc_segfull = pc_contact = 0;
            perf_frames = perf_loops = perf_steps = perf_real = 0;
            view_cols = view_rows = 0;
        }
    }
    return 0;
}
