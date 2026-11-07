/*
 * Planet Chomp rules: the original's game.c (GameDirector.cs: states,
 * scoring, keys, levels, scatter/chase clock; Ghost.cs: the chasers' brain
 * on a sphere; Chomper.cs: buffered screen-relative steering; Mover.cs: rail
 * movement). Times are 50 Hz ticks; positions fixed point.
 *
 * port: the same statements with 16-bit vectors (pc.h); vec3.h's products
 * are the original's, so the game takes the same decisions on the same
 * ticks (tools/compare.c checks it against the original). Logging is
 * zx_log ("pc: ..."), the test reads it.
 */
#include <string.h>
#include "pc.h"
#include "game.h"

const char *const ghost_name[4] = { "GLOOM", "SPARK", "MIST", "MOSS" };

#define START_LIVES   3
#define EXTRA_LIFE_AT 10000
#define CATCH_COS     16330          /* about 1 world unit apart (cos 0.08 rad), Q14 */

static const int32_t mode_ticks[8] = { 350, 1000, 350, 1000, 250, 1000, 250, 0x7FFFFFFFL };

/* ---- movement on rails (Mover.cs) ---- */

typedef int8_t (*Chooser)(void *ctx, int16_t cell, uint8_t cruising);
typedef void (*Arrived)(void *ctx, int16_t cell);

static void mover_place(Mover *m, int16_t cell, const V3 *facing)
{
    int16_t d = v3_dot(facing, DIR(cell));
    V3 t;
    m->from = cell; m->to = -1; m->prev = -1; m->t = 0; m->moving = 0; m->exitk = -1; m->hd_k = -1;
    v3_scale(&t, DIR(cell), d);
    v3_sub(&t, facing, &t);
    v3_norm(&m->heading, &t);
}

void mover_dir(V3 *r, const Mover *m)
{
    const V3 *a, *b;
    int16_t t;
    V3 p;
    if (!m->moving) { *r = *DIR(m->from); return; }
    t = (int16_t)(m->t >> 2);                            /* Q14 */
    a = DIR(m->from);
    b = DIR(m->to);
    p.x = a->x + mulq14(b->x - a->x, t);
    p.y = a->y + mulq14(b->y - a->y, t);
    p.z = a->z + mulq14(b->z - a->z, t);
    v3_norm(r, &p);
}

int16_t mover_nearest(const Mover *m) { return m->moving && m->t > 32768 ? m->to : m->from; }

static void mover_reverse(Mover *m)
{
    int16_t f;
    uint8_t back;
    if (!m->moving) return;
    back = REV(m->from, m->exitk);
    f = m->from; m->from = m->to; m->to = f;
    m->exitk = (int8_t)back;
    m->t = 65536L - m->t;
    m->prev = m->from;
    v3_scale(&m->heading, &m->heading, -ONE14);
    m->hd_k = -1;
}

static void mover_advance(Mover *m, int16_t dist, Chooser choose, Arrived arrived, void *ctx)
{
    uint8_t cruising = m->moving, guard;
    for (guard = 0; dist > 0 && guard < 8; guard++) {
        int16_t len;
        int32_t remain;
        if (!m->moving) {
            int8_t k = choose(ctx, m->from, cruising);
            if (k < 0) break;
            m->to = NB(m->from, k);
            m->exitk = k;
            m->t = 0;
            m->moving = 1;
        }
        len = LEN(m->from, m->exitk);
        remain = ((65536L - m->t) * len) >> 16;
        if (dist < remain) {
            m->t += ((int32_t)dist << 16) / len;
            dist = 0;
        } else {
            uint8_t back = REV(m->from, m->exitk);
            dist -= (int16_t)remain;
            m->prev = m->from;
            m->from = m->to;
            m->to = -1;
            m->t = 0;
            m->moving = 0;
            mz_tan(&m->heading, m->from, back);
            v3_scale(&m->heading, &m->heading, -ONE14);
            m->hd_k = -1;
            cruising = 1;
            if (arrived) arrived(ctx, m->from);
        }
    }
    if (m->moving && (m->hd_from != m->from || m->hd_k != m->exitk)) {
        /* port: the original sets this every tick; it only changes with the edge */
        mz_tan(&m->heading, m->from, m->exitk);
        m->hd_from = m->from;
        m->hd_k = m->exitk;
    }
}

