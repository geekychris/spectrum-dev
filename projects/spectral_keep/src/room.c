/*
 * One room, built from its level data and simulated while the player is in
 * it (RoomView.cs), and everything that moves in it (Actors.cs). Leaving and
 * re-entering rebuilds the room, so crates and enemies reset, as in the
 * 8-bit originals. The original's room.c in 16-bit Q11 (see keep.h); the
 * original's Q12 value of each constant is in brackets.
 */
#include "keep.h"
#include "zxrle.h"

#define WALK_SPEED   127           /* 3.1 u/s (254) */
#define JUMP_SPEED   303           /* 7.4 u/s (606) */
#define LIFT_SPEED   53            /* 1.3 u/s (106) */
#define LIFT_PAUSE   45            /* 0.9 s */
#define LIFT_TRAVEL  (2 * FX)
#define TALL         (12 * FX)     /* port: 30 tiles; 12 is above any jump and fits 16 bits */

RoomState R;

/* ---- small maths ---- */

static uint16_t isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1UL << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (uint16_t)r;
}

/* |(x, z)|: Q11 positions or speeds in, the same out (exact to 4 units, as the original) */
static int16_t flat_len(int16_t x, int16_t z)
{
    int32_t a = x >> 2, b = z >> 2;
    return (int16_t)(isqrt((uint32_t)(a * a + b * b)) << 2);
}

/* the same for Q12 directions and speeds (u/s) that may exceed 16 bits */
static int32_t flat_len32(int32_t x, int32_t z)
{
    int32_t a = x >> 2, b = z >> 2;
    return (int32_t)isqrt((uint32_t)(a * a + b * b)) << 2;
}

static uint8_t octant(int32_t vx, int32_t vz)
{
    int32_t ax = vx < 0 ? -vx : vx, az = vz < 0 ? -vz : vz;
    if (ax * 10 < az * 4) return vz > 0 ? 0 : 4;      /* within 22 degrees of an axis */
    if (az * 10 < ax * 4) return vx > 0 ? 2 : 6;
    if (vx > 0) return vz > 0 ? 1 : 3;
    return vz > 0 ? 7 : 5;
}

/* ---- level helpers (LevelData.cs) ---- */

int8_t lv_room_at(const KLevel *l, int8_t gx, int8_t gy)
{
    uint8_t i;
    for (i = 0; i < l->nrooms; i++)
        if (l->rooms[i].gx == gx && l->rooms[i].gy == gy) return (int8_t)i;
    return -1;
}

static const int8_t side_dx[4] = { 0, 1, 0, -1 }, side_dy[4] = { 1, 0, -1, 0 };
static const char side_ch[4] = { 'N', 'E', 'S', 'W' };

int8_t lv_neighbour(const KLevel *l, uint8_t room, uint8_t side)
{
    return lv_room_at(l, l->rooms[room].gx + side_dx[side], l->rooms[room].gy + side_dy[side]);
}

static uint8_t sealed(const KRoom *r, uint8_t side)
{
    const char *s;
    for (s = r->seal; s && *s; s++)
        if (*s == side_ch[side]) return 1;
    return 0;
}

uint8_t lv_has_door(const KLevel *l, uint8_t room, uint8_t side)
{
    int8_t n = lv_neighbour(l, room, side);
    return n >= 0 && !sealed(&l->rooms[room], side) && !sealed(&l->rooms[n], (side + 2) & 3);
}

void door_span(uint8_t len, uint8_t *lo, uint8_t *hi)
{
    *lo = len / 2 - 1;
    *hi = *lo + 2;
}

/* ---- construction ---- */

static Actor *new_actor(uint8_t kind, fix x, fix y, fix z, fix sx, fix sy, fix sz)
{
    Actor *a = &R.act[R.nact];
    a->kind = kind;
    a->body = w_add(x, y, z, sx, sy, sz);
    a->body->dynamic = 1;
    a->body->owner = (int8_t)R.nact;
    a->dx = a->dz = 0;
    a->awake = 0;
    a->bob = a->last_talk = 0;
    a->text = 0;
    a->face = 4;                          /* Vector3.back */
    a->phase = 0;
    a->roll = 0;
    a->invuln = a->coyote = a->jumpbuf = 0;
    a->was_grounded = 1;
    a->visible = 1;
    R.nact++;
    return a;
}

