/*
 * export.c - runs the original game's own renderer (tools/orig: vox.c, the
 * level data, the room builder) on the host and writes what the Next needs:
 *
 *   build/host/sprites.bin   every model frame vox_init() renders
 *   build/host/room_<l>_<r>.bin
 *                            the room's pieces (vox_room(): floor tiles,
 *                            wall bricks, door steps, blocks, arches, plus
 *                            the spikes, gate and throne models) and its
 *                            draw list: the original scene.c's order for
 *                            the floor and back walls, then the static items
 *                            in its dependency order, then the front arches
 *   build/host/ref_<l>_<r>.bin
 *                            the original scene.c's own picture of the room
 *                            with nothing moving in it (320 x 240 RGB15),
 *                            to check the Next's picture against
 *
 * File format (little endian): a sprite is
 *   u16 w, h; s16 ax, ay; w*h u16 RGB15 (0 = transparent)
 * sprites.bin = u16 count, then the sprites in vox_init() order.
 * room = u16 npieces, pieces; u16 nops, ops of
 *   u8 piece, u8 kind (0 floor/wall, 1 static item, 2 gate, 3 arch),
 *   s16 x, y (top-left on the 320 x 240 frame), s16 cell x, y, z (items)
 * then the room's frame origin s16 ox, oy.
 *
 *   cc -O2 -Itools/orig -o build/host/export tools/export.c tools/orig/vox.c \
 *      tools/orig/textures.c tools/orig/levels.c tools/orig/room.c \
 *      tools/orig/world.c tools/orig/scene.c -DSCENE_RGB15
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "keep.h"

/* what the original's room.c / game.c call back into */
int host_taken(int ridx, int cell) { (void)ridx; (void)cell; return 0; }
int host_gate_open(int ridx) { (void)ridx; return 0; }
void host_pickup(int kind, int ridx, int cell) { (void)kind; (void)ridx; (void)cell; }
int host_try_key(int ridx) { (void)ridx; return 0; }
void host_death(const char *c) { (void)c; }
void host_exit(int s, fix x, fix y, fix z) { (void)s; (void)x; (void)y; (void)z; }
void host_throne(void) {}
void host_talk(const char *t) { (void)t; }
void host_sound(int s) { (void)s; }
void keep_log(const char *fmt, ...) { (void)fmt; }

/* scene.c (original, SCENE_RGB15) */
extern unsigned short *scene_fb;
extern int scene_ox, scene_oy;
void scene_init(void);
void scene_room(void);
void *scene_frame(int show_player, int panel, int flash_ink);

static FILE *out;
static void w16(int v) { fputc(v & 255, out); fputc((v >> 8) & 255, out); }
static void wsprite(const Sprite *s)
{
    int i;
    w16(s->pix ? s->w : 0);
    w16(s->pix ? s->h : 0);
    w16(s->ax);
    w16(s->ay);
    if (s->pix)
        for (i = 0; i < s->w * s->h; i++) w16(s->pix[i]);
}

