/*
 * pb_rules.c - Boing Pinball's rules: the original's (tools/orig/pb_core.c,
 * "the rules", the display, the lamps and the bot), unchanged but for the
 * units of the physics they touch (pbz.h) and types sized for the Z80;
 * those places are marked port:.
 */
#include <string.h>
#include "pbz.h"

#define ABS16(a)  ((int16_t)((a) < 0 ? -(a) : (a)))

const MissionDef pb_missions[5] = {
    { "POP BUMPERS", 25, MI_BUMPER, 40000 },
    { "TARGET BANKS", 2, MI_BANK,   50000 },
    { "SPINS",       40, MI_SPIN,   45000 },
    { "RAMP SHOTS",   3, MI_RAMP,   60000 },
    { "LANE SETS",    2, MI_LANES,  55000 },
};


#define MISSION_TIME PB_SECS(40)
#define SAVE_TIME    PB_SECS(10)

static void gev(Game *g, int8_t type, int16_t x, int16_t y, int8_t id, int32_t val)
{
    Event *e;
    if (g->nev >= PB_MAX_EV) return;
    e = &g->ev[g->nev++];
    memset(e, 0, sizeof *e);
    e->type = (int8_t)type; e->x = (int16_t)x; e->y = (int16_t)y; e->id = (int8_t)id; e->val = val;
}

void pb_fmt(char *out, int32_t n)
{
    char tmp[16];
    int8_t len = 0, i, o = 0;
    uint32_t u = (uint32_t)(n < 0 ? -n : n);
    do { tmp[len++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (n < 0) out[o++] = '-';
    for (i = len - 1; i >= 0; i--) {
        out[o++] = tmp[i];
        if (i && i % 3 == 0) out[o++] = ',';
    }
    out[o] = 0;
}

static void copy(char *d, const char *s, int8_t max)
{
    int8_t i = 0;
    if (s) for (; s[i] && i < max - 1; i++) d[i] = s[i];
    d[i] = 0;
}

void pb_say(Game *g, const char *big, const char *small, int32_t steps)
{
    Msg *m;
    if (g->nmsg >= PB_MAX_MSG) { memmove(&g->msg[0], &g->msg[1], sizeof(Msg) * (PB_MAX_MSG - 1)); g->nmsg--; }
    m = &g->msg[g->nmsg++];
    copy(m->big, big, sizeof m->big);
    copy(m->small, small, sizeof m->small);
    m->until = g->time + steps;
    m->at = g->time;
}

void pb_award(Game *g, int32_t pts, int16_t x, int16_t y)
{
    int32_t p = g->supernova > g->time ? pts * 3 : pts;
    g->score += p;
    if (x != -9999) gev(g, EV_SCORE, x, y, 0, p);
}
#define AWARD(g, p) pb_award(g, p, -9999, 0)

static void add_timer(Game *g, int32_t steps, int8_t kind, int8_t arg)
{
    if (g->ntimers >= PB_MAX_TIMER) return;
    g->timers[g->ntimers].at = g->time + steps;
    g->timers[g->ntimers].kind = kind;
    g->timers[g->ntimers].arg = arg;
    g->ntimers++;
}

static void reset_rules(Game *g)
{
    int8_t i;
    g->lanes[0] = g->lanes[1] = g->lanes[2] = 0;
    g->skill_lane = 1;
    g->bonus_x = 1;
    g->banks = 0; g->ramps = 0; g->locks = 0;
    g->missions_done = 0;
    g->mission = -1; g->mission_lit = 1;
    g->multiball = 0; g->jackpot = 0;
    g->supernova = 0; g->jackpot_lit = 0;
    g->bonus_ramps = g->bonus_targets = g->bonus_lanes = g->bonus_spins = 0;
    g->tilt_meter = 0; g->tilt_warnings = 0; g->tilted = 0;
    g->save_until = 0; g->skill_armed = 0; g->plunge_at = -1;
    g->pull = 0; g->pull_held = 0;
    g->ntimers = 0;
    for (i = 0; i < g->t->nseg; i++) if (g->t->seg[i].kind == SK_DROP) g->t->seg[i].up = 1;
}

void pb_game_init(Game *g, Table *t, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->t = t;
    pb_world_init(&g->w, t);
    g->state = GS_ATTRACT;
    g->balls_per_game = 3;
    g->rng = seed ? seed : 1;
    reset_rules(g);
}

/* a new ball (or a multiball / saved ball) in the plunger lane */
static Ball *serve(Game *g, int8_t autol)
{
    Ball *b;
    if (!autol) {
        g->ball_no++;
        g->tilt_meter = 0; g->tilt_warnings = 0; g->tilted = 0;
        g->w.flipper_power = 1; g->w.active_targets = 1;
        g->skill_lane = (g->skill_lane + 1) % 3;
        g->skill_armed = 1;
        g->bonus_ramps = g->bonus_targets = g->bonus_lanes = g->bonus_spins = 0;
        g->bonus_x = 1;
    }
    b = pb_add_ball(&g->w, g->t->plunger_x, g->t->plunger_rest, 0, 0, BS_LANE);
    if (b && autol) b->autol = PB_SECS(0.6);
    return b;
}

void pb_game_start(Game *g)
{
    char s[32];
    g->score = 0; g->ball_no = 0; g->state = GS_PLAY; g->time = 0;
    g->w.nballs = 0;
    g->nmsg = 0; g->nev = 0;
    reset_rules(g);
    serve(g, 0);
    copy(s, g->t->name, sizeof s);
    pb_say(g, s, "BALL 1", PB_SECS(2.5));
}

int8_t pb_live_balls(const Game *g)
{
    int8_t i, n = 0;
    for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].state != BS_DRAINED) n++;
    return n;
}