/* collision for one wall: columns either side of a doorway, the lintel and
 * an invisible top so nobody hops over; front walls are not drawn */
static void wall(uint8_t side)
{
    const KRoom *r = R.room;
    uint8_t alongx = side == SIDE_N || side == SIDE_S, len = alongx ? r->w : r->d, lo, hi, door, n, i;
    fix W = (fix)r->w << FXS, D = (fix)r->d << FXS;
    fix a[4], b[4], y0[4], y1[4];
    door_span(len, &lo, &hi);
    door = lv_has_door(R.level, R.ridx, side);
    if (door) {
        a[0] = -FX;              b[0] = (fix)lo << FXS;          y0[0] = 0;      y1[0] = TALL;
        a[1] = (fix)hi << FXS;   b[1] = (fix)(len + 1) << FXS;   y0[1] = 0;      y1[1] = TALL;
        a[2] = (fix)lo << FXS;   b[2] = (fix)hi << FXS;          y0[2] = 2 * FX; y1[2] = TALL;
        a[3] = (fix)lo << FXS;   b[3] = (fix)hi << FXS;          y0[3] = 0;      y1[3] = 2 * FX;
        n = 4;
    } else {
        a[0] = -FX; b[0] = (fix)(len + 1) << FXS; y0[0] = 0; y1[0] = TALL;
        n = 1;
    }
    for (i = 0; i < n; i++) {
        Body *o;
        switch (side) {
        case SIDE_N: o = w_add_static(a[i], y0[i], D, b[i], y1[i], D + FX); break;
        case SIDE_E: o = w_add_static(W, y0[i], a[i], W + FX, y1[i], b[i]); break;
        case SIDE_S: o = w_add_static(a[i], y0[i], -FX, b[i], y1[i], 0); break;
        default:     o = w_add_static(-FX, y0[i], a[i], 0, y1[i], b[i]); break;
        }
        if (i == 3) o->npcbar = 1;
    }
}

