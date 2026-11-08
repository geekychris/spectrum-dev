/*
 * The course loader (the original's course.c). port: the courses are made
 * on the host (tools/gen_course.py) straight into the form the physics
 * reads - a header page of small tables, then the triangles, then the
 * collision grid, each from a page boundary (see gen_course.course_blob) -
 * so loading copies the small tables and notes where the rest is.
 */
#include <string.h>
#include <stddef.h>
#include <arch/zxn.h>
#include "rs.h"
#include "data.h"
#include "zxlog.h"

Course C;
int course_count = 6;
static PathPt path_buf[MAXPATH];
CrumbleSpec crumble_spec[MAXCRUMBLE];

/* phys.asm's layouts (rsmac.inc) must be zsdcc's */
#define CHECK(n, c) typedef char check_##n[(c) ? 1 : -1]
CHECK(b_r, offsetof(Ball, r) == 36);
CHECK(b_gr, offsetof(Ball, grounded) == 60);
CHECK(b_lg, offsetof(Ball, last_ground) == 66);
CHECK(b_kill, offsetof(Ball, kill) == 84);
CHECK(b_size, sizeof(Ball) == 88);
CHECK(cr_size, sizeof(Crumble) == 10);
CHECK(e_kind, offsetof(Enemy, kind) == 100);
CHECK(e_phase, offsetof(Enemy, phase) == 118);
CHECK(e_size, sizeof(Enemy) == 122);
CHECK(w_nenemy, offsetof(World, nenemy) == 1296);
CHECK(w_clock, offsetof(World, clock) == 1410);
CHECK(w_size, sizeof(World) == 1414);
CHECK(zone, sizeof(Zone) == 52);

static const uint8_t *cur;
static void take(void *dst, uint16_t n) { memcpy(dst, cur, n); cur += n; }

int course_load_b(int index)
{
    uint8_t n[8], pg;
    uint16_t nt, np;
    if (index < 1 || index > course_count) return 0;
    pg = course_page[index - 1];
    C.page = pg;
    ZXN_WRITE_MMU2(pg);
    cur = (const uint8_t *)0x4000;
    take(&nt, 2);
    take(&np, 2);
    take(&phy_gx0, 4);
    take(&phy_gz0, 4);
    take(&phy_gnx, 2);
    take(&phy_gnz, 2);
    phy_tpage = pg + cur[0];
    phy_gpage = pg + cur[1];
    phy_ipage = pg + cur[2];
    cur += 3;
    take(&C.time_ms, 4);
    take(&C.gold_ms, 4);
    take(&C.silver_ms, 4);
    take(&C.bronze_ms, 4);
    take(&C.music, 4);
    take(&C.spawn, 12);
    take(&C.goal_world, 12);
    take(&C.kill_y, 4);
    phy_kill_y = C.kill_y;
    take(&phy_goal, sizeof(OBB));
    take(n, 8);
    phy_nacid = n[0]; phy_nzone = n[1]; phy_npillar = n[2]; phy_nsweep = n[3];
    phy_ncrush = n[4]; phy_nenemy = n[5]; phy_ncrumble = n[6];
    take(phy_acid, n[0] * sizeof(OBB));
    take(phy_zone, n[1] * sizeof(Zone));
    take(phy_pillar, n[2] * sizeof(Pillar));
    take(phy_sweep, n[3] * sizeof(Sweeper));
    take(phy_crush, n[4] * sizeof(Crusher));
    take(phy_enemy, n[5] * sizeof(EnemySpec));
    take(crumble_spec, n[6] * sizeof(CrumbleSpec));
    if (np > MAXPATH) np = MAXPATH;
    take(path_buf, np * sizeof(PathPt));
    C.npath = np;
    C.path = path_buf;
    take(C.name, 33);
    ZXN_WRITE_MMU2(10);
    zx_log("course %d %s tris=%u path=%u page=%u\n", index, C.name, nt, np, pg);
    return 1;
}
