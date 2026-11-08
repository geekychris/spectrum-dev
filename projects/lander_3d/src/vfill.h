/*
 * vfill.h - the view's drawing (vfill.c), on the Layer 2 back buffer's top
 * VROWS rows (shown twice each by the copper: the original's 216-row view
 * at 0.8, on 184 lines of the display).
 *
 * port: the original sorts ~360 polygons of the ground, the pads and the
 * lander into depth buckets and fills them far to near. Here the ground is
 * a height field seen by a camera that never turns (yaw 0): its strips
 * (a row of cells between two grid lines) are drawn near to far, each
 * column of the screen keeping the highest row drawn so far (vf_ybuf),
 * so each pixel is written once and a hill hides what is behind it as in
 * the original's picture. The pads, the shadow and the lander are drawn
 * after the ground, over it, each clipped by a copy of vf_ybuf made when
 * the strips nearer than it had been drawn (what is in front of it).
 *
 * Coordinates: VfVtx records (below) for the ground and the polygons.
 * In page 47 at 0x6000 (MMU slot 3) with its data; vf_draw() maps the back
 * buffer into slots 0-2 while it draws and puts the view's bank back.
 */
#ifndef VFILL_H
#define VFILL_H
#include <stdint.h>

#define VW       256
#define VROWS    92                      /* rows drawn (184 lines shown) */
#define GW       6                       /* ground cells each side of the middle: port: the original has 9 (68k), 20 (AmigaOS 4) */
#define NVX      (2 * GW + 2)            /* grid points a line */
#define NLINE    (2 * GW + 2)            /* grid lines: NLINE - 1 strips */
#define VF_NONE  0xFF                    /* a cell not drawn */
#define VF_SNAPS 4
#define VF_MAXPOLY 48
#define VF_MAXPT   200                   /* vertices of all the polygons */
#define VF_NSTARS  70
#define VF_OFF     32                    /* vf_y and vf_ybuf: rows + 32 */

typedef struct { uint8_t n, pen, snap; uint16_t first; } VfPoly;     /* snap >= VF_SNAPS: no clip */

/* the ground: line 0 nearest; a strip k between lines k and k + 1 */
/* a grid point on the screen: x in pixels (-256..511, VF_BEHIND: behind the
   camera), y = (row + VF_OFF) * 256 (256..65280) */
typedef struct { int16_t x; uint16_t y; } VfVtx;
#define VF_BEHIND ((int16_t)0x8000)
extern VfVtx vf_v[NLINE][NVX];
extern uint8_t vf_col[NLINE - 1][NVX - 1];
extern uint8_t vf_nlines;
extern uint8_t vf_snap_at[VF_SNAPS];     /* copy vf_ybuf before this strip (>= strips: at the end) */
/* the rest of the picture */
extern uint8_t vf_sky, vf_plain, vf_hz;  /* pens above and below the horizon (row vf_hz) */
extern uint8_t vf_star_x[VF_NSTARS], vf_star_y[VF_NSTARS], vf_star_pen[VF_NSTARS];
extern uint8_t vf_nstars;
extern VfPoly vf_poly[VF_MAXPOLY];
extern VfVtx vf_pv[VF_MAXPT];
extern uint8_t vf_npoly;
extern uint16_t vf_npt;
extern uint8_t vf_ybuf[VW];              /* each column's highest row drawn + VF_OFF */
extern uint8_t vf_snap[VF_SNAPS][VW];
extern uint8_t vf_tmp[VW], vf_full[VW];  /* a polygon's bottoms; VROWS + VF_OFF everywhere */
void vf_ground(void);
/* a polygon's edge (a.x < b.x): mode 0 fills down to vf_tmp, 1 writes it */
void vf_seg(const VfVtx *a, const VfVtx *b, uint8_t pen, uint8_t mode);
#ifdef __SDCC
void vf_poly_page(uint8_t snap_page);   /* the clip for mode 1 (a 256-byte page) */
#endif
extern uint8_t vf_bank0;                 /* the 16K bank to give slots 0-1 back (the view's code) */

void vf_draw(void);                      /* sky, stars, ground, then the polygons in order */
uint8_t vf_add_poly(const VfVtx *v, uint8_t n, uint8_t pen, uint8_t snap);   /* 0: no room */
void vf_clear_polys(void);

/* the target: the Next's back buffer (vfill.c) or the host's picture */
#ifdef __SDCC
#define VF_FB ((uint8_t *)0x0000)
#else
extern uint8_t vf_host_fb[VROWS * VW];
#define VF_FB vf_host_fb
#endif

#endif