void room_build(const KLevel *l, uint8_t ridx, uint8_t host)
{
    const KRoom *r = &l->rooms[ridx];
    uint8_t x, y, z, i, gate_open;
    R.level = l;
    R.room = r;
    R.ridx = ridx;
    R.host = host;
    R.nact = 0;
    R.player = -1;
    R.nlift = R.npk = R.nhaz = R.ngate = 0;
    R.has_throne = 0;
    R.clock = 0;
    R.exited = 0;
    rle_unpack((uint8_t *)R.cells, r->cells);
    w_reset(r->w, r->d);

    /* the shell */
    w_add_static(-2 * FX, -FX, -2 * FX, (fix)(r->w + 2) << FXS, 0, (fix)(r->d + 2) << FXS);
    wall(SIDE_N);
    wall(SIDE_E);
    wall(SIDE_S);
    wall(SIDE_W);
    w_add_static((fix)r->w << FXS, 0, (fix)r->d << FXS, (fix)(r->w + 1) << FXS, TALL, (fix)(r->d + 1) << FXS);

    /* the cells */
    gate_open = host && host_gate_open(ridx);
    for (y = 0; y < MAXH; y++)
        for (z = 0; z < r->d; z++)
            for (x = 0; x < r->w; x++) {
                uint16_t cell = ((uint16_t)y * r->d + z) * r->w + x;
                char c = R.cells[cell];
                fix bx = ((fix)x << FXS) + FX / 2, by = (fix)y << FXS, bz = ((fix)z << FXS) + FX / 2;
                fix cx = (fix)x << FXS, cz = (fix)z << FXS;
                switch (c) {
                case '#':
                case 'B':
                    w_set_block(x, y, z);
                    break;
                case 'C':
                    if (R.nact < MAXACT) {
                        Actor *a = new_actor(K_CRATE, bx, by, bz, 1966, 1966, 1966);   /* 0.96 (3932) */
                        a->body->pushable = 1;
                    }
                    break;
                case '^':
                    if (R.nhaz < MAXHAZ) {
                        Box *h = &R.haz[R.nhaz++];
                        h->x0 = cx + 205; h->x1 = cx + 1843;       /* 0.1 .. 0.9 (410, 3686) */
                        h->y0 = by;       h->y1 = by + 819;        /* 0.4 (1638) */
                        h->z0 = cz + 205; h->z1 = cz + 1843;
                    }
                    break;
                case 'L':
                    if (R.nlift < MAXLIFT) {
                        Lift *lf = &R.lift[R.nlift++];
                        lf->body = w_add(bx, by, bz, 2007, FX / 4, 2007);   /* 0.98 x 0.25 (4014) */
                        lf->base = by;
                        lf->dir = 1;
                        lf->wait = LIFT_PAUSE;
                    }
                    break;
                case 'G':
                    if (!gate_open && R.ngate < MAXGATE) {
                        Gate *g = &R.gate[R.ngate++];
                        g->body = w_add_static(cx, by, cz, cx + FX, by + 2 * FX, cz + FX);
                        g->x = x; g->y = y; g->z = z;
                        g->rot = x == 0 || x == r->w - 1;     /* face along the passage */
                    }
                    break;
                case 'R':
                case 'P':
                case 'K':
                    if ((host && host_taken(ridx, cell)) || R.npk >= MAXPICK) break;
                    {
                        Pickup *p = &R.pk[R.npk++];
                        p->kind = (uint8_t)c;
                        p->x = x; p->y = y; p->z = z;
                        p->cell = cell;
                        p->box.x0 = bx - 717; p->box.x1 = bx + 717;     /* 0.35 (1434) */
                        p->box.y0 = by;       p->box.y1 = by + 1843;    /* 0.9 (3686) */
                        p->box.z0 = bz - 717; p->box.z1 = bz + 717;
                    }
                    break;
                case 'T':
                    R.has_throne = 1;
                    R.tx = x; R.ty = y; R.tz = z;
                    R.throne.x0 = cx - 102; R.throne.x1 = cx + FX + 102;   /* 0.05 (205) */
                    R.throne.y0 = by;       R.throne.y1 = by + 2458;       /* 1.2 (4915) */
                    R.throne.z0 = cz - 102; R.throne.z1 = cz + FX + 102;
                    w_add_static(cx + 205, by, cz + 205, cx + 1843, by + 819, cz + 1843);
                    break;
                }
            }

    /* the actors */
    for (i = 0; i < r->nactors && R.nact < MAXACT; i++) {
        const KActor *k = &r->actors[i];
        fix px = ((fix)k->x << FXS) + FX / 2, py = (fix)k->y << FXS, pz = ((fix)k->z << FXS) + FX / 2;
        Actor *a;
        switch (k->type) {
        case AT_GUARD:
            a = new_actor(K_GUARD, px, py, pz, 1229, 1946, 1229);      /* 0.6 x 0.95 */
            a->body->harm = 1;
            if (k->alongz) a->dz = D12; else a->dx = D12;
            break;
        case AT_HOUND:
            new_actor(K_HOUND, px, py, pz, 1024, 1434, 1024)->body->harm = 1;   /* 0.5 x 0.7 */
            break;
        case AT_GHOST:
            a = new_actor(K_GHOST, px, py, pz, 1126, 1638, 1126);      /* 0.55 x 0.8 */
            a->body->collides = a->body->solid = a->body->dynamic = 0;
            a->bob = (int32_t)k->x * 50 + 25;                          /* bob = pos.x seconds */
            break;
        case AT_BOUNCER:
            a = new_actor(K_BOUNCER, px, py, pz, 1229, 1270, 1229);    /* 0.6 x 0.62 */
            a->body->harm = 1;
            if (k->alongz) { a->dx = -2107; a->dz = 3512; }            /* (-0.6, 1) normalised, Q12 */
            else { a->dx = 3512; a->dz = 2107; }
            break;
        case AT_SAGE:
            a = new_actor(K_SAGE, px, py, pz, 1229, 2253, 1229);       /* 0.6 x 1.1 */
            a->text = k->text && k->text[0] ? k->text : "HELLO TRAVELLER.";
            a->last_talk = -99 * 50;
            break;
        }
    }
}

void room_spawn_player(fix x, fix y, fix z)
{
    Actor *a;
    if (R.nact >= MAXACT) return;
    a = new_actor(K_PLAYER, x, y, z, 1147, 1843, 1147);                 /* 0.56 x 0.9 */
    a->body->player = 1;
    R.player = (int8_t)(R.nact - 1);
    R.exited = 0;
}

/* ---- simulation ---- */

