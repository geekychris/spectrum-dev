/*
 * pb_phys.c - Boing Pinball's physics for the Z80: the original's
 * (tools/orig/pb_core.c, "collisions", "the ramps" and "the world"),
 * function for function, in the 16-bit units of pbz.h. The constants are
 * the original's, converted; where the arithmetic differs it says so.
 */
#include <string.h>
#include "pbz.h"

#define ABS16(a)  ((int16_t)((a) < 0 ? -(a) : (a)))
#define SLOW      V_MM(60)              /* below this a touch doesn't bounce or scrub */
#define PI_A      12868                 /* pi, A */

static int16_t clamp16(int16_t v, int16_t lo, int16_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : (int16_t)v; }

/* |(x, y)|, V or P */
static int16_t hypot16(int16_t x, int16_t y)
{
    return (int16_t)isqrt32((uint32_t)(mul16(x, x) + mul16(y, y)));
}

/* ================================================================ collisions */

static void bounce(Ball *b, int16_t nx, int16_t ny, int16_t vn, uint8_t e, uint8_t fric)
{
    int16_t tx = b->vx - mulq14(vn, nx), ty = b->vy - mulq14(vn, ny);
    int8_t slow = -vn < SLOW;
    int16_t ee = slow ? 0 : e, k = slow ? 256 : 256 - fric;
    int16_t en = mulq8(ee, vn);
    b->vx = mulq8(tx, k) - mulq14(en, nx);
    b->vy = mulq8(ty, k) - mulq14(en, ny);
}

static void unit(int16_t dx, int16_t dy, int16_t d, int16_t *nx, int16_t *ny)
{
    *nx = div_q14(dx, d);
    *ny = div_q14(dy, d);
}

static void place(Ball *b, int16_t x, int16_t y)
{
    b->x = x; b->y = y; b->xf = b->yf = 128;
}

/* Circle (the ball) against a capsule segment: resolves the overlap and
 * returns the approach speed (V), or -1. */
static int16_t hit_segment(Ball *b, const Seg *s)
{
    int16_t dx = b->x - s->ax, dy = b->y - s->ay, t, qx, qy, rr = PB_R + s->r, d, nx, ny, vn;
    int32_t tt = (mul16(dx, s->ux) + mul16(dy, s->uy)) >> 14;
    t = tt < 0 ? 0 : tt > s->len ? s->len : (int16_t)tt;
    qx = s->ax + mulq14(s->ux, t);
    qy = s->ay + mulq14(s->uy, t);
    dx = b->x - qx; dy = b->y - qy;
    if (ABS16(dx) >= rr || ABS16(dy) >= rr) return -1;
    {
        uint32_t d2 = (uint32_t)(mul16(dx, dx) + mul16(dy, dy));
        if (d2 >= (uint32_t)mul16(rr, rr)) return -1;
        d = (int16_t)isqrt32(d2);
    }
    if (d < 1) {                        /* dead centre: out on the side it came from */
        nx = -s->uy; ny = s->ux;
        if (mul16(nx, b->vx) + mul16(ny, b->vy) > 0) { nx = -nx; ny = -ny; }
    } else unit(dx, dy, d, &nx, &ny);
    vn = (int16_t)((mul16(b->vx, nx) + mul16(b->vy, ny)) >> 14);
    if ((s->ow_x || s->ow_y) && (nx * s->ow_x + ny * s->ow_y <= 0 || vn >= 0)) return -1;
    place(b, qx + mulq14(nx, rr), qy + mulq14(ny, rr));
    if (vn < 0) bounce(b, nx, ny, vn, s->e, 8);           /* friction 0.03 */
    return vn < 0 ? -vn : 0;
}

static int16_t hit_circle(Ball *b, const Circ *c, int16_t *pnx, int16_t *pny)
{
    int16_t dx = b->x - c->cx, dy = b->y - c->cy, rr = PB_R + c->r, d, nx, ny, vn;
    if (ABS16(dx) >= rr || ABS16(dy) >= rr) return -1;
    {
        uint32_t d2 = (uint32_t)(mul16(dx, dx) + mul16(dy, dy));
        if (d2 >= (uint32_t)mul16(rr, rr)) return -1;
        d = (int16_t)isqrt32(d2);
    }
    if (d < 1) { nx = 0; ny = 16384; } else unit(dx, dy, d, &nx, &ny);
    place(b, c->cx + mulq14(nx, rr), c->cy + mulq14(ny, rr));
    vn = (int16_t)((mul16(b->vx, nx) + mul16(b->vy, ny)) >> 14);
    if (vn < 0) bounce(b, nx, ny, vn, c->e, 5);           /* friction 0.02 */
    *pnx = nx; *pny = ny;
    return vn < 0 ? -vn : 0;
}

