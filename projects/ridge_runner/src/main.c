/* Ridge Runner for the Spectrum Next (work in progress: the view's timing) */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "l2poly.h"
#include "zxlog.h"
#include "rr.h"
#include "view.h"

static RRGame g;
#define RULES() do { ZXN_WRITE_MMU0(78); ZXN_WRITE_MMU1(79); } while (0)
#define VIEW()  do { ZXN_WRITE_MMU0(80); ZXN_WRITE_MMU1(81); } while (0)

static uint16_t raster(void) { return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F); }
/* raster lines since (frame tick f0, line t0); lines count from the interrupt (248) */
static uint8_t f0;
static uint16_t mark(void) { uint16_t t = raster(); f0 = frame_ticks; return t >= 248 ? t - 248 : t + 64; }
static uint16_t lines(uint16_t t0) { uint16_t t = raster(); t = t >= 248 ? t - 248 : t + 64; return (uint16_t)(frame_ticks - f0) * 312 + t - t0; }

static void fill(const int16_t *xy, uint8_t n, uint8_t pen) { l2p_poly(xy, n, pen); }

int main(void)
{
    ViewIn in;
    int32_t cam_yaw;
    uint16_t f, t0, tb, td, tl, sb = 0, sd = 0, sl = 0, n = 0;
    next_init();
    l2_init();
    frame_init();
    RULES();
    rr_new_game(&g, 5, 1);
    VIEW();
    view_new_track(&g);
    cam_yaw = g.yaw;
    l2p_top = 0;
    l2p_bot = VIEW_H;
    zx_log("rr: ready\n");
    for (f = 0; f < 400; f++) {
        uint8_t k;
        frame_wait();
        t0 = mark();
        RULES();
        for (k = 0; k < 2; k++) {
            rr_tick(&g, rr_bot(&g));
            cam_yaw = (cam_yaw + (int32_t)(int16_t)(g.yaw - cam_yaw) / 4) & 0xFFFF;
        }
        tl = lines(t0);
        t0 = mark();
        VIEW();
        in.cam_yaw = cam_yaw;
        in.show_player = 1;
        n = view_build(&g, &in);
        tb = lines(t0);
        t0 = mark();
        l2p_begin();
        view_draw(fill);
        l2p_end();
        td = lines(t0);
        frame_wait();
        l2_flip();
        sb += tb; sd += td; sl += tl;
        if ((f & 7) == 7) {
            zx_log("rr: z=%ld polys=%u lines/frame logic2=%u build=%u draw=%u\n", (long)(g.z >> 16), n, sl / 8, sb / 8, sd / 8);
            sb = sd = sl = 0;
        }
    }
    zx_log("rr: done\n");
    for (;;) frame_wait();
}