/* exit at c best matching a wanted direction (projected onto the surface) */
static int8_t best_exit(int16_t c, const V3 *want_in, int16_t min_dot)
{
    int8_t k, best = -1;
    int16_t bd = min_dot, d, dot;
    V3 want, t;
    if (v3_zero(want_in)) return -1;
    d = v3_dot(want_in, DIR(c));
    v3_scale(&t, DIR(c), d);
    v3_sub(&t, want_in, &t);
    v3_norm(&want, &t);
    for (k = 0; k < 4; k++) {
        if (!OPEN(c, k)) continue;
        mz_tan(&t, c, k);
        dot = v3_dot(&t, &want);
        if (dot > bd) { bd = dot; best = k; }
    }
    return best;
}

/* ---- the player (Chomper.cs) ---- */

static int8_t pend_x, pend_y, last_x, last_y;     /* screen direction, -1/0/1 */
static uint16_t held_now;

static uint8_t held_dir(int8_t x, int8_t y)
{
    return (x > 0 && (held_now & PAD_RIGHT)) || (x < 0 && (held_now & PAD_LEFT)) ||
           (y > 0 && (held_now & PAD_UP)) || (y < 0 && (held_now & PAD_DOWN));
}

static void consume_pending(void)
{
    if (!held_dir(pend_x, pend_y)) pend_x = pend_y = 0;
}

/* screen direction -> surface tangent (camera right/up on the planet) */
static void wish(V3 *w)
{
    V3 r;
    v3_cross(&r, &g.cam.focus, &g.cam.up);
    w->x = r.x * pend_x + g.cam.up.x * pend_y;
    w->y = r.y * pend_x + g.cam.up.y * pend_y;
    w->z = r.z * pend_x + g.cam.up.z * pend_y;
}

/* ---- autopilot (GameDirector.BotChoose / BotDodge): eats the nearest
 * crumb it can reach without passing next to a spook, grabs a key that's
 * close, hunts frightened spooks; otherwise heads away from the spooks ---- */

static int8_t bot_choose(int16_t c)
{
    uint8_t *danger = pc_work.bot.danger;
    int16_t *dist = pc_work.bot.dist, *parent = pc_work.bot.parent, *q = pc_work.bot.q;
    int16_t i, head = 0, tail = 0, target = -1, best = 0x7FFF, x, nb, n;
    uint8_t k, want;
    memset(danger, 0, CELLS);
    for (i = 0; i < CELLS; i++) { dist[i] = 0x7FFF; parent[i] = -1; }
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        int16_t gc;
        if (!ghost_dangerous(gh) || gh->mode == GM_NEST) continue;
        gc = mover_nearest(&gh->mv);
        danger[gc] = 1;
        for (k = 0; k < 4; k++) if (OPEN(gc, k)) danger[NB(gc, k)] = 1;
        if (gh->mv.moving) danger[gh->mv.from == gc ? gh->mv.to : gh->mv.from] = 1;
    }
    dist[c] = 0;
    q[tail++] = c;
    while (head < tail) {
        x = q[head++];
        for (k = 0; k < 4; k++) {
            nb = NB(x, k);
            if (!OPEN(x, k) || dist[nb] != 0x7FFF || danger[nb]) continue;
            dist[nb] = dist[x] + 1;
            parent[nb] = x;
            q[tail++] = nb;
        }
    }
    for (i = 1; i < tail; i++) {
        n = q[i];
        want = crumb[n] || (key[n] && dist[n] < 6);
        if (g.power_left > TICKS(1.5)) {
            for (k = 0; k < 4; k++)
                if (g.ghost[k].fright && g.ghost[k].mode == GM_ACTIVE && mover_nearest(&g.ghost[k].mv) == n)
                    want = dist[n] < 12;
        }
        if (want && dist[n] < best) { best = dist[n]; target = n; }
    }
    if (target >= 0) {
        int16_t step = target;
        while (parent[step] != c) step = parent[step];
        for (k = 0; k < 4; k++) if (NB(c, k) == step && OPEN(c, k)) return (int8_t)k;
    }
    {
        /* nothing safe to eat: the exit farthest from the nearest spook */
        int8_t bk = -1;
        int16_t bscore = -0x7FFF;    /* port: int16 (the scores are dot products) */
        int16_t worst, d;
        uint8_t first = 1;
        V3 gd;
        for (k = 0; k < 4; k++) {
            worst = -ONE14;
            if (!OPEN(c, k)) continue;
            for (i = 0; i < 4; i++)
                if (ghost_dangerous(&g.ghost[i])) {
                    mover_dir(&gd, &g.ghost[i].mv);
                    d = v3_dot(DIR(NB(c, k)), &gd);
                    if (d > worst) worst = d;
                }
            if (first || -worst > bscore) { bscore = -worst; bk = (int8_t)k; first = 0; }
        }
        return bk;
    }
}