void pb_flipper_tip(const Flipper *f, int16_t *x, int16_t *y)
{
    Flipper *m = (Flipper *)f;          /* port: the tip is cached until the flipper moves */
    if (m->tip_ang != f->ang) {
        m->tx = f->d.px + mulq14(f->d.len, cos_q14(f->ang));
        m->ty = f->d.py + mulq14(f->d.len, sin_q14(f->ang));
        m->tip_ang = f->ang;
    }
    *x = m->tx;
    *y = m->ty;
}

/* Ball against a (moving) flipper, a tapered capsule; the surface's
 * velocity at the contact is w x r. */
static int16_t hit_flipper(Ball *b, const Flipper *f)
{
    int16_t tx, ty, ex, ey, t, qx, qy, rad, dx, dy, rr, d, nx, ny, cx, cy, vcx, vcy, rvx, rvy, vn;
    int32_t num, den;
    pb_flipper_tip(f, &tx, &ty);
    ex = tx - f->d.px; ey = ty - f->d.py;
    num = mul16(b->x - f->d.px, ex) + mul16(b->y - f->d.py, ey);
    den = mul16(f->d.len, f->d.len);
    /* t = clamp(num / den, 0, 1), Q14 (port: no 64-bit division) */
    t = num <= 0 ? 0 : num >= den ? 16384 : (int16_t)((num << 8) / (den >> 6));
    qx = f->d.px + mulq14(t, ex);
    qy = f->d.py + mulq14(t, ey);
    rad = f->d.r1 + mulq14(f->d.r2 - f->d.r1, t);
    dx = b->x - qx; dy = b->y - qy;
    rr = PB_R + rad;
    if (ABS16(dx) >= rr || ABS16(dy) >= rr) return -1;
    {
        uint32_t d2 = (uint32_t)(mul16(dx, dx) + mul16(dy, dy));
        if (d2 >= (uint32_t)mul16(rr, rr)) return -1;
        d = (int16_t)isqrt32(d2);
    }
    if (d < 1) {                        /* dead centre: out on the side the flipper moves towards */
        nx = -div_q14(ey, f->d.len); ny = div_q14(ex, f->d.len);
        if (f->w < 0) { nx = -nx; ny = -ny; }
    } else unit(dx, dy, d, &nx, &ny);
    cx = qx + mulq14(nx, rad);
    cy = qy + mulq14(ny, rad);
    /* the contact point's velocity: W/256 rad/s x P/16 mm = mm/s, x4 for V */
    vcx = -(int16_t)(mul16(f->w, cy - f->d.py) >> 10);
    vcy = (int16_t)(mul16(f->w, cx - f->d.px) >> 10);
    place(b, qx + mulq14(nx, rr), qy + mulq14(ny, rr));
    rvx = sat16((int32_t)b->vx - vcx); rvy = sat16((int32_t)b->vy - vcy);
    vn = (int16_t)((mul16(rvx, nx) + mul16(rvy, ny)) >> 14);
    if (vn < 0) {
        int16_t e = ABS16(f->w) > 256 ? 141 : 77;          /* 0.55 : 0.3 */
        int8_t slow = -vn < SLOW;
        int16_t ee = slow ? 0 : e, k = slow ? 256 : 248, en = mulq8(ee, vn);   /* 0.97 */
        int16_t sx = rvx - mulq14(vn, nx), sy = rvy - mulq14(vn, ny);
        b->vx = sat16((int32_t)vcx + mulq8(sx, k) - mulq14(en, nx));
        b->vy = sat16((int32_t)vcy + mulq8(sy, k) - mulq14(en, ny));
        return -vn;
    }
    return 0;
}