static void fire_plunger(Game *g, Ball *b, int16_t pull, int8_t autol)
{
    pb_plunge(&g->w, b, pull);
    if (!autol) {
        if (g->plunge_at < 0 || g->save_until < g->time) g->save_until = g->time + SAVE_TIME;
        g->plunge_at = g->time;
    }
}

static void lane_change(Game *g, int8_t dir)
{
    int8_t any = g->lanes[0] || g->lanes[1] || g->lanes[2], all = g->lanes[0] && g->lanes[1] && g->lanes[2];
    if (any && !all) {
        int8_t a = g->lanes[0], b = g->lanes[1], c = g->lanes[2];
        if (dir < 0) { g->lanes[0] = b; g->lanes[1] = c; g->lanes[2] = a; }
        else         { g->lanes[0] = c; g->lanes[1] = a; g->lanes[2] = b; }
    }
    if (g->skill_armed) g->skill_lane = (g->skill_lane + (dir < 0 ? 2 : 1)) % 3;
}

void pb_do_tilt(Game *g)
{
    int8_t i;
    g->tilted = 1;
    g->w.flipper_power = 0; g->w.active_targets = 0;
    for (i = 0; i < g->w.nflip; i++) g->w.flip[i].held = 0;
    g->save_until = 0;
    g->mission = -1;
    pb_say(g, "TILT", "NO BONUS", PB_SECS(99));
    gev(g, EV_TILT, 0, 0, 0, 0);
}

static void do_nudge(Game *g, int8_t nx, int8_t ny)
{
    /* port: 240 mm/s along (nx, ny) normalised, plus 40 mm/s up the table */
    int16_t k = nx && ny ? 170 : 240;
    if (!nx && !ny) return;
    pb_nudge(&g->w, V_MM(1) * nx * k, V_MM(1) * (ny * k + 40));
    gev(g, EV_NUDGE, nx, ny, 0, 0);
    g->tilt_meter += 1200;              /* port: the tilt bob in 1/1200 (1.0 a nudge) */
    if (g->tilt_meter > 2040) {
        g->tilt_meter = 360;
        g->tilt_warnings++;
        if (g->tilt_warnings >= 3) pb_do_tilt(g);
        else {
            pb_say(g, "DANGER", g->tilt_warnings == 1 ? "WARNING 1 OF 2" : "LAST WARNING", PB_SECS(1.6));
            gev(g, EV_TILTWARN, 0, 0, 0, g->tilt_warnings);
        }
    }
}