static void pose(Actor *a, int32_t vx, int32_t vz, uint8_t walks)    /* v in u/s, Q11 */
{
    int32_t speed = flat_len32(vx, vz);
    if (speed > 205) a->face = octant(vx, vz);           /* sqrMagnitude > 0.01 (410) */
    if (!walks) return;
    if (speed > 205) a->phase += (uint16_t)((speed * 939) >> 11);   /* 4.5 rad per unit */
    else a->phase = 0;
}

/* direction (Q12) times speed (Q11 per step) */
static fix scale12(int16_t d, int16_t speed) { return (fix)(((int32_t)d * speed) >> 12); }

static void walk(Actor *a, int16_t wx, int16_t wz, int16_t speed, uint8_t push, fix *moved)
{
    a->body->vx = scale12(wx, speed);
    a->body->vz = scale12(wz, speed);
    w_step(a->body, push, moved);
}

static void player_pos(const Actor *self, fix *x, fix *y, fix *z)
{
    const Body *b = R.player >= 0 ? R.act[R.player].body : self->body;
    *x = b->px; *y = b->py; *z = b->pz;
}

static uint8_t ground_ahead(Actor *a)
{
    Box b, ahead;
    fix ox = scale12(a->dx, 922), oz = scale12(a->dz, 922);       /* 0.45 (1843) */
    body_box(a->body, &b);
    ahead.x0 = b.x0 + ox; ahead.x1 = b.x1 + ox;
    ahead.z0 = b.z0 + oz; ahead.z1 = b.z1 + oz;
    ahead.y0 = b.y0 - 307;                                        /* 0.15 (614) */
    ahead.y1 = b.y0 - 20;                                         /* 0.01 (41) */
    return w_overlapping_solid(&ahead, a->body);
}

/* a Q12 direction towards (tx, tz), Q11 offsets with length len */
static void towards(fix *tx, fix *tz, int16_t len)
{
    *tx = (fix)(((int32_t)*tx << 12) / len);
    *tz = (fix)(((int32_t)*tz << 12) / len);
}

static void tick_actor(Actor *a)
{
    fix moved[3], px, py, pz, tx, tz;
    int16_t len;
    switch (a->kind) {
    case K_GUARD:
        if (a->body->grounded && !ground_ahead(a)) { a->dx = -a->dx; a->dz = -a->dz; }
        walk(a, a->dx, a->dz, 66, 0, moved);                       /* 1.6 u/s (131) */
        {
            int32_t along = ((int32_t)moved[0] * a->dx + (int32_t)moved[2] * a->dz) >> 12;
            if (along < 0) along = -along;
            if (along < 33) { a->dx = -a->dx; a->dz = -a->dz; }    /* (65) */
        }
        pose(a, a->dx, a->dz, 1);
        break;
    case K_HOUND:
        player_pos(a, &px, &py, &pz);
        tx = px - a->body->px;
        tz = pz - a->body->pz;
        len = flat_len(tx, tz);
        if (!a->awake && R.player >= 0 && len < 7168) {            /* 3.5 (14336) */
            a->awake = 1;
            if (R.host) host_sound(SFX_BARK);
        }
        if (a->awake && len > 205) towards(&tx, &tz, len);         /* 0.1 (410) */
        else tx = tz = 0;
        walk(a, tx, tz, 59, 0, moved);                             /* 1.45 u/s (119) */
        pose(a, tx, tz, 1);
        break;
    case K_GHOST:
        a->bob++;
        player_pos(a, &px, &py, &pz);
        tx = px - a->body->px;
        tz = pz - a->body->pz;
        len = flat_len(tx, tz);
        if (len > 410) {                                            /* 0.2 (819) */
            tx = (fix)((int32_t)tx * 37 / len);                    /* 0.9 u/s (74) */
            tz = (fix)((int32_t)tz * 37 / len);
        } else tx = tz = 0;
        {
            Body *b = a->body;
            fix lo = 614, hix = ((fix)R.room->w << FXS) - 614, hiz = ((fix)R.room->d << FXS) - 614;   /* 0.3 */
            b->px += tx;
            b->pz += tz;
            if (b->px < lo) b->px = lo;
            if (b->px > hix) b->px = hix;
            if (b->pz < lo) b->pz = lo;
            if (b->pz > hiz) b->pz = hiz;
            b->py = (py > 0 ? py / 2 : 0) + 512 +                  /* 0.25 (1024), 0.2 (819) */
                    (fix)(((int32_t)isin256((uint8_t)((a->bob * 2037) / 1000)) * 410) >> 14);
        }
        pose(a, (int32_t)tx * 50, (int32_t)tz * 50, 0);
        break;
    case K_BOUNCER:
        walk(a, a->dx, a->dz, 86, 0, moved);                       /* 2.1 u/s (172) */
        {
            fix wx = scale12(a->dx, 86), wz = scale12(a->dz, 86);
            if ((moved[0] < 0 ? -moved[0] : moved[0]) * 2 < (wx < 0 ? -wx : wx)) a->dx = -a->dx;
            if ((moved[2] < 0 ? -moved[2] : moved[2]) * 2 < (wz < 0 ? -wz : wz)) a->dz = -a->dz;
        }
        if (a->body->grounded) a->body->vy = 254;                  /* 6.2 u/s (508) */
        a->phase += 136;                                           /* 300 degrees/s, frames 11.25 apart */
        a->roll = (uint8_t)(a->phase >> 8) & 7;
        break;
    case K_SAGE:
        w_step(a->body, 0, moved);
        player_pos(a, &px, &py, &pz);
        pose(a, (px - a->body->px) / 20, (pz - a->body->pz) / 20, 0);
        break;
    case K_CRATE:
        a->body->vx = a->body->vz = 0;
        w_step(a->body, 0, moved);
        break;
    }
}

