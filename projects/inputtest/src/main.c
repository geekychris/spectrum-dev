/*
 * inputtest - logs the raw input hardware, for the harness's input tests.
 *
 * Every frame it reads the eight keyboard half-rows (ports xxFE) and the
 * Kempston joystick (port 0x1F) and logs any change:
 *   "inputtest: row 7F=1e"   (bit = 0 means pressed, 5 bits)
 *   "inputtest: kempston=10" (bit = 1 means pressed: R L D U F)
 * Sinclair and Cursor joysticks show up as number keys in the rows.
 */
#include <intrinsic.h>
#include <z80.h>
#include <arch/zx.h>
#include "zxlog.h"
#include "zxtext.h"

static const unsigned char rows[8] = {0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F};
static unsigned char last[8], last_k;
static unsigned int frames;

int main(void)
{
    unsigned char i;
    zx_border(INK_WHITE);
    zx_cls(PAPER_WHITE | INK_BLACK);
    zx_print_at(1, 1, "inputtest: keys + joystick");
    for (i = 0; i < 8; i++) last[i] = 0x1F;
    zx_log("inputtest: ready\n");
    intrinsic_ei();
    for (;;) {
        unsigned char k;
        intrinsic_halt();
        frames++;
        for (i = 0; i < 8; i++) {
            unsigned char v = z80_inp(((unsigned int)rows[i] << 8) | 0xFE) & 0x1F;
            if (v != last[i]) {
                zx_log("inputtest: row %02x=%02x f=%u\n", rows[i], v, frames);
                last[i] = v;
            }
        }
        k = z80_inp(0x001F) & 0x1F;
        if (k != last_k) {
            zx_log("inputtest: kempston=%02x f=%u\n", k, frames);
            last_k = k;
            zx_border(k ? INK_RED : INK_WHITE);
        }
    }
}