static void start_multiball(Game *g, int8_t extra)
{
    int8_t i;
    g->multiball = 1; g->jackpot = 0;
    for (i = 0; i < extra; i++) add_timer(g, PB_SECS(0.4) + i * PB_SECS(1.1), TM_SERVE, 0);
    g->save_until = g->time + PB_SECS(12);
    pb_say(g, "MULTIBALL", "RAMP FOR JACKPOT", PB_SECS(2.6));
    gev(g, EV_MULTIBALL, 0, 0, 0, 0);
}

void pb_mission_hit(Game *g, int8_t ev)
{
    const MissionDef *m;
    char s[16];
    if (g->mission < 0) return;
    m = &pb_missions[g->mission];
    if (m->ev != ev) return;
    if (++g->mission_n >= m->need) {
        AWARD(g, m->award);
        g->missions_done |= 1 << g->mission;
        pb_fmt(s, m->award);
        pb_say(g, "MISSION DONE", s, PB_SECS(2.4));
        gev(g, EV_MISSIONDONE, 0, 0, g->mission, 0);
        g->mission = -1;
        g->mission_lit = 1;
    }
}

static void sensor(Game *g, const Event *e)
{
    if (e->kind == SN_LANE) {
        int8_t i = e->id;
        if (e->up) return;              /* lanes count on the way down */
        if (g->skill_armed && g->time - g->plunge_at < PB_SECS(6)) {
            g->skill_armed = 0;
            if (i == g->skill_lane) { pb_award(g, 10000, e->x, e->y); pb_say(g, "SKILL SHOT", "10,000", PB_SECS(2)); gev(g, EV_SKILL, 0, 0, 0, 0); }
        } else g->skill_armed = 0;
        pb_award(g, 500, e->x, e->y);
        g->bonus_lanes++;
        if (!g->lanes[i]) {
            g->lanes[i] = 1;
            if (g->lanes[0] && g->lanes[1] && g->lanes[2]) {
                g->lanes[0] = g->lanes[1] = g->lanes[2] = 0;
                if (g->bonus_x < 5) g->bonus_x++;
                AWARD(g, 2000);
                g->mission_lit = g->mission_lit || g->mission < 0;
                {
                    char big[16] = "BONUS  X";
                    big[6] = (char)('0' + g->bonus_x);
                    pb_say(g, big, "LANES COMPLETE", PB_SECS(1.8));
                }
                gev(g, EV_LANESDONE, 0, 0, 0, 0);
                pb_mission_hit(g, MI_LANES);
            }
        }
        return;
    }
    if (e->kind == SN_IN) { if (!e->up) pb_award(g, 1000, e->x, e->y); return; }
    if (e->kind == SN_OUT) { if (!e->up) { pb_award(g, 2500, e->x, e->y); gev(g, EV_OUTLANE, e->x, e->y, 0, 0); } return; }
}

static void drop_hit(Game *g, const Event *e)
{
    Table *t = g->t;
    int8_t i, bank = t->seg[t->drop_seg[e->id]].bank, all = 1;
    pb_award(g, 750, e->x, e->y);
    g->bonus_targets++;
    for (i = 0; i < t->ndrop; i++) {
        const Seg *s = &t->seg[t->drop_seg[i]];
        if (s->bank == bank && s->up) all = 0;
    }
    if (all) {
        g->banks++;
        AWARD(g, 5000);
        pb_say(g, "BANK COMPLETE", "5,000", PB_SECS(1.5));
        gev(g, EV_BANK, 0, 0, bank, 0);
        pb_mission_hit(g, MI_BANK);
        add_timer(g, PB_SECS(1.2), TM_BANK_RESET, bank);
    }
}