static void drive_player(Actor *a, int16_t mx, int16_t mz, uint8_t jump)
{
    fix moved[3];
    int16_t len = (int16_t)flat_len32(mx, mz);
    if (a->invuln > 0) a->invuln--;
    a->coyote = a->body->grounded ? 5 : a->coyote - 1;
    a->jumpbuf = jump ? 7 : a->jumpbuf - 1;
    if (a->jumpbuf > 0 && a->coyote > 0) {
        a->body->vy = JUMP_SPEED;
        a->jumpbuf = a->coyote = 0;
        host_sound(SFX_JUMP);
    }
    if (len > D12) {
        mx = (int16_t)(((int32_t)mx << 12) / len);
        mz = (int16_t)(((int32_t)mz << 12) / len);
    }
    walk(a, mx, mz, WALK_SPEED, 1, moved);
    if (a->body->grounded && !a->was_grounded) host_sound(SFX_LAND);
    a->was_grounded = a->body->grounded;
    pose(a, (int32_t)a->body->vx * 50, (int32_t)a->body->vz * 50, 1);
    a->visible = a->invuln <= 0 || (a->invuln % 10) < 6;
}

static void tick_lift(Lift *l)
{
    fix target, y, step;
    if (l->wait > 0) { l->wait--; return; }
    target = l->base + (l->dir > 0 ? LIFT_TRAVEL : 0);
    y = l->body->py;
    step = target - y;
    if (step > LIFT_SPEED) step = LIFT_SPEED;
    if (step < -LIFT_SPEED) step = -LIFT_SPEED;
    w_move_kinematic(l->body, step);
    if (l->body->py == target) { l->dir = -l->dir; l->wait = LIFT_PAUSE; }
}

static uint8_t check_exit(void)
{
    const Body *b = R.act[R.player].body;
    int8_t side = -1;
    if (b->px > ((fix)R.room->w << FXS) + 41) side = SIDE_E;   /* 0.02 (82) */
    else if (b->px < -41) side = SIDE_W;
    else if (b->pz > ((fix)R.room->d << FXS) + 41) side = SIDE_N;
    else if (b->pz < -41) side = SIDE_S;
    if (side < 0) return 0;
    R.exited = 1;
    host_exit((uint8_t)side, b->px, b->py, b->pz);
    return 1;
}

static const char *actor_name(uint8_t kind)
{
    switch (kind) {
    case K_GUARD: return "A GUARD";
    case K_HOUND: return "THE HOUND";
    case K_GHOST: return "A GHOST";
    case K_BOUNCER: return "A BOUNCER";
    }
    return "";
}