int8_t bot_exit(int16_t c) { return bot_choose(c); }

static void bot_dodge(void)
{
    uint8_t i;
    V3 p, gd, t, tn;
    int16_t d;
    if (!g.player.moving) return;
    mover_dir(&p, &g.player);
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        if (!ghost_dangerous(gh) || gh->mode == GM_NEST) continue;
        mover_dir(&gd, &gh->mv);
        d = v3_dot(&gd, &p);
        if (d < 15960) continue;                          /* farther than ~1.3 cells */
        v3_scale(&t, &p, d);
        v3_sub(&t, &gd, &t);
        if (v3_zero(&t)) continue;
        v3_norm(&tn, &t);
        if (v3_dot(&tn, &g.player.heading) > 8192) {
            mover_reverse(&g.player);
            return;
        }
    }
}

static int8_t player_choose(void *ctx, int16_t c, uint8_t cruising)
{
    V3 w;
    (void)ctx;
    if (g.demo) return bot_choose(c);
    if (pend_x || pend_y) {
        int8_t k;
        wish(&w);
        k = best_exit(c, &w, 7373);                   /* 0.45 */
        if (k >= 0) { consume_pending(); return k; }
    }
    return cruising ? best_exit(c, &g.player.heading, 8192) : -1;   /* 0.5 */
}

static void start_power(void);
static void add_score(int32_t pts);
static void set_state(uint8_t s);

static void player_arrived(void *ctx, int16_t c)
{
    (void)ctx;
    if (crumb[c]) {
        crumb[c] = 0;
        g.crumbs_left--;
        add_score(10);
        sfx_crumb();
        if (g.crumbs_left % 10 == 0)
            PC_LOG("pc: chomp score=%ld left=%d\n", g.score, g.crumbs_left);
        if (g.crumbs_left == 0) {
            sfx_ambience(0);
            sfx_play(SFX_CLEAR);
            g.power_left = 0;
            g.powered = 0;
            set_state(GS_CLEAR);
            PC_LOG("pc: level clear level=%u score=%ld\n", g.level, g.score);
        }
    } else if (key[c]) {
        key[c] = 0;
        add_score(50);
        sfx_play(SFX_KEY);
        g.keys_taken++;
        start_power();
    }
}

static void player_tick(void)
{
    if (g.player.moving && (pend_x || pend_y)) {
        V3 w, wn;
        int16_t d;
        wish(&w);
        v3_norm(&wn, &w);
        d = v3_dot(&wn, &g.player.heading);
        if (!v3_zero(&w) && d < -8192) {
            mover_reverse(&g.player);
            consume_pending();
        }
    }
    mover_advance(&g.player, g.player_speed / 50, player_choose, player_arrived, 0);
    if (g.player.moving) g.mouth++;
}

/* ---- the spooks (Ghost.cs) ---- */

static int16_t grnd(Ghost *gh, int16_t n)
{
    gh->rng = gh->rng * 1103515245UL + 12345UL;
    return (int16_t)((uint16_t)(gh->rng >> 16) % (uint16_t)n);
}

uint8_t ghost_dangerous(const Ghost *gh) { return gh->mode != GM_EATEN && !gh->fright; }

/* point `cells` cells ahead of dir along heading: dir cos a + heading sin a */
static void ahead(V3 *r, const V3 *dir, const V3 *heading, uint8_t cells)
{
    /* one cell is ~0.175 rad: cos/sin of 2 and 4 cells, Q14 */
    int16_t cs = cells == 2 ? 15385 : 12531, sn = cells == 2 ? 5632 : 10555;
    V3 a, b;
    v3_scale(&a, dir, cs);
    v3_scale(&b, heading, sn);
    v3_add(&a, &a, &b);
    v3_norm(r, &a);
}

