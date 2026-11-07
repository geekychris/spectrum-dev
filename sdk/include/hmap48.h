/*
 * hmap48.h - heightmap terrain on the 48K screen: a landscape given as the
 * surface height of each of the 256 pixel columns (h[x] = screen y of the
 * surface, smaller = higher), drawn once (a level's scenery) and coloured by
 * character cell. Uranus Lander's terrain is this.
 *
 *   hmap_line(h, dy)                    the surface as a continuous line,
 *                                       dy lines below it (0 = the surface)
 *   hmap_fill(h, dy, bottom, pattern)   ORs pattern[y & 7] (dither) into every
 *                                       pixel from h[x] + dy down to `bottom`
 *   hmap_colour(h, row0, row1, sky, surface, ground, attrs)
 *       colours character rows row0..row1: cells wholly above the surface get
 *       `sky`, cells the surface passes through `surface`, cells wholly below
 *       `ground`. attrs = the attribute map (0x5800, or a shadow copy laid out
 *       the same: 32 bytes a row).
 *
 * The fill works a byte (8 columns) at a time: whole bytes below the lowest
 * of the 8 surfaces, a mask only between the highest and the lowest.
 */
#ifndef ZX_HMAP48_H
#define ZX_HMAP48_H
#include <stdint.h>
extern void hmap_line(const uint8_t *h, uint8_t dy);
extern void hmap_fill(const uint8_t *h, uint8_t dy, uint8_t bottom, const uint8_t *pattern);
extern void hmap_colour(const uint8_t *h, uint8_t row0, uint8_t row1,
                        uint8_t sky, uint8_t surface, uint8_t ground, uint8_t *attrs);
#endif