static int8_t hit_balls(Ball *a, Ball *b)
{
    int16_t dx = b->x - a->x, dy = b->y - a->y, rr = 2 * PB_R, d, nx, ny, pen, rel;
    uint32_t d2;
    if (ABS16(dx) >= rr || ABS16(dy) >= rr) return 0;
    d2 = (uint32_t)(mul16(dx, dx) + mul16(dy, dy));
    if (d2 >= (uint32_t)mul16(rr, rr) || d2 == 0) return 0;
    d = (int16_t)isqrt32(d2);
    if (d < 1) return 0;
    unit(dx, dy, d, &nx, &ny);
    pen = (rr - d) / 2;
    place(a, a->x - mulq14(nx, pen), a->y - mulq14(ny, pen));
    place(b, b->x + mulq14(nx, pen), b->y + mulq14(ny, pen));
    rel = (int16_t)((mul16(b->vx - a->vx, nx) + mul16(b->vy - a->vy, ny)) >> 14);
    if (rel < 0) {
        int16_t j = mulq8(rel, 243);                     /* 0.95 */
        a->vx += mulq14(j, nx); a->vy += mulq14(j, ny);
        b->vx -= mulq14(j, nx); b->vy -= mulq14(j, ny);
    }
    return 1;
}

/* Did the move p0 -> p1 cross the sensor line? +1 / -1 (the side crossed
 * to), or 0. */
static int8_t crossed(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const Sensor *s)
{
    int16_t ex = s->bx - s->ax, ey = s->by - s->ay, px, py;
    int32_t d0 = mul16(ex, y0 - s->ay) - mul16(ey, x0 - s->ax);
    int32_t d1 = mul16(ex, y1 - s->ay) - mul16(ey, x1 - s->ax);
    int32_t u, l2;
    if ((d0 > 0) == (d1 > 0) || d0 == d1) return 0;
    /* the crossing point (port: the step is short, so (x1 - x0) * d0 fits) */
    px = x0 + (int16_t)((int32_t)(x1 - x0) * d0 / (d0 - d1));
    py = y0 + (int16_t)((int32_t)(y1 - y0) * d0 / (d0 - d1));
    l2 = mul16(ex, ex) + mul16(ey, ey);
    u = mul16(px - s->ax, ex) + mul16(py - s->ay, ey);   /* 0 <= u / l2 <= 1 */
    if (u < 0 || u > l2) return 0;
    return d1 > 0 ? 1 : -1;
}

/* ================================================================ the ramps */

static void ramp_at(const Ramp *r, int16_t s, int16_t *x, int16_t *y, int16_t *z,
                    int16_t *slope, int16_t *dx, int16_t *dy)
{
    int16_t L = r->prof[r->n - 1], t, seglen;
    const int16_t *a, *b;
    int8_t i = 1;
    s = clamp16(s, 0, L);
    while (i < r->n - 1 && r->prof[i] < s) i++;
    seglen = r->prof[i] - r->prof[i - 1];
    if (seglen <= 0) seglen = 16;
    t = div_q14(s - r->prof[i - 1], seglen);
    a = r->pts[i - 1]; b = r->pts[i];
    *x = a[0] * 16 + mulq14((b[0] - a[0]) * 16, t);
    *y = a[1] * 16 + mulq14((b[1] - a[1]) * 16, t);
    *z = a[2] + mulq14(b[2] - a[2], t);
    if (slope) *slope = div_q14((b[2] - a[2]) * 16, seglen);
    if (dx) *dx = div_q14((b[0] - a[0]) * 16, seglen);
    if (dy) *dy = div_q14((b[1] - a[1]) * 16, seglen);
}

/* ================================================================ the world */

static void emit(World *w, int8_t type, int16_t x, int16_t y, int8_t id, int8_t up, int32_t val, int8_t ball)
{
    Event *e;
    if (w->nev >= PB_MAX_EV) return;
    e = &w->ev[w->nev++];
    e->type = type; e->id = id; e->kind = 0; e->up = up;
    e->x = x >> 4; e->y = y >> 4; e->val = val; e->ball = ball;
}

void pb_world_init(World *w, Table *t)
{
    int8_t i;
    memset(w, 0, sizeof *w);
    w->t = t;
    w->nflip = t->nflip;
    for (i = 0; i < t->nflip; i++) { w->flip[i].d = t->flip[i]; w->flip[i].ang = t->flip[i].rest; w->flip[i].tip_ang = 0x7FFF; }
    for (i = 0; i < t->nseg; i++) if (t->seg[i].kind == SK_DROP) t->seg[i].up = 1;
    w->next_id = 1;
    w->flipper_power = w->active_targets = 1;
    w->substeps = 1;
}