static void ghost_target(V3 *r, Ghost *gh)
{
    V3 pd, gd;
    mover_dir(&pd, &g.player);
    if (g.scatter) { *r = gh->scatter; return; }
    switch (gh->kind) {
    case GK_AMBUSHER:
        ahead(r, &pd, &g.player.heading, 4);
        return;
    case GK_FLANKER: {
        V3 pivot, chaser;
        int32_t tx, ty, tz;              /* port: 2 pivot - chaser can pass 16 bits */
        ahead(&pivot, &pd, &g.player.heading, 2);
        mover_dir(&chaser, &g.ghost[0].mv);
        tx = 2L * pivot.x - chaser.x;
        ty = 2L * pivot.y - chaser.y;
        tz = 2L * pivot.z - chaser.z;
        if (tx | ty | tz) v3_norm32(r, tx, ty, tz);
        else *r = pd;
        return;
    }
    case GK_DRIFTER:
        /* chase when more than 7 cells away (cos 1.22 rad = 0.34) */
        mover_dir(&gd, &gh->mv);
        *r = v3_dot(&gd, &pd) < 5570 ? pd : gh->scatter;
        return;
    default:
        *r = pd;
    }
}

static int8_t ghost_choose(void *ctx, int16_t c, uint8_t cruising)
{
    Ghost *gh = (Ghost *)ctx;
    int16_t back = gh->mv.prev, bd = -0x7FFF, d;
    uint8_t options = 0, allow_back, k;
    int8_t best = -1;
    V3 target;
    (void)cruising;
    for (k = 0; k < 4; k++) if (OPEN(c, k) && NB(c, k) != back) options++;
    allow_back = options == 0;
    if (gh->mode == GM_EATEN) {
        int8_t bk = -1;
        int16_t bdist = 0x7FFF;
        for (k = 0; k < 4; k++) {
            if (!OPEN(c, k)) continue;
            if (nest_dist[NB(c, k)] < bdist) { bdist = nest_dist[NB(c, k)]; bk = (int8_t)k; }
        }
        return bk;
    }
    if (gh->fright) {
        int16_t pick = grnd(gh, options > 0 ? options : 1);
        for (k = 0; k < 4; k++) {
            if (!OPEN(c, k) || (!allow_back && NB(c, k) == back)) continue;
            if (pick-- == 0) return (int8_t)k;
        }
    }
    ghost_target(&target, gh);
    for (k = 0; k < 4; k++) {
        if (!OPEN(c, k) || (!allow_back && NB(c, k) == back)) continue;
        d = v3_dot(DIR(NB(c, k)), &target);      /* closest by angle = largest dot */
        if (d > bd) { bd = d; best = (int8_t)k; }
    }
    return best;
}

static void ghost_arrived(void *ctx, int16_t c)
{
    Ghost *gh = (Ghost *)ctx;
    if (gh->mode == GM_EATEN && c == mz_nest) {
        gh->mode = GM_NEST;                 /* home: regrow, out again shortly */
        gh->release_at = g.life + TICKS(1.2);
    }
}

static void ghost_tick(Ghost *gh)
{
    int16_t speed = g.ghost_speed;
    if (gh->mode == GM_NEST) {
        if (g.life >= gh->release_at) gh->mode = GM_ACTIVE;
        else return;
    }
    if (gh->fright) speed = (int16_t)(((int32_t)speed * 55) / 100);
    if (gh->mode == GM_EATEN) speed = (int16_t)(((int32_t)speed * 24) / 10);
    mover_advance(&gh->mv, speed / 50, ghost_choose, ghost_arrived, gh);
}

void ghost_world(V3 *r, const Ghost *gh)
{
    V3 n, t;
    mover_dir(&n, &gh->mv);
    v3_scale(r, &n, PLANET_R + Q8(0.52));
    if (gh->mode == GM_NEST) {
        /* huddle round the nest, bobbing */
        static const int16_t bob[16] = { 0, 12, 22, 28, 30, 28, 22, 12, 0, -12, -22, -28, -30, -28, -22, -12 };
        int16_t lift = Q8(0.15) + bob[((g.clock >> 2) + gh->slot * 4) & 15];
        mz_tan(&t, mz_nest, gh->slot);
        v3_scale(&t, &t, Q8(0.55));
        v3_add(r, r, &t);
        v3_scale(&t, &n, lift);
        v3_add(r, r, &t);
    }
}