void room_tick(int16_t mx, int16_t mz, uint8_t jump)
{
    Box pb, hurt, ab, reach;
    Actor *p;
    uint8_t i;
    R.clock++;
    for (i = 0; i < R.nlift; i++) tick_lift(&R.lift[i]);
    if (R.player >= 0) drive_player(&R.act[R.player], mx, mz, jump);
    for (i = 0; i < R.nact; i++)
        if (i != R.player) tick_actor(&R.act[i]);
    if (R.player < 0 || R.exited || !R.host) return;
    p = &R.act[R.player];
    body_box(p->body, &pb);
    if (check_exit()) return;
    for (i = R.npk; i-- > 0;) {
        Pickup k;
        if (!box_overlap(&R.pk[i].box, &pb, 2)) continue;
        k = R.pk[i];
        R.pk[i] = R.pk[--R.npk];
        host_pickup(k.kind, R.ridx, k.cell);
    }
    if (R.has_throne && box_overlap(&R.throne, &pb, 2)) host_throne();
    if (R.ngate > 0) {
        reach = pb;
        box_grow(&reach, 164);                                     /* 0.08 (328) */
        for (i = 0; i < R.ngate; i++) {
            Box gb;
            body_box(R.gate[i].body, &gb);
            if (box_overlap(&reach, &gb, 2) && host_try_key(R.ridx)) {
                uint8_t k;
                for (k = 0; k < R.ngate; k++) w_remove(R.gate[k].body);
                R.ngate = 0;
                host_gate_opened();
                break;
            }
        }
    }
    if (p->invuln > 0) return;
    hurt = pb;
    box_shrink80(&hurt);
    for (i = 0; i < R.nhaz; i++)
        if (box_overlap(&hurt, &R.haz[i], 2)) { host_death("SPIKES"); return; }
    for (i = 0; i < R.nact; i++) {
        Actor *a = &R.act[i];
        if (a->kind < K_GUARD || a->kind > K_BOUNCER) continue;
        body_box(a->body, &ab);
        box_shrink80(&ab);
        if (!box_overlap(&hurt, &ab, 2)) continue;
        host_death(actor_name(a->kind));
        return;
    }
    reach = pb;
    box_grow(&reach, 614);                                         /* 0.3 (1229) */
    for (i = 0; i < R.nact; i++) {
        Actor *a = &R.act[i];
        if (a->kind != K_SAGE) continue;
        body_box(a->body, &ab);
        if (box_overlap(&reach, &ab, 2) && R.clock - a->last_talk >= 200) {   /* 4 s */
            a->last_talk = R.clock;
            host_talk(a->text);
            keep_log("talk %s\n", R.room->id);
        }
    }
}

/* port: the top of what's below each actor (scene.c draws the shadows
   there; the original asks w_ground_below while drawing) */
void room_grounds(void)
{
    uint8_t i;
    for (i = 0; i < R.nact; i++) {
        const Body *b = R.act[i].body;
        R.act[i].ground = w_ground_below(b->px, b->py, b->pz);
    }
}

/* where the player appears entering `to` through a side, given where they
 * left `from` (RoomView.EntryPoint) */
void room_entry_point(const KRoom *from, const KRoom *to, uint8_t side, fix *x, fix *y, fix *z)
{
    const fix inset = 819;                                         /* 0.4 (1638) */
    fix px = *x, pz = *z;
    uint8_t lo, hi, alongx = side == SIDE_N || side == SIDE_S;
    switch (side) {
    case SIDE_E: px = inset; pz = *z - ((fix)from->d << (FXS - 1)) + ((fix)to->d << (FXS - 1)); break;
    case SIDE_W: px = ((fix)to->w << FXS) - inset; pz = *z - ((fix)from->d << (FXS - 1)) + ((fix)to->d << (FXS - 1)); break;
    case SIDE_N: pz = inset; px = *x - ((fix)from->w << (FXS - 1)) + ((fix)to->w << (FXS - 1)); break;
    default:     pz = ((fix)to->d << FXS) - inset; px = *x - ((fix)from->w << (FXS - 1)) + ((fix)to->w << (FXS - 1)); break;
    }
    door_span(alongx ? to->w : to->d, &lo, &hi);
    if (alongx) {
        if (px < ((fix)lo << FXS) + 717) px = ((fix)lo << FXS) + 717;   /* 0.35 (1434) */
        if (px > ((fix)hi << FXS) - 717) px = ((fix)hi << FXS) - 717;
    } else {
        if (pz < ((fix)lo << FXS) + 717) pz = ((fix)lo << FXS) + 717;
        if (pz > ((fix)hi << FXS) - 717) pz = ((fix)hi << FXS) - 717;
    }
    *x = px;
    *z = pz;
    if (*y < 0) *y = 0;
}
