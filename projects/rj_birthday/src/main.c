/*
 * RJ's Birthday Bash for the Spectrum Next (the original's main.c and
 * input.c): start-up, the 50 Hz loop, the keys, the screens.
 *
 * Memory: the program at 0x8000; game.c (the rules, unchanged but for the
 * port: notes) in bank 24 at 0x0000, mapped for good after the ROM's
 * characters are copied (and screens.c in bank 26 there in its place
 * while it's called: SCREENS); three of its room updates (game_rooms.c) in page
 * 50 at 0x6000, which Layer 2 drawing borrows and puts back (l2w_restore).
 * The tilemap is in bank 5 (0x4000, its definitions to 0x6060: they are
 * read by the video hardware from bank 5 itself, not through slot 3).
 * The pictures: banks 16..22 (the house and the title, column RLE), 23
 * (sprite patterns); Layer 2 is banks 9..13.
 *
 * The original runs game_update once a PAL frame; so does the port, a
 * frame behind at most 3 frames when drawing is slow (the camera's
 * column copying), so the game keeps the original's speed.
 *
 * port: keys. Q A O P, the cursor keys, Sinclair and Kempston move; SPACE,
 * M, 0 or fire is the original's fire; BREAK (CAPS SPACE) its Esc; ENTER
 * its Return; DELETE (CAPS 0) its Backspace and Del; the letter keys type
 * (H help and E the guest list, as the original). The original's Q on the
 * title quits: there's nothing to quit to (and Q is up).
 */
#include <stdint.h>
#include <string.h>
#include <input.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nextspr.h"
#include "nexttile.h"
#include "l2pic.h"
#include "game.h"
#include "gfx_data.h"
#include "view.h"
#include "screens.h"
#include "sound.h"

#define CODE_BANK  24                     /* game.c: pages 48, 49 at 0x0000 */
#define ROOMS_PAGE 50                     /* game_rooms.c at 0x6000 */
#define SCREENS_BANK 26                   /* screens.c: pages 52, 53 at 0x0000, in place of game.c while called */

/* call screens.c (it uses nothing in game.c) */
#define SCREENS(call) do { ZXN_WRITE_REG(0x50, SCREENS_BANK * 2); ZXN_WRITE_REG(0x51, SCREENS_BANK * 2 + 1); call; \
                           ZXN_WRITE_REG(0x50, CODE_BANK * 2); ZXN_WRITE_REG(0x51, CODE_BANK * 2 + 1); } while (0)

static GameState gs;
static InputState inp;
static uint8_t prev_keys;

/* the original's input_read: held bits, the fire edge, the typing edges */
static void read_input(void)
{
    uint8_t in = input_read(), p = input_pressed();
    uint8_t caps = key_down(IN_KEY_SCANCODE_CAPS), del = caps && key_down(IN_KEY_SCANCODE_0);
    char ch = input_letter_pressed();
    UWORD prev = inp.prev_fire;
    inp.bits = 0;
    if (in & IN_LEFT) inp.bits |= INP_LEFT;
    if (in & IN_RIGHT) inp.bits |= INP_RIGHT;
    if (in & IN_UP) inp.bits |= INP_UP;
    if (in & IN_DOWN) inp.bits |= INP_DOWN;
    if ((in & IN_FIRE) && !caps) inp.bits |= INP_FIRE;      /* CAPS SPACE and CAPS 0 aren't fire */
    if (in & IN_QUIT) inp.bits |= INP_ESC;
    inp.fire_edge = (inp.bits & INP_FIRE) && !prev;
    inp.prev_fire = (inp.bits & INP_FIRE) ? 1 : 0;
    inp.key_up = (p & IN_UP) != 0;
    inp.key_down = (p & IN_DOWN) != 0;
    inp.key_return = (p & IN_START) != 0;
    inp.key_backspace = inp.key_delete = del && !(prev_keys & 1);
    prev_keys = del;
    inp.last_char = ch && !caps ? (UBYTE)(ch - 'A' + 'a') : 0;
}

static const char *const state_names[] = { "title", "playing", "gameover", "credits", "win", "hiscore",
                                           "enter_name", "add_guest", "help", "guest_edit", "jail" };

static uint8_t shown_state = 0xFF;

static void enter_state(void)
{
    shown_state = (uint8_t)gs.state;
    zx_log("rj: state %s score=%ld lives=%d room=%d\n", gs.state <= GS_JAIL ? state_names[gs.state] : "?",
           (long)gs.score, gs.lives, gs.current_room);
    if (gs.state == GS_PLAYING) {
        view_hide_sprites();
        tm_clear();
        tm_scroll(0, 0);
        view_wtext_clear();
        SCREENS(hud_reset());
        view_house();
        view_camera(gs.camera_x);
    } else
        SCREENS(screen_enter(&gs));
    music_for_state(gs.state);
}

static int32_t log_score;
static int16_t log_lives, log_room;

static void log_play(void)
{
    if (gs.score != log_score || gs.lives != log_lives || gs.current_room != log_room) {
        log_score = gs.score;
        log_lives = gs.lives;
        log_room = gs.current_room;
        zx_log("rj: play score=%ld lives=%d room=%d clock=%d\n", (long)gs.score, gs.lives, gs.current_room,
               gs.party_clock);
    }
}

int main(void)
{
    uint8_t frames, k, steps;
    uint16_t perf_frames = 0, perf_loops = 0;
    memcpy(rom_font, (const uint8_t *)0x3D00, 768);
    ZXN_WRITE_REG(0x50, CODE_BANK * 2);
    ZXN_WRITE_REG(0x51, CODE_BANK * 2 + 1);
    ZXN_WRITE_REG(0x53, ROOMS_PAGE);
    l2w_restore = ROOMS_PAGE;
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x13);            /* sprites on, over the border; order U S L: text over all */
    pal_set8(0, 16, L2_TRANSPARENT);      /* the fallback colour (black) */
    zx_border(INK_BLACK);
    view_init();
    view_text_init();
    snd_init();
    input_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    game_init_tables();
    game_load_names(&gs, "");
    game_load_hiscores(&gs, "");
    gs.state = GS_TITLE;
    zx_log("rj: ready kempston=%u\n", input_kempston);
    for (;;) {
        frames = frame_wait();
        steps = frames > 3 ? 3 : frames;
        perf_frames += frames;
        perf_loops++;
        if (perf_frames >= 100) {
            zx_log("rj: perf loops=%u frames=%u copied=%u ay=%u,%u,%u,%u\n", perf_loops, perf_frames, l2pic_copied,
                   snd_reg(0, 8) & 15, snd_reg(0, 9) & 15, snd_reg(1, 8) & 15, snd_reg(1, 9) & 15);
            perf_frames = perf_loops = 0;
            l2pic_copied = 0;
        }
        for (k = 0; k < steps; k++) {
            read_input();
            if ((inp.bits & INP_ESC) && gs.state == GS_PLAYING) {   /* the original's Esc in play: the credits */
                gs.state = GS_CREDITS;
                gs.credits_scroll = 0;
            } else
                game_update(&gs, &inp);
            snd_tick();
            if (gs.state != shown_state) break;
        }
        if (gs.state != shown_state) enter_state();
        if (gs.state == GS_PLAYING) {
            view_camera(gs.camera_x);
            view_sprites(&gs);
            SCREENS(world_text(&gs); hud_draw(&gs));
            log_play();
        } else
            SCREENS(screen_frame(&gs));
    }
    return 0;
}
