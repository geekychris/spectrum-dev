/* port check: game.c's fast angle_to against the original's scan
   (angle_to_scan), on random and boundary vectors. Host build:
   cc -I src -I ../../sdk/include tools/check_angle.c src/trig.c src/models.c ../../sdk/src/fix88.c ../../sdk/src/fix88_tab.c */
#include <stdio.h>
#include <stdlib.h>
void sfx_gunfire(void) {}
void sfx_explosion(void) {}
void sfx_hit(void) {}
void sfx_die(void) {}
#include "../src/game.c"

int main(void)
{
    long n = 0, bad = 0;
    int dx, dz;
    srand(1);
    for (dx = -1200; dx <= 1200; dx += 3)            /* every direction, short and mid vectors */
        for (dz = -1200; dz <= 1200; dz += 7) {
            n++;
            if (angle_to(dx, dz) != angle_to_scan(dx, dz)) {
                if (bad++ < 10) printf("dx=%d dz=%d fast=%d scan=%d\n", dx, dz, angle_to(dx, dz), angle_to_scan(dx, dz));
            }
        }
    for (; n < 3000000; n++) {                       /* the game's ranges: +-4000 */
        LONG x = (rand() % 8001) - 4000, z = (rand() % 8001) - 4000;
        if (angle_to(x, z) != angle_to_scan(x, z)) {
            if (bad++ < 20) printf("dx=%ld dz=%ld fast=%d scan=%d\n", (long)x, (long)z, angle_to(x, z), angle_to_scan(x, z));
        }
    }
    printf("%ld vectors, %ld differ\n", n, bad);
    return bad != 0;
}