void player_world(V3 *r)
{
    V3 d;
    mover_dir(&d, &g.player);
    v3_scale(r, &d, PLANET_R + Q8(0.55));
}

/* ---- the director (GameDirector.cs) ---- */

static void set_state(uint8_t s)
{
    g.state = s;
    g.timer = 0;
    PC_LOG("pc: state=%u score=%ld lives=%d level=%u crumbs=%d\n", s, g.score, g.lives, g.level, g.crumbs_left);
}

static void add_score(int32_t pts)
{
    g.score += pts;
    if (g.score > g.hiscore && !g.demo) g.hiscore = g.score;   /* demos don't count */
    if (!g.extra_given && g.score >= EXTRA_LIFE_AT) {
        g.extra_given = 1;
        g.lives++;
        sfx_play(SFX_EXTRA);
    }
}

static void build_level(void)
{
    int16_t c, lv = g.level - 1, ps, gp;
    uint8_t i;
    mz_build(2026UL + 7919UL * (uint32_t)g.level);
    mz_bfs(mz_nest, nest_dist);
    g.crumbs_total = 0;
    memset(crumb, 0, CELLS);
    memset(key, 0, CELLS);
    for (c = 0; c < CELLS; c++) {
        if (c == mz_start || c == mz_nest) continue;
        for (i = 0; i < 4; i++) if (mz_keys[i] == c) break;
        if (i < 4) { key[c] = 1; continue; }
        crumb[c] = 1;
        g.crumbs_total++;
    }
    g.crumbs_left = g.crumbs_total;
    ps = 1485 + 77 * lv;                           /* (5.8 + 0.3 lv) * 256 */
    if (ps > Q8(7.4)) ps = Q8(7.4);
    g.player_speed = ps;
    gp = 84 + 35 * lv / 10;
    if (gp > 97) gp = 97;
    g.ghost_speed = (int16_t)(((int32_t)ps * gp) / 100);
    PC_LOG("pc: level %u seed=%ld crumbs=%d\n", g.level, (long)(2026 + 7919L * g.level), g.crumbs_total);
}

static void reset_actors(void)
{
    static const int16_t release[4] = { 25, 150, 300, 450 };    /* 0.5, 3, 6, 9 s */
    int16_t s = mz_start, denom = 6 + 4 * g.level;               /* (0.6 + 0.4 L) * 10 */
    V3 facing_v, t;
    const V3 *facing = &facing_v;
    uint8_t i;
    mz_tan(&facing_v, s, 0);
    if (denom < 10) denom = 10;
    mover_place(&g.player, s, facing);
    g.powered = 0;
    pend_x = pend_y = last_x = last_y = 0;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        mz_tan(&t, mz_nest, i);
        mover_place(&gh->mv, mz_nest, &t);
        gh->mode = GM_NEST;
        gh->fright = 0;
        gh->release_at = (int32_t)release[i] * 10 / denom;
    }
    g.power_left = 0;
    g.mode_index = 0;
    g.mode_timer = 0;
    g.scatter = 1;
    g.life = 0;
    for (i = 0; i < 8; i++) g.popup[i].age = -1;
    g.cam.focus = *DIR(s);
    v3_scale(&t, DIR(s), v3_dot(facing, DIR(s)));
    v3_sub(&t, facing, &t);
    v3_norm(&g.cam.up, &t);
    sfx_ambience(0);
}

static void start_game(void)
{
    g.score = 0;
    g.lives = START_LIVES;
    g.level = 1;
    g.extra_given = 0;
    g.ghosts_eaten = 0;
    g.keys_taken = 0;
    build_level();
    reset_actors();
    set_state(GS_READY);
    sfx_play(SFX_START);
}

static void start_power(void)
{
    uint8_t i;
    int32_t t = TICKS(9) - TICKS(1) * (g.level - 1);
    if (t < TICKS(3)) t = TICKS(3);
    g.power_total = g.power_left = t;
    g.combo = 0;
    g.powered = 1;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        if (gh->mode == GM_EATEN) continue;
        if (gh->mode == GM_ACTIVE) mover_reverse(&gh->mv);
        gh->fright = 1;
    }
    sfx_ambience(2);
    PC_LOG("pc: key: invincible for %ld ticks\n", t);
}

