/*
 * voxel.h - height-map terrain drawn column by column, front to back, on
 * Layer 2's 256 x 192 back buffer (nextgfx.h's double buffer): the "voxel
 * space" way 1990s flight and racing games drew landscapes. Each column of
 * the screen walks out from the camera in slices; at each slice the
 * ground's height is looked up and projected, and if it rises above what
 * the column shows so far the rows between are filled with the ground's
 * colour there (a y-buffer: nothing is drawn twice). The cost depends on
 * the columns and slices, not on how complicated the ground is.
 *
 * World: x across, z ahead, y up, in units the game chooses. The ground is
 * described by maps that are rings (positions wrap), so a game streams in
 * the ground ahead as the camera moves:
 *
 *   heights   int8, 128 x 128 cells, row-major (row = z), each cell
 *             1 << hshift units square; a cell's height is value << hscale
 *             units
 *   colours   a Layer 2 pen per (1 << csh) x (1 << csh) cells (a
 *             (128 >> csh)-square map): e.g. one shade per hill face
 *   ribbon    optional: a road, river or track painted over the colours,
 *             sharp at any distance: for each height row, the ribbon's middle
 *             (x, in cells * 256) and a stripe bit; bands out from the
 *             middle, each a half width (cells * 256) and two pens (stripe
 *             0 and 1; a sided band swaps them left of the middle). The
 *             first band a point is inside gives its pen.
 *
 *   vox_world(hshift, hscale)          the cell size and the height scale
 *   vox_maps(heights, colours, csh)    the maps (they must stay mapped while
 *                                      vox_draw runs: not in MMU slot 3)
 *   vox_ribbon(mid, stripe, bands, n)  the ribbon (mid = 0: none)
 *   vox_camera(x, y, z, yaw)           position (world units) and heading
 *                                      (16-bit angle: 0 = +z, 16384 = +x)
 *   vox_lens(fovx, fovy, horizon, rows) pixels per unit at distance 1 across
 *                                      and down, the horizon's row (pitch as
 *                                      a shear), the rows drawn from the top
 *                                      (96 with l2double.h's line doubling)
 *   vox_depth(near, far, grow)         slices from near to far units, each
 *                                      1 + d / grow further than the last
 *   vox_snap_at(k, depth)              keep a copy of the y-buffer as it was
 *                                      when the slices reached depth (k <
 *                                      VOX_SNAPS): ground nearer than that
 *                                      hides rows from vox_snap[k][col] down
 *                                      (a sprite standing there); 0 = off
 *   vox_draw()                         the ground into the back buffer,
 *                                      VOX_COLS columns of 2 pixels; rows
 *                                      above vox_ybuf[col] are left alone
 *   vox_sky(pen)                       fill above vox_ybuf with a pen
 *   vox_project(x, y, z, &sx, &sy, &d) a world point to the screen (sx 0..255,
 *                                      sy in rows drawn, d its depth); 0 if
 *                                      nearer than the first slice
 *
 * The C (voxel.c) is the reference and builds on the host (draws into
 * vox_host_fb there); on the Next the slice loop is voxel_draw.asm, with
 * the same arithmetic. MMU slot 3 is used to reach Layer 2 while drawing.
 * Test: projects/voxeltest.
 */
#ifndef VOXEL_H
#define VOXEL_H

#include <stdint.h>

#define VOX_COLS   128                  /* columns of 2 pixels */
#define VOX_MAP    128                  /* the height map is VOX_MAP x VOX_MAP */
#define VOX_SNAPS  4
#define VOX_SLICES 64
#define VOX_BANDS  4

/* a ribbon band: inside half (cells * 256) of the middle; pen[stripe], or
   with sided set pen[stripe ^ (left of the middle)] */
typedef struct { int16_t half; uint8_t pen[2]; uint8_t sided; } VoxBand;

extern uint8_t vox_ybuf[VOX_COLS];
extern uint8_t vox_snap[VOX_SNAPS][VOX_COLS];

extern void vox_world(uint8_t hshift, uint8_t hscale);
extern void vox_maps(const int8_t *heights, const uint8_t *colours, uint8_t csh);
extern void vox_ribbon(const int16_t *mid, const uint8_t *stripe, const VoxBand *bands, uint8_t n);
extern void vox_camera(int32_t x, int32_t y, int32_t z, uint16_t yaw);
extern void vox_lens(int16_t fovx, int16_t fovy, int16_t horizon, uint8_t rows);
extern void vox_depth(int16_t d_near, int16_t d_far, uint8_t grow);
extern void vox_snap_at(uint8_t k, int16_t depth);
extern uint8_t vox_draw(void);           /* returns the slices drawn */
extern void vox_sky(uint8_t pen);
extern uint8_t vox_project(int32_t x, int32_t y, int32_t z, int16_t *sx, int16_t *sy, int16_t *depth);

#ifndef __SDCC
extern uint8_t *vox_host_fb;             /* host builds: 256 x rows, row-major */
#endif

#endif