static void ramp_made(Game *g, const Event *e)
{
    g->ramps++; g->bonus_ramps++;
    pb_award(g, 2500, e->x, e->y);
    pb_mission_hit(g, MI_RAMP);
    if (g->multiball) {
        int32_t pts = 50000 + 10000L * g->jackpot++, before = g->score;
        char s[16];
        AWARD(g, pts);
        pb_fmt(s, g->score - before);
        pb_say(g, "JACKPOT", s, PB_SECS(2.2));
        gev(g, EV_JACKPOT, 0, 0, 0, 0);
        return;
    }
    g->locks++;
    if (g->locks >= 3) { g->locks = 0; start_multiball(g, 2); }
    else {
        char big[16] = "BALL 0 LOCKED", small[32] = "0 MORE FOR MULTIBALL";
        big[5] = (char)('0' + g->locks);
        small[0] = (char)('0' + 3 - g->locks);
        pb_say(g, big, small, PB_SECS(1.8));
        gev(g, EV_LOCK, 0, 0, g->locks, 0);
    }
}

static Ball *find_ball(Game *g, int16_t id)
{
    int8_t i;
    for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].id == id) return &g->w.balls[i];
    return NULL;
}

static void saucer(Game *g, const Event *e)
{
    Ball *b = find_ball(g, e->ball);
    pb_award(g, 1000, e->x, e->y);
    if (g->mission < 0 && g->mission_lit) {
        int8_t left[5], nl = 0, i, done = 0, m;
        char s[32];
        for (i = 0; i < 5; i++) { if (g->missions_done & (1 << i)) done++; else left[nl++] = i; }
        if (!nl) {                      /* every mission done: the wizard mode */
            g->missions_done = 0;
            g->supernova = g->time + PB_SECS(30);
            pb_say(g, "SUPERNOVA", "EVERYTHING SCORES 3X", PB_SECS(3));
            gev(g, EV_SUPERNOVA, 0, 0, 0, 0);
            if (!g->multiball) start_multiball(g, 2);
            if (b) b->hold = PB_SECS(2.5);
            return;
        }
        m = left[(done + g->ball_no) % nl];
        g->mission = m; g->mission_n = 0;
        g->mission_until = g->time + MISSION_TIME + PB_SECS(2);
        g->mission_lit = 0;
        pb_fmt(s, pb_missions[m].need);
        { int8_t l = 0; while (s[l]) l++; s[l++] = ' '; copy(s + l, pb_missions[m].goal, (int8_t)sizeof s - l); }
        pb_say(g, g->t->mission[m], s, PB_SECS(2.4));
        gev(g, EV_MISSIONSTART, 0, 0, m, 0);
        if (b) b->hold = PB_SECS(2.2);
    }
}

static void drain(Game *g, const Event *e)
{
    int8_t left;
    char s[16];
    (void)e;
    pb_remove_drained(&g->w);
    left = pb_live_balls(g);
    if (!g->tilted && g->time < g->save_until) {
        pb_say(g, "BALL SAVED", "", PB_SECS(1.6));
        gev(g, EV_SAVE, 0, 0, 0, 0);
        serve(g, 1);
        return;
    }
    if (left) {
        if (g->multiball && left == 1) { g->multiball = 0; pb_say(g, "MULTIBALL OVER", "", PB_SECS(1.5)); }
        return;
    }
    if (g->ntimers && g->multiball) return;            /* a multiball ball is about to come out */
    g->multiball = 0;
    g->mission = -1; g->mission_lit = 1;
    /* end of ball: the bonus (none if tilted) */
    {
        int32_t base = (int32_t)g->bonus_ramps * 1000 + (int32_t)g->bonus_targets * 250 + (int32_t)g->bonus_lanes * 200 + (int32_t)g->bonus_spins * 20 + 1000;
        g->bonus_pts = g->tilted ? 0 : base * g->bonus_x;
    }
    g->score += g->bonus_pts;
    g->state = GS_BONUS; g->bonus_t = PB_SECS(2.2);
    pb_fmt(s, g->bonus_pts);
    if (g->tilted) pb_say(g, "TILT", s, PB_SECS(2.2));
    else { char big[16] = "BONUS  X"; big[6] = (char)('0' + g->bonus_x); pb_say(g, big, s, PB_SECS(2.2)); }
    gev(g, EV_ENDBALL, 0, 0, 0, 0);
}