Ball *pb_add_ball(World *w, int16_t x, int16_t y, int16_t vx, int16_t vy, int8_t state)
{
    Ball *b;
    if (w->nballs >= PB_MAX_BALLS) return 0;
    b = &w->balls[w->nballs++];
    memset(b, 0, sizeof *b);
    b->id = w->next_id++;
    place(b, x, y);
    b->vx = vx; b->vy = vy; b->state = state; b->autol = -1;
    return b;
}

void pb_remove_drained(World *w)
{
    int8_t i, n = 0;
    for (i = 0; i < w->nballs; i++) if (w->balls[i].state != BS_DRAINED) w->balls[n++] = w->balls[i];
    w->nballs = n;
}

/* the substep: n of them make the 1/120 s step */
int16_t sub_kpos;                       /* also zmath.asm's advance() */
static int16_t sub_acc_up, sub_acc_down, sub_kang;

static void flipper_step(World *w, Flipper *f)
{
    int8_t want = f->held && w->flipper_power;
    int16_t lo = f->d.rest < f->d.up ? f->d.rest : f->d.up, hi = f->d.rest < f->d.up ? f->d.up : f->d.rest;
    int16_t target = want ? f->d.up : f->d.rest;
    int8_t sgn = target > f->ang ? 1 : target < f->ang ? -1 : 0;
    int16_t wmax = want ? 42 * 256 : 26 * 256;
    int16_t acc = want ? sub_acc_up : sub_acc_down;      /* 9000 / 5000 rad/s^2 for the substep */
    if (sgn == 0 && f->w == 0) return;                   /* at rest (the rest changes nothing) */
    if (sgn == 0) f->w = 0;
    else {
        f->w += sgn * acc;
        if (ABS16(f->w) > wmax) f->w = sgn * wmax;
        if ((f->w > 0 ? 1 : f->w < 0 ? -1 : 0) != sgn) f->w = sgn * (ABS16(f->w) < acc ? ABS16(f->w) : acc);
    }
    f->ang += (int16_t)(mul16(f->w, sub_kang) >> 16);
    if (f->ang <= lo) {
        f->ang = lo;
        if (f->w < 0) { if (ABS16(f->w) > 8 * 256) emit(w, EV_FLIPSTOP, f->d.px, f->d.py, f->d.side, f->ang == f->d.up, 0, 0); f->w = 0; }
    }
    if (f->ang >= hi) {
        f->ang = hi;
        if (f->w > 0) { if (ABS16(f->w) > 8 * 256) emit(w, EV_FLIPSTOP, f->d.px, f->d.py, f->d.side, f->ang == f->d.up, 0, 0); f->w = 0; }
    }
    f->tip_ang = 0x7FFF;                                 /* the tip moved */
}

static int8_t flipper_moving(const World *w, const Flipper *f)
{
    int16_t target = (f->held && w->flipper_power) ? f->d.up : f->d.rest;
    return f->w != 0 || f->ang != target;
}

/* x += v * dt: P with an 8-bit fraction (v * sub_kpos >> 11: 1/256 P);
   zmath.asm on the Z80 */
#ifdef __SDCC
extern void advance(int16_t *p, uint8_t *frac, int16_t v);
#else
static void advance(int16_t *p, uint8_t *frac, int16_t v)
{
    int32_t q = ((int32_t)*p << 8) + *frac + (mul16(v, sub_kpos) >> 11);
    *p = (int16_t)(q >> 8);
    *frac = (uint8_t)q;
}
#endif

