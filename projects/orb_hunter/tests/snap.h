/* What host_diff.c compares after every step: the game state of the port
   (port_side.c) and of the original (orig_side.c) in one layout */
#include <stdint.h>
typedef struct { int32_t x, y, dx, dy; int16_t a, b, c, d; } SnapObj;
typedef struct {
    int32_t hx, hy, hdx, hdy, health, max_energy, missiles, score, room_x, room_y, items;
    int16_t on_ground, facing, alive, rolled, gun, invuln, state, state_timer, frame;
    SnapObj bullets[6], bombs[3], enemies[12], ebullets[6], particles[24];
    uint8_t room[16][20];
} Snap;
void port_init(void);
void port_update(int l, int r, int u, int d, int j, int f);
void port_snap(Snap *s);
void port_give_all(void);
void orig_init(void);
void orig_update(int l, int r, int u, int d, int j, int f);
void orig_snap(Snap *s);
void orig_give_all(void);
void port_resume(void);   /* STATE_ITEM_GET -> STATE_PLAYING on both (main.c) */