static void after_bonus(Game *g)
{
    char s[16], big[16] = "BALL 0";
    g->w.flipper_power = 1; g->w.active_targets = 1;
    pb_fmt(s, g->score);
    if (g->ball_no >= g->balls_per_game + g->extra_balls) {
        g->state = GS_OVER;
        pb_say(g, "GAME OVER", s, PB_SECS(99));
        gev(g, EV_GAMEOVER, 0, 0, 0, 0);
        return;
    }
    g->state = GS_PLAY;
    serve(g, 0);
    big[5] = (char)('0' + g->ball_no);
    pb_say(g, big, s, PB_SECS(2));
}

void pb_game_handle(Game *g, const Event *e)
{
    if (g->nev < PB_MAX_EV) g->ev[g->nev++] = *e;
    if (g->tilted && e->type != EV_DRAIN && e->type != EV_LANEREST) return;
    switch (e->type) {
    case EV_BUMPER: pb_award(g, 100, e->x, e->y); pb_mission_hit(g, MI_BUMPER); break;
    case EV_SLING:  AWARD(g, 10); break;
    case EV_SPIN:   AWARD(g, 100); g->bonus_spins++; pb_mission_hit(g, MI_SPIN); break;
    case EV_DROP:   drop_hit(g, e); break;
    case EV_SENSOR: sensor(g, e); break;
    case EV_RAMP:   ramp_made(g, e); break;
    case EV_SAUCER: saucer(g, e); break;
    case EV_DRAIN:  drain(g, e); break;
    }
}

static void run_timers(Game *g)
{
    int8_t i, n = 0;
    Timer due[PB_MAX_TIMER];
    int8_t nd = 0;
    for (i = 0; i < g->ntimers; i++) {
        if (g->timers[i].at <= g->time) due[nd++] = g->timers[i];
        else g->timers[n++] = g->timers[i];
    }
    g->ntimers = n;
    for (i = 0; i < nd; i++) {
        if (due[i].kind == TM_BANK_RESET) {
            int8_t k;
            for (k = 0; k < g->t->ndrop; k++) {
                Seg *s = &g->t->seg[g->t->drop_seg[k]];
                if (s->bank == due[i].arg) s->up = 1;
            }
            gev(g, EV_BANKRESET, 0, 0, due[i].arg, 0);
        } else if (due[i].kind == TM_SERVE) serve(g, 1);
    }
}