static void move_ball(World *w, Ball *b, int16_t grav)
{
    Table *t = w->t;
    int16_t x0 = b->x, y0 = b->y;
    int16_t gx, gy;
    uint16_t k, end;
    int8_t i;

    b->vy -= grav;
    if (ABS16(b->vx) + ABS16(b->vy) > PB_MAX_SPEED) {
        int16_t sp = hypot16(b->vx, b->vy);
        if (sp > PB_MAX_SPEED) { int16_t s = div_q14(PB_MAX_SPEED, sp); b->vx = mulq14(b->vx, s); b->vy = mulq14(b->vy, s); }
    }
    advance(&b->x, &b->xf, b->vx);
    advance(&b->y, &b->yf, b->vy);

    gx = b->x >> 10;                    /* 64 mm cells */
    gy = (b->y + 1024) >> 10;
    if (gx < 0) gx = 0;
    if (gx >= PB_GX) gx = PB_GX - 1;
    if (gy < 0) gy = 0;
    if (gy >= PB_GY) gy = PB_GY - 1;
    k = t->grid_at[gy * PB_GX + gx];
    end = t->grid_at[gy * PB_GX + gx + 1];
    for (; k < end; k++) {
        uint16_t entry = t->grid[k];
        uint8_t type = entry >> 12;
        uint16_t idx = entry & 0xFFF;
        int16_t bx = b->x, by = b->y;
        if (type == 0) {                                    /* a wall, gate, sling or drop target */
            Seg *s = &t->seg[idx];
            int16_t imp;
            if (bx < s->bb[0] || bx > s->bb[2] || by < s->bb[1] || by > s->bb[3]) continue;
            if (s->kind == SK_DROP && !s->up) continue;
            w->tests++;
            imp = hit_segment(b, s);
            if (imp <= 0) continue;
            if (s->kind == SK_DROP) {
                if (imp > V_MM(90)) { s->up = 0; emit(w, EV_DROP, b->x, b->y, s->id, 0, imp >> 2, b->id); }
            } else if (s->kind == SK_SLING) {
                if (imp > V_MM(140) && w->active_targets) {
                    int16_t nx = -s->uy, ny = s->ux;
                    if (mul16(b->x - s->ax, nx) + mul16(b->y - s->ay, ny) < 0) { nx = -nx; ny = -ny; }
                    b->vx += mulq14(V_MM(950), nx); b->vy += mulq14(V_MM(950), ny);
                    emit(w, EV_SLING, b->x, b->y, s->id, 0, 0, b->id);
                }
            } else if (imp > V_MM(250)) emit(w, EV_WALL, b->x, b->y, 0, 0, imp >> 2, b->id);
        } else if (type == 1) {                             /* a post or a pop bumper */
            const Circ *c = &t->circ[idx];
            int16_t nx, ny, imp;
            w->tests++;
            imp = hit_circle(b, c, &nx, &ny);
            if (imp < 0) continue;
            if (c->bumper) {
                if (w->active_targets) {
                    int16_t out = (int16_t)((mul16(b->vx, nx) + mul16(b->vy, ny)) >> 14);
                    if (out < V_MM(1050)) { int16_t add = V_MM(1050) - out; b->vx += mulq14(nx, add); b->vy += mulq14(ny, add); }
                    emit(w, EV_BUMPER, c->cx, c->cy, c->id, 0, 0, b->id);
                }
            } else if (imp > V_MM(250)) emit(w, EV_WALL, b->x, b->y, 0, 0, imp >> 2, b->id);
        } else {                                            /* a sensor */
            const Sensor *s = &t->sens[idx];
            int8_t c;
            if (bx < s->bb[0] || bx > s->bb[2] || by < s->bb[1] || by > s->bb[3]) continue;
            c = crossed(x0, y0, b->x, b->y, s);
            if (!c) continue;
            if (s->kind == SN_SPIN) {
                Spinner *sp2 = &w->spin[s->idx];
                /* |vy| / 28 rad/s, at most 70: in W, |vy|_V * 16 / 7 */
                int16_t add = (int16_t)(mul16(ABS16(b->vy), 16) / 7);
                sp2->w += add < 70 * 256 ? add : 70 * 256;
                emit(w, EV_SPINHIT, b->x, b->y, s->idx, b->vy > 0, 0, b->id);
                continue;
            }
            if (s->kind == SN_RAMPIN) {
                if (b->vy > 0) {
                    b->state = BS_RAMP; b->ramp = s->idx; b->rs = 0;
                    b->rv = mulq14(hypot16(b->vx, b->vy), Q14(0.92));
                    emit(w, EV_RAMPENTER, b->x, b->y, s->idx, 1, 0, b->id);
                    return;
                }
                continue;
            }
            if (w->nev < PB_MAX_EV) {
                emit(w, EV_SENSOR, b->x, b->y, s->idx, b->vy > 0, 0, b->id);
                w->ev[w->nev - 1].kind = s->kind;
            }
        }
    }
    /* the flippers */
    for (i = 0; i < w->nflip; i++) {
        Flipper *f = &w->flip[i];
        int16_t imp, reach = f->d.len + f->d.r1 + PB_R;
        if (ABS16(b->x - f->d.px) > reach || ABS16(b->y - f->d.py) > reach) continue;
        w->tests++;
        imp = hit_flipper(b, f);
        if (imp > V_MM(300)) emit(w, EV_FLIPHIT, b->x, b->y, f->d.side, 0, imp >> 2, b->id);
    }
    /* the saucer catches a ball that isn't moving too fast */
    if (b->nocatch <= 0 && ABS16(b->x - t->saucer_x) < t->saucer_r && ABS16(b->y - t->saucer_y) < t->saucer_r &&
        hypot16(b->x - t->saucer_x, b->y - t->saucer_y) < t->saucer_r && hypot16(b->vx, b->vy) < V_MM(1300)) {
        b->state = BS_SAUCER; place(b, t->saucer_x, t->saucer_y); b->vx = b->vy = 0;
        b->hold = PB_SECS(1.2);
        emit(w, EV_SAUCER, b->x, b->y, 0, 0, 0, b->id);
        return;
    }
    /* resting on the plunger */
    if (b->x > P_MM(478) && b->y < t->plunger_rest + P_MM(4) && hypot16(b->vx, b->vy) < V_MM(80)) {
        b->state = BS_LANE; place(b, t->plunger_x, t->plunger_rest); b->vx = b->vy = 0;
        emit(w, EV_LANEREST, b->x, b->y, 0, 0, 0, b->id);
    }
    if (b->y < P_MM(-40)) { b->state = BS_DRAINED; emit(w, EV_DRAIN, b->x, b->y, 0, 0, 0, b->id); }
}