static void end_power(void)
{
    uint8_t i;
    g.power_left = 0;
    g.powered = 0;
    for (i = 0; i < 4; i++) if (g.ghost[i].mode != GM_EATEN) g.ghost[i].fright = 0;
    sfx_ambience(1);
}

static void popup(const V3 *world, int16_t pts)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
        if (g.popup[i].age < 0) {
            g.popup[i].world = *world;
            g.popup[i].pts = pts;
            g.popup[i].age = 0;
            return;
        }
}

static void check_catches(void)
{
    V3 pd, gd, w;
    uint8_t i;
    mover_dir(&pd, &g.player);
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        if (gh->mode == GM_EATEN) continue;
        mover_dir(&gd, &gh->mv);
        if (v3_dot(&gd, &pd) < CATCH_COS) continue;
        if (gh->fright) {
            int16_t pts = 200 << (g.combo < 3 ? g.combo : 3);
            g.combo++;
            add_score(pts);
            gh->fright = 0;
            gh->mode = GM_EATEN;
            if (!gh->mv.moving && gh->mv.from == mz_nest) ghost_arrived(gh, mz_nest);
            ghost_world(&w, gh);
            popup(&w, pts);
            sfx_play(SFX_EAT);
            g.ghosts_eaten++;
            PC_LOG("pc: eat %s +%d score=%ld\n", ghost_name[i], pts, g.score);
        } else {
            PC_LOG("pc: death caught by %s\n", ghost_name[i]);
            sfx_ambience(0);
            sfx_play(SFX_DEATH);
            g.power_left = 0;
            g.powered = 0;
            set_state(GS_DYING);
            return;
        }
    }
}

static void tick_playing(void)
{
    uint8_t i;
    g.life++;
    if (g.power_left > 0) {
        if (--g.power_left <= 0) end_power();
    } else if (++g.mode_timer >= mode_ticks[g.mode_index < 7 ? g.mode_index : 7]) {
        g.mode_timer = 0;
        g.mode_index++;
        g.scatter = (g.mode_index % 2) == 0;
        for (i = 0; i < 4; i++) if (g.ghost[i].mode == GM_ACTIVE) mover_reverse(&g.ghost[i].mv);
    }
    if (g.demo) bot_dodge();
    player_tick();
    if (g.state != GS_PLAYING) return;          /* the last crumb ends the level mid-tick */
    for (i = 0; i < 4; i++) ghost_tick(&g.ghost[i]);
    check_catches();
}

/* ---- camera (PlanetCamera.cs) ---- */

static void cam_transport(const V3 *f)
{
    int16_t d = v3_dot(&g.cam.up, f);
    V3 u;
    v3_scale(&u, f, d);
    v3_sub(&u, &g.cam.up, &u);
    if (!v3_zero(&u)) v3_norm(&g.cam.up, &u);
    g.cam.focus = *f;
}

static void cam_height(int16_t target, int16_t div)
{
    g.cam.height += (target - g.cam.height) / div;
}

static void cam_follow(const V3 *target, uint16_t held)
{
    V3 t, f;
    v3_sub(&t, target, &g.cam.focus);
    v3_scale(&t, &t, 2700);
    v3_add(&t, &g.cam.focus, &t);
    v3_norm(&f, &t);
    cam_transport(&f);
    if (held & (PAD_L | PAD_R)) {               /* spin the view, ~110 deg/s */
        V3 side, a;
        int16_t s = (held & PAD_L) ? -629 : 629;
        v3_cross(&side, &g.cam.focus, &g.cam.up);
        v3_scale(&a, &g.cam.up, 16372);
        v3_scale(&t, &side, s);
        v3_add(&a, &a, &t);
        v3_norm(&g.cam.up, &a);
    }
    cam_height(g.overview ? Q8(42) : Q8(13), 9);
}

static void cam_orbit(int16_t height)
{
    static const V3 axis = { 4729, 3153, 15766 };   /* (0.3, 0.2, 1) normalised */
    V3 t, f;
    /* rotate the focus about the axis (~14 deg/s) and up about the focus */
    v3_cross(&t, &axis, &g.cam.focus);
    v3_scale(&t, &t, 80);
    v3_add(&t, &g.cam.focus, &t);
    v3_norm(&f, &t);
    cam_transport(&f);
    v3_cross(&t, &g.cam.focus, &g.cam.up);
    v3_scale(&t, &t, 34);
    v3_add(&t, &g.cam.up, &t);
    v3_norm(&g.cam.up, &t);
    cam_height(height, 25);
}