void pb_game_tick(Game *g, const PbInput *in)
{
    World *w = &g->w;
    int8_t i, L, R, nev;
    Ball *in_lane = NULL;
    g->time++;
    if (g->state == GS_BONUS) {
        if (--g->bonus_t <= 0) after_bonus(g);
        run_timers(g);
        return;
    }
    /* the flippers (dead while tilted); pressing one also moves the lit lanes along */
    L = in->left && !g->tilted;
    R = in->right && !g->tilted;
    {
        int8_t held_l = 0, held_r = 0;
        for (i = 0; i < w->nflip; i++) {
            if (w->flip[i].d.side == 0) held_l |= w->flip[i].held; else held_r |= w->flip[i].held;
        }
        if (L && !held_l) lane_change(g, -1);
        if (R && !held_r) lane_change(g, 1);
        for (i = 0; i < w->nflip; i++) w->flip[i].held = w->flip[i].d.side == 0 ? L : R;
    }
    /* the plunger: hold to pull back (one second to full), release to fire */
    for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_LANE && w->balls[i].autol < 0) { in_lane = &w->balls[i]; break; }
    if (!in_lane) for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_LANE) { in_lane = &w->balls[i]; break; }
    /* port: the pull in 1/1000 (one second to full: 8 a step) */
    if (in->pull_to >= 0) { g->pull = in->pull_to < 0 ? 0 : in->pull_to > 1000 ? 1000 : in->pull_to; g->pull_held = 1; }
    else if (in->plunger) { g->pull += 8; if (g->pull > 1000) g->pull = 1000; g->pull_held = 1; }
    else if (g->pull_held) {
        /* (the browser won't fire while tilted, so a ball in the lane then
         * keeps the ball alive for ever; here it fires, and drains) */
        g->pull_held = 0;
        if (in_lane) fire_plunger(g, in_lane, g->pull, 0);
        g->pull = 0;
    }
    /* automatic launches, for multiball and saved balls */
    for (i = 0; i < w->nballs; i++) {
        Ball *b = &w->balls[i];
        if (b->state == BS_LANE && b->autol >= 0 && --b->autol < 0)
            fire_plunger(g, b, 820 + (int16_t)(a3_rand(&g->rng) % 100), 1);
    }
    /* nudging and the tilt bob */
    if ((in->nudge_x || in->nudge_y) && g->state == GS_PLAY && !g->tilted) do_nudge(g, in->nudge_x, in->nudge_y);
    g->tilt_meter -= 9;                 /* 0.9 a second */
    if (g->tilt_meter < 0) g->tilt_meter = 0;
    pb_world_step(w);
    /* the saucer holds the ball, then kicks it out */
    /* (with a little variation the browser doesn't have: a fixed kick can set up an endless loop) */
    for (i = 0; i < w->nballs; i++)
        if (w->balls[i].state == BS_SAUCER && w->balls[i].hold <= 0)
            pb_kick_saucer(w, &w->balls[i], V_MM((int16_t)(a3_rand(&g->rng) % 161) - 80));
    nev = w->nev; w->nev = 0;
    for (i = 0; i < nev; i++) {
        Event e = w->ev[i];
        pb_game_handle(g, &e);
    }
    /* missions time out */
    if (g->mission >= 0 && g->time > g->mission_until) {
        pb_say(g, "MISSION OVER", g->t->mission[g->mission], PB_SECS(2));
        gev(g, EV_MISSIONFAIL, 0, 0, g->mission, 0);
        g->mission = -1; g->mission_lit = 1;
    }
    run_timers(g);
}

/* What the dot-matrix display shows now. */
void pb_dmd(Game *g, Dmd *d)
{
    static char big[24], small[40];
    Msg *m = NULL;
    int8_t i;
    while (g->nmsg > 1 && g->msg[0].until < g->time) { memmove(&g->msg[0], &g->msg[1], sizeof(Msg) * (g->nmsg - 1)); g->nmsg--; }
    for (i = 0; i < g->nmsg; i++) if (g->msg[i].until >= g->time) m = &g->msg[i];
    d->flash = 0;
    if (g->state == GS_ATTRACT) { d->big = g->t->name; d->small = "PRESS FIRE"; return; }
    if (m) { d->big = m->big; d->small = m->small; d->flash = g->time - m->at < PB_SECS(0.6); return; }
    pb_fmt(big, g->score);
    d->big = big; d->small = small;
    {
        char *p = small;
        const char *s;
        if (g->mission >= 0) {
            int32_t left = (g->mission_until - g->time + PB_HZ - 1) / PB_HZ;
            char num[12];
            for (s = g->t->mission[g->mission]; *s && *s != ' '; s++) *p++ = *s;
            *p++ = ' ';
            pb_fmt(num, g->mission_n); for (s = num; *s; s++) *p++ = *s;
            *p++ = '/';
            pb_fmt(num, pb_missions[g->mission].need); for (s = num; *s; s++) *p++ = *s;
            *p++ = ' ';
            pb_fmt(num, left < 0 ? 0 : left); for (s = num; *s; s++) *p++ = *s;
            *p++ = 'S'; *p = 0;
        } else if (g->multiball) {
            char num[12];
            for (s = "MULTIBALL  JACKPOT "; *s; s++) *p++ = *s;
            pb_fmt(num, 50 + 10 * g->jackpot); for (s = num; *s; s++) *p++ = *s;
            *p++ = 'K'; *p = 0;
        } else {
            int8_t lane = 0;
            for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].state == BS_LANE) lane = 1;
            for (s = "BALL "; *s; s++) *p++ = *s;
            *p++ = (char)('0' + g->ball_no % 10);
            if (lane) { for (s = "  SKILL SHOT "; *s; s++) *p++ = *s; *p++ = (char)('A' + g->skill_lane); }
            else if (g->bonus_x > 1) { for (s = "  BONUS "; *s; s++) *p++ = *s; *p++ = (char)('0' + g->bonus_x); *p++ = 'X'; }
            *p = 0;
        }
    }
}