/* A ball on a ramp: one-dimensional motion along the rail, pulled by the slope. */
static void ramp_step(World *w, Ball *b)
{
    const Ramp *r = &w->t->ramp[b->ramp];
    int16_t x, y, z, slope, dx, dy, L = r->prof[r->n - 1];
    ramp_at(r, b->rs, &x, &y, &z, &slope, 0, 0);
    /* rv += (-3200 slope - 120 sign(rv)) h: in V, 106.7 slope and 4 */
    b->rv -= (int16_t)(mul16(slope, 1707) >> 18) + (b->rv > 0 ? 4 : b->rv < 0 ? -4 : 0);
    b->rs += b->rv / 30;                /* V for 1/120 s, in P */
    ramp_at(r, b->rs, &x, &y, &z, 0, &dx, &dy);
    place(b, x, y); b->z = z;
    b->vx = mulq14(dx, b->rv); b->vy = mulq14(dy, b->rv);
    if (b->rs <= 0 && b->rv <= 0) {     /* didn't make it: rolls back out of the entrance */
        b->state = BS_PLAY; b->z = 0;
        place(b, r->pts[0][0] * 16, (r->pts[0][1] - 4) * 16);
        b->vx = 0; b->vy = b->rv < V_MM(-120) ? b->rv : V_MM(-120);
        emit(w, EV_RAMPFAIL, b->x, b->y, b->ramp, 0, 0, b->id);
    } else if (b->rs >= L) {
        int16_t v = -(ABS16(b->rv) >> 1);
        b->state = BS_PLAY; b->z = 0;
        place(b, r->exit_x, r->exit_y);
        b->vx = 0; b->vy = r->exit_vy < v ? r->exit_vy : v;
        emit(w, EV_RAMP, b->x, b->y, b->ramp, 0, 0, b->id);
    }
}