static FILE *open_out(const char *dir, const char *fmt, ...)
{
    char path[256], name[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(name, sizeof(name), fmt, ap);
    va_end(ap);
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    out = fopen(path, "wb");
    if (!out) { perror(path); exit(1); }
    return out;
}

/* ---- the room: pieces and the draw list ---- */

enum { P_TILE = 0, P_BRICK = 4, P_STEP = 8, P_BLOCK = 10, P_ARCH = 16, P_SPIKES = 18,
       P_GATE = 19, P_THRONE = 21, P_COUNT = 22 };
static const Sprite *piece[P_COUNT];

typedef struct { int p, kind, x, y, cx, cy, cz; } Op;
static Op ops[2000];
static int nops;

static void op(int p, int kind, long x, long y, int cx, int cy, int cz)
{
    const Sprite *s = piece[p];
    Op *o = &ops[nops++];
    o->p = p;
    o->kind = kind;
    o->x = (int)(x - s->ax);
    o->y = (int)(y - s->ay);
    o->cx = cx; o->cy = cy; o->cz = cz;
}

#define BX(x, z) (scene_ox + ISO_X * ((x) - (z)))
#define BY(x, y, z) (scene_oy - ISO_Y * (y) - ISO_Z * ((x) + (z)))

/* scene.c's background(), emitting ops instead of blitting */
static void background(void)
{
    const KRoom *r = R.room;
    int x, y, z, k, door[2], lo[2], hi[2];
    door[0] = lv_has_door(R.level, R.ridx, SIDE_N);
    door[1] = lv_has_door(R.level, R.ridx, SIDE_E);
    door_span(r->w, &lo[0], &hi[0]);
    door_span(r->d, &lo[1], &hi[1]);
    for (k = r->w + r->d; k >= 0; k--)
        for (y = 0; y < 3; y++) {
            if (k == r->w + r->d) op(P_BRICK + VARIANTS / 2, 0, BX(r->w, r->d), BY(r->w, y, r->d), 0, 0, 0);
            x = k - r->d;
            if (x >= 0 && x < r->w && !(door[0] && x >= lo[0] && x < hi[0] && y < 2))
                op(P_BRICK + brick_variant(x, y, 0), 0, BX(x, r->d), BY(x, y, r->d), 0, 0, 0);
            z = k - r->w;
            if (z >= 0 && z < r->d && !(door[1] && z >= lo[1] && z < hi[1] && y < 2))
                op(P_BRICK + brick_variant(z, y, 1), 0, BX(r->w, z), BY(r->w, y, z), 0, 0, 0);
        }
    if (door[0]) op(P_STEP, 0, scene_ox, scene_oy, 0, 0, 0);
    if (door[1]) op(P_STEP + 1, 0, scene_ox, scene_oy, 0, 0, 0);
    for (k = r->w + r->d - 2; k >= 0; k--)
        for (x = 0; x < r->w; x++) {
            z = k - x;
            if (z >= 0 && z < r->d) op(P_TILE + tile_variant(x, z), 0, BX(x, z), BY(x, 0, z), 0, 0, 0);
        }
}

/* scene.c's static items, their sort and the depth-first walk */
enum { I_BLOCK, I_SPIKES, I_GATE, I_THRONE };
typedef struct {
    Box b;
    long x0, y0, x1, y1, key;
    int kind, var, cx, cy, cz, mark;
} It;
static It item[700];
static int nitem;
static unsigned char edge[700][700];      /* edge[j][i]: i is drawn before j */

static long sx_of(fix x, fix z) { return scene_ox + (((x - z) * ISO_X) >> 12); }
static long sy_of(fix x, fix y, fix z) { return scene_oy - ((y * ISO_Y + (x + z) * ISO_Z) >> 12); }

static void set_box(It *it, fix x0, fix y0, fix z0, fix x1, fix y1, fix z1)
{
    it->b.x0 = x0; it->b.y0 = y0; it->b.z0 = z0;
    it->b.x1 = x1; it->b.y1 = y1; it->b.z1 = z1;
    it->x0 = sx_of(x0, z1) - 2;
    it->x1 = sx_of(x1, z0) + 2;
    it->y0 = sy_of(x1, y1, z1) - 2;
    it->y1 = sy_of(x0, y0, z0) + 2;
    it->key = ((x0 + x1 + z0 + z1) * 17 - (y0 + y1) * 14) >> 1;
}

static int order(const It *a, const It *b)
{
    const fix e = 4;
    int ab = a->b.x0 >= b->b.x1 - e || a->b.z0 >= b->b.z1 - e || a->b.y1 <= b->b.y0 + e;
    int ba = b->b.x0 >= a->b.x1 - e || b->b.z0 >= a->b.z1 - e || b->b.y1 <= a->b.y0 + e;
    if (ab && !ba) return 1;
    if (ba && !ab) return -1;
    if (a->key != b->key) return a->key > b->key ? 1 : -1;
    return 0;
}

static void add_static(int kind, int x, int y, int z, int var)
{
    It *it = &item[nitem++];
    fix X = (fix)x << 12, Y = (fix)y << 12, Z = (fix)z << 12;
    it->kind = kind;
    it->var = var;
    switch (kind) {
    case I_SPIKES: set_box(it, X, Y, Z, X + FX, Y + 1843, Z + FX); break;
    case I_GATE:   set_box(it, X, Y, Z, X + FX, Y + 2 * FX, Z + FX); break;
    case I_THRONE: set_box(it, X + 410, Y, Z + 614, X + 3686, Y + 6800, Z + 3564); break;
    default:       set_box(it, X, Y, Z, X + FX, Y + FX, Z + FX); break;
    }
    it->cx = x; it->cy = y; it->cz = z;
    it->mark = 0;
}

static void emit_item(const It *it)
{
    fix x = ((fix)it->cx << 12) + FX / 2, y = (fix)it->cy << 12, z = ((fix)it->cz << 12) + FX / 2;
    switch (it->kind) {
    case I_BLOCK:
        op(P_BLOCK + it->var, 1, sx_of((fix)it->cx << 12, (fix)it->cz << 12),
           sy_of((fix)it->cx << 12, (fix)it->cy << 12, (fix)it->cz << 12), it->cx, it->cy, it->cz);
        break;
    case I_SPIKES: op(P_SPIKES, 1, sx_of(x, z), sy_of(x, y, z), it->cx, it->cy, it->cz); break;
    case I_GATE:   op(P_GATE + it->var, 2, sx_of(x, z), sy_of(x, y, z), it->cx, it->cy, it->cz); break;
    case I_THRONE: op(P_THRONE, 1, sx_of(x, z), sy_of(x, y, z), it->cx, it->cy, it->cz); break;
    }
}

static void visit(int i)
{
    int j;
    item[i].mark = 1;
    for (j = 0; j < nitem; j++)
        if (edge[i][j] && !item[j].mark) visit(j);
    emit_item(&item[i]);
}

static void statics(void)
{
    const KRoom *r = R.room;
    int x, y, z, i, j;
    nitem = 0;
    for (y = 0; y < MAXH; y++)
        for (z = 0; z < r->d; z++)
            for (x = 0; x < r->w; x++)
                switch (CELL(r, x, y, z)) {
                case '#': add_static(I_BLOCK, x, y, z, block_variant(x, y, z)); break;
                case 'B': add_static(I_BLOCK, x, y, z, BLOCK_VARIANTS + block_variant(x, y, z)); break;
                case '^': add_static(I_SPIKES, x, y, z, 0); break;
                case 'G': add_static(I_GATE, x, y, z, x == 0 || x == r->w - 1); break;
                case 'T': add_static(I_THRONE, x, y, z, 0); break;
                }
    for (i = 1; i < nitem; i++) {
        It t = item[i];
        for (j = i; j > 0 && item[j - 1].key < t.key; j--) item[j] = item[j - 1];
        item[j] = t;
    }
    memset(edge, 0, sizeof(edge));
    for (i = 0; i < nitem; i++)
        for (j = i + 1; j < nitem; j++) {
            int o;
            if (!(item[i].x0 <= item[j].x1 && item[j].x0 <= item[i].x1 &&
                  item[i].y0 <= item[j].y1 && item[j].y0 <= item[i].y1)) continue;
            o = order(&item[i], &item[j]);
            if (o > 0) edge[j][i] = 1;
            else if (o < 0) edge[i][j] = 1;
        }
    /* scene.c walks a linked list per item, newest edge first: the same
       set of predecessors; visiting them in index order instead can only
       change the order of items that don't overlap */
    for (i = 0; i < nitem; i++)
        if (!item[i].mark) visit(i);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/host";
    static unsigned short fb[320 * 240];
    int l, r, i, m, f, k;

    vox_init();
    scene_init();
    open_out(dir, "sprites.bin");
    w16(3 * 8 * 3 + 8 * 3 + 16 * 2 + 1 + 1 + 1 + 9 + 3);
    for (m = 0; m < 3; m++)
        for (f = 0; f < 8; f++)
            for (k = 0; k < 3; k++) wsprite(&spr_walker[m][f][k]);
    for (f = 0; f < 8; f++) wsprite(&spr_sage[f]);
    for (f = 0; f < 8; f++) wsprite(&spr_ghost[f]);
    for (f = 0; f < 8; f++) wsprite(&spr_bouncer[f]);
    for (f = 0; f < 16; f++) wsprite(&spr_relic[f]);
    for (f = 0; f < 16; f++) wsprite(&spr_key[f]);
    wsprite(&spr_potion);
    wsprite(&spr_crate);
    wsprite(&spr_lift);
    for (f = 0; f < 9; f++) wsprite(&spr_piston[f]);
    for (f = 0; f < 3; f++) wsprite(&spr_shadow[f]);
    fclose(out);

    for (l = 0; l < keep_nlevels; l++)
        for (r = 0; r < keep_levels[l].nrooms; r++) {
            room_build(&keep_levels[l], r, 0);
            scene_fb = fb;
            scene_room();                 /* vox_room() + the original's own sort */
            for (i = 0; i < VARIANTS; i++) { piece[P_TILE + i] = &spr_tile[i]; piece[P_BRICK + i] = &spr_brick[i]; }
            piece[P_STEP] = &spr_step[0];
            piece[P_STEP + 1] = &spr_step[1];
            for (i = 0; i < BLOCK_VARIANTS; i++) { piece[P_BLOCK + i] = &spr_block[0][i]; piece[P_BLOCK + 3 + i] = &spr_block[1][i]; }
            piece[P_ARCH] = &spr_arch[0];
            piece[P_ARCH + 1] = &spr_arch[1];
            piece[P_SPIKES] = &spr_spikes;
            piece[P_GATE] = &spr_gate[0];
            piece[P_GATE + 1] = &spr_gate[1];
            piece[P_THRONE] = &spr_throne;

            nops = 0;
            background();
            statics();
            if (lv_has_door(R.level, R.ridx, SIDE_S)) op(P_ARCH, 3, scene_ox, scene_oy, 0, 0, 0);
            if (lv_has_door(R.level, R.ridx, SIDE_W)) op(P_ARCH + 1, 3, scene_ox, scene_oy, 0, 0, 0);

            open_out(dir, "room_%d_%d.bin", l, r);
            w16(P_COUNT);
            for (i = 0; i < P_COUNT; i++) wsprite(piece[i]);
            w16(nops);
            for (i = 0; i < nops; i++) {
                fputc(ops[i].p, out);
                fputc(ops[i].kind, out);
                w16(ops[i].x); w16(ops[i].y);
                w16(ops[i].cx); w16(ops[i].cy); w16(ops[i].cz);
            }
            w16(scene_ox);
            w16(scene_oy);
            fclose(out);

            /* the original's own picture of it, nothing moving */
            R.nact = 0; R.player = -1; R.npk = 0; R.nlift = 0;
            scene_frame(0, 0, -1);
            open_out(dir, "ref_%d_%d.bin", l, r);
            fwrite(fb, 2, 320 * 240, out);
            fclose(out);
        }
    printf("exported %d keeps\n", keep_nlevels);
    return 0;
}