/* Which lamps are lit (the browser page's rules); blink toggles at 2.5 Hz,
 * chase counts up in the attract mode. */
void pb_lamps(const Game *g, int8_t blink, int8_t chase, uint8_t on[L_COUNT])
{
    int8_t i, lane = 0, skill;
    memset(on, 0, L_COUNT);
    if (g->state == GS_ATTRACT || g->state == GS_OVER) {
        for (i = 0; i < L_COUNT; i++) on[i] = (uint8_t)((i + chase) % 5 == 0);
        return;
    }
    for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].state == BS_LANE) lane = 1;
    skill = g->skill_armed && (g->time - g->plunge_at < PB_SECS(6) || lane) ? g->skill_lane : -1;
    for (i = 0; i < 3; i++) on[L_LANE0 + i] = (uint8_t)(g->lanes[i] || (skill == i && blink));
    for (i = 0; i < 3; i++) on[L_LOCK0 + i] = (uint8_t)(g->multiball ? blink : (i < g->locks || (i == g->locks && blink && g->state == GS_PLAY)));
    on[L_JACKPOT] = (uint8_t)(g->multiball && blink);
    for (i = 0; i < 5; i++) on[L_M0 + i] = (uint8_t)((g->missions_done & (1 << i)) || (g->mission == i && blink));
    on[L_MISSION] = (uint8_t)(g->mission_lit && g->mission < 0 && blink);
    for (i = 0; i < 4; i++) on[L_X2 + i] = (uint8_t)(g->bonus_x >= i + 2);
    on[L_SAVE] = (uint8_t)(g->time < g->save_until);
    for (i = 0; i < g->t->nspin && i < 2; i++) on[L_SPIN0 + i] = (uint8_t)(g->w.spin[i].w > 2 * 256);
    for (i = 0; i < g->t->ndrop && i < 8; i++) on[L_DROP0 + i] = g->t->seg[g->t->drop_seg[i]].up;
    on[L_SUPER] = (uint8_t)(g->supernova > g->time && blink);
}

/* The attract mode's player (and the tests'): launches, holds a flipper up
 * while a ball comes down towards it and lets go a moment after. */
void pb_bot(const Game *g, PbInput *in, Bot *bot)
{
    int8_t i, k;
    int16_t *hold = bot->hold;
    uint32_t *rng = &bot->rng;
    memset(in, 0, sizeof *in);
    in->pull_to = -1;
    for (k = 0; k < 2; k++) if (hold[k] > 0) { hold[k]--; if (k) in->right = 1; else in->left = 1; }
    for (i = 0; i < g->w.nballs; i++) {
        const Ball *b = &g->w.balls[i];
        if (b->state == BS_LANE) {
            if (++bot->launch > PB_SECS(0.8)) { in->pull_to = 800 + (int16_t)(a3_rand(rng) % 200); bot->launch = 0; }
            continue;
        }
        if (b->state != BS_PLAY) continue;
        for (k = 0; k < g->w.nflip; k++) {
            const Flipper *f = &g->w.flip[k];
            int16_t tx, ty, dx, dy;
            pb_flipper_tip(f, &tx, &ty);
            dx = b->x - (f->d.px + tx) / 2;
            dy = b->y - (f->d.py + ty) / 2;
            /* coming down within reach: flip, with a little randomness in the timing
               (port: in P and V; |vy| / 20 mm is |vy|_V / 5 P) */
            if (b->vy < V_MM(-60) && ABS16(dx) < f->d.len * 3 / 4 + PB_R && dy > 0 && dy < P_MM(40) + ABS16(b->vy) / 5) {
                if (a3_rand(rng) % 4) hold[f->d.side] = PB_SECS(0.25);
            }
        }
    }
    for (k = 0; k < 2; k++) if (hold[k] > 0) { if (k) in->right = 1; else in->left = 1; }
}