/* One fixed step (1/120 s), split into substeps (see pb.h). */
void pb_world_step(World *w)
{
    int16_t fast = 0, tip = 0, n, k, done = 0, gq, gr;
    int8_t i, j, live = 0;

    for (i = 0; i < w->nballs; i++) {
        Ball *b = &w->balls[i];
        if (b->nocatch > 0) b->nocatch--;
        if (b->state != BS_PLAY) continue;
        live++;
        b->vx -= b->vx / 3000; b->vy -= b->vy / 3000;     /* rolling resistance, 0.04 h */
        { int16_t s = hypot16(b->vx, b->vy) + PB_GRAVITY_STEP; if (s > fast) fast = s; }
    }
    for (i = 0; i < w->nflip; i++)
        if (flipper_moving(w, &w->flip[i])) {
            int16_t tp = (int16_t)(mul16(w->flip[i].d.len + w->flip[i].d.r2, 21) >> 1);   /* 42 rad/s, in V */
            if (tp > tip) tip = tp;
        }
    /* no substep moves anything more than a quarter of the ball's radius:
       (fast + tip) / 30 P a step, PB_R / 4 = 54 P */
    n = live ? (int16_t)(((int32_t)fast + tip + 1619) / 1620) : 1;
    if (n < 1) n = 1;
    if (n > 40) n = 40;
    w->substeps = (int8_t)n;
    sub_acc_up = (int16_t)(19200 / n);                     /* 9000 rad/s^2 / 120 / n, W */
    sub_acc_down = (int16_t)(10667 / n);
    sub_kang = (int16_t)(8738 / n);                        /* W -> A for the substep, Q16 */
    sub_kpos = (int16_t)(17476 / n);                       /* V -> 1/256 P for the substep, Q11 */
    gq = PB_GRAVITY_STEP / n;
    gr = PB_GRAVITY_STEP % n;
    for (k = 0; k < n; k++) {
        int16_t grav = gq + (done < gr);                  /* port: adds up to the step's, no divide */
        done++;
        for (i = 0; i < w->nflip; i++) flipper_step(w, &w->flip[i]);
        for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_PLAY) move_ball(w, &w->balls[i], grav);
        for (i = 0; i < w->nballs; i++)
            for (j = i + 1; j < w->nballs; j++)
                if (w->balls[i].state == BS_PLAY && w->balls[j].state == BS_PLAY) hit_balls(&w->balls[i], &w->balls[j]);
    }
    for (i = 0; i < w->nballs; i++) {
        Ball *b = &w->balls[i];
        if (b->state == BS_RAMP) ramp_step(w, b);
        else if (b->state == BS_SAUCER) { b->hold--; b->vx = b->vy = 0; }
        else if (b->state == BS_LANE) { b->vx = b->vy = 0; }
    }
    /* the spinners keep turning on their own and slow down */
    for (i = 0; i < w->t->nspin; i++) {
        Spinner *sp = &w->spin[i];
        if (sp->w <= 0) continue;
        sp->w -= 11 + mulq14(sp->w, 123);                  /* (5 rad/s + 0.9 w) h */
        if (sp->w < 0) sp->w = 0;
        sp->ang += (int16_t)(mul16(sp->w, 2) / 15);        /* w h, A */
        while (sp->ang >= PI_A) { sp->ang -= PI_A; emit(w, EV_SPIN, w->t->spin[i].x, w->t->spin[i].y, i, 0, 0, 0); }
    }
}

void pb_kick_saucer(World *w, Ball *b, int16_t jitter)
{
    int8_t right = w->t->saucer_x > PB_MID;            /* kicks out towards the middle */
    b->state = BS_PLAY;
    place(b, w->t->saucer_x + (right ? P_MM(-4) : P_MM(4)), w->t->saucer_y - P_MM(4));
    b->vx = V_MM(520) + jitter; b->vy = V_MM(-820);
    if (right) b->vx = -b->vx;
    b->nocatch = PB_SECS(0.5);
    emit(w, EV_KICK, b->x, b->y, 0, 0, 0, b->id);
}

void pb_plunge(World *w, Ball *b, int16_t pull)
{
    pull = clamp16(pull, 0, 1000);
    b->state = BS_PLAY;
    b->vx = 0; b->vy = V_MM(700) + (int16_t)(mul16(pull, 172) / 10);   /* 700 + 4300 pull mm/s */
    place(b, b->x, w->t->plunger_rest + P_MM(1));
    emit(w, EV_PLUNGE, b->x, b->y, 0, 0, pull, b->id);
}

void pb_nudge(World *w, int16_t dx, int16_t dy)
{
    int8_t i;
    for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_PLAY) { w->balls[i].vx += dx; w->balls[i].vy += dy; }
}