/* ---- top level ---- */

void game_init(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        V3 corner;
        corner.x = (i % 2 == 0) ? ONE14 : -ONE14;
        corner.y = (i < 2) ? ONE14 : -ONE14;
        corner.z = (i == 1 || i == 2) ? ONE14 : -ONE14;
        v3_norm(&gh->scatter, &corner);
        gh->kind = i;
        gh->slot = i;
        gh->rng = 1234 + i;
    }
    g.level = 1;
    g.cam.height = Q8(30);
    build_level();
    reset_actors();
    set_state(GS_TITLE);
}

void game_step(uint16_t held, uint16_t pressed)
{
    uint8_t i;
    V3 pd;
    if (g.demo && g.state != GS_TITLE && (pressed & (PAD_A | PAD_B | PAD_C | PAD_P))) {
        g.demo = 0;
        sfx_ambience(0);
        set_state(GS_TITLE);
        return;
    }
    held_now = held;
    g.clock++;
    g.timer++;
    for (i = 0; i < 8; i++) if (g.popup[i].age >= 0 && ++g.popup[i].age > 60) g.popup[i].age = -1;

    /* buffered steering: a press is remembered until a junction allows it */
    if (pressed & PAD_UP)    { pend_x = 0;  pend_y = 1;  last_x = 0;  last_y = 1; }
    if (pressed & PAD_DOWN)  { pend_x = 0;  pend_y = -1; last_x = 0;  last_y = -1; }
    if (pressed & PAD_LEFT)  { pend_x = -1; pend_y = 0;  last_x = -1; last_y = 0; }
    if (pressed & PAD_RIGHT) { pend_x = 1;  pend_y = 0;  last_x = 1;  last_y = 0; }
    if ((last_x || last_y) && held_dir(last_x, last_y)) { pend_x = last_x; pend_y = last_y; }
    if (pressed & PAD_C) g.overview = !g.overview;

    switch (g.state) {
    case GS_TITLE:
    case GS_OVER:
        if (g.state == GS_OVER && g.demo && g.timer > TICKS(3)) {
            g.demo = 0;                       /* demo over: back to the title */
            set_state(GS_TITLE);
            break;
        }
        if ((pressed & (PAD_A | PAD_P)) && g.timer > TICKS(0.5)) { g.demo = 0; start_game(); break; }
        if (g.state == GS_TITLE && g.timer > TICKS(15)) {
            g.demo = 1;                       /* attract mode */
            PC_LOG("pc: demo starts\n");
            start_game();
            break;
        }
        cam_orbit(Q8(30));
        if (g.state == GS_TITLE) {
            g.life++;
            for (i = 0; i < 4; i++) ghost_tick(&g.ghost[i]);
        }
        break;
    case GS_READY:
        mover_dir(&pd, &g.player);
        cam_follow(&pd, held);
        if (g.timer >= ((g.level == 1 && g.lives == START_LIVES) ? TICKS(2.2) : TICKS(1.6))) {
            set_state(GS_PLAYING);
            sfx_ambience(1);
        }
        break;
    case GS_PLAYING:
        if (pressed & PAD_P) g.paused = !g.paused;
        if (g.paused) break;
        tick_playing();
        mover_dir(&pd, &g.player);
        cam_follow(&pd, held);
        break;
    case GS_DYING:
        mover_dir(&pd, &g.player);
        cam_follow(&pd, held);
        if (g.timer >= TICKS(2.2)) {
            g.lives--;
            if (g.lives > 0) {
                reset_actors();
                set_state(GS_READY);
            } else {
                if (g.score >= g.hiscore && !g.demo) g.hiscore = g.score;
                set_state(GS_OVER);
            }
        }
        break;
    case GS_CLEAR:
        mover_dir(&pd, &g.player);
        cam_follow(&pd, held);
        if (g.timer >= TICKS(2.6)) {
            g.level++;
            build_level();
            reset_actors();
            set_state(GS_READY);
        }
        break;
    }
}
