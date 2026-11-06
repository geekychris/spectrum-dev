/*
 * __NAME__ - made from sdk/template (./zx new).
 *
 * Main-loop skeleton for 48K (TARGET := zx) and Next (TARGET := zxn):
 * 50 Hz loop on HALT, keyboard (Q A O P, SPACE) and Kempston joystick input,
 * a block you can move, and zx_log lines the harness and test.py wait for.
 */
#include <stdio.h>
#include <input.h>
#include <intrinsic.h>
#ifdef __ZXNEXT
#include <arch/zxn.h>          /* includes everything in arch/zx.h, plus Next registers */
#else
#include <arch/zx.h>
#endif
#include "zxlog.h"
#include "zxtext.h"

static unsigned int frames;
static unsigned char px = 15, py = 12;       /* player cell */
static unsigned char last_fire;

static void draw_player(unsigned char attr)
{
    zx_attr_at(px, py, 2, attr);
    zx_attr_at(px, py + 1, 2, attr);
}

int main(void)
{
#ifdef __ZXNEXT
    ZXN_WRITE_REG(0x07, 0x03);               /* 28 MHz */
#endif
    zx_border(INK_BLACK);
    zx_cls(PAPER_BLACK | INK_WHITE);
    zx_print_at(1, 1, "__NAME__");
    zx_print_at(1, 22, "QAOP/joystick move, SPACE fire");
    zx_attr_at(1, 1, 30, PAPER_BLACK | INK_YELLOW | BRIGHT);
    draw_player(PAPER_GREEN | BRIGHT);

    zx_log("__NAME__: ready\n");
    intrinsic_ei();                           /* ROM IM1 interrupt: HALT = vsync */

    for (;;) {
        unsigned int stick;
        unsigned char nx = px, ny = py, fire;

        intrinsic_halt();
        frames++;

        stick = in_stick_kempston();
        if ((stick & IN_STICK_LEFT) || in_key_pressed(IN_KEY_SCANCODE_o)) nx--;
        if ((stick & IN_STICK_RIGHT) || in_key_pressed(IN_KEY_SCANCODE_p)) nx++;
        if ((stick & IN_STICK_UP) || in_key_pressed(IN_KEY_SCANCODE_q)) ny--;
        if ((stick & IN_STICK_DOWN) || in_key_pressed(IN_KEY_SCANCODE_a)) ny++;
        fire = (stick & IN_STICK_FIRE) || in_key_pressed(IN_KEY_SCANCODE_SPACE);

        if (nx > 30) nx = px;                 /* also catches 0 - 1 = 255 */
        if (ny < 3 || ny > 19) ny = py;
        if (nx != px || ny != py) {
            draw_player(PAPER_BLACK | INK_WHITE);
            px = nx; py = ny;
            draw_player(PAPER_GREEN | BRIGHT);
            zx_log("__NAME__: move x=%u y=%u\n", px, py);
        }
        if (fire && !last_fire) {
            zx_border(INK_RED);
            zx_log("__NAME__: fire frame=%u\n", frames);
        }
        else if (!fire && last_fire) {
            zx_border(INK_BLACK);
        }
        last_fire = fire;
    }
}
