/*
 * The game around the marble (GameDirector.cs, Player.cs, IsoCamera.cs,
 * Progress.cs, Ghost.cs, DeathFx.cs): one clock that carries over between
 * the six courses, falls that cost three seconds and put you back behind
 * where you last had contact, medals, best times, a ghost of your best run,
 * the title flyover, wipeouts in slow motion - and one or two players.
 *
 * Everything here steps at 50 Hz. Gameplay runs through a time-scale
 * accumulator so a wipeout can slow it down (single player, as in Unity).
 */
#include <string.h>
#include "game.h"
#include "zxlog.h"

/* port: the original's C for zsdcc. Its changes are marked port:; the
   usual ones are V3CPY (zsdcc miscompiles small structure copies),
   functions that returned a V3 now fill one (zsdcc can't return one), and
   results of calls put in a local before a 32-bit multiply (another zsdcc
   fault). */
#define rs_log zx_log
#define V3CPY(d, s) ((d).x = (s).x, (d).y = (s).y, (d).z = (s).z)

Game G;
extern uint8_t autopilot;                  /* port: main.c */

#define SECS(s)        ((long)((s) * 50.0 + 0.5))
#define DEG(d)         ((long)((d) * 65536.0 / 360.0))
#define DEATH_PENALTY  SECS(3)
#define DYING_HOLD     SECS(1.35)
#define CLEAR_HOLD     SECS(3.4)
#define DEFAULT_SIZE   FXF(10.5)           /* the Unity view is 13 units; 320x240 needs a closer look */

/* port: best times last the session (no NVRAM; the high scores are the SDK's) */
static void rs_save_progress(void) {}
static void rs_load_progress(void) {}

/* ---- small things ---- */

static unsigned long rnd_state = 12345;
static long rnd(long lo, long hi)          /* lo..hi-1 */
{
    rnd_state = rnd_state * 1103515245UL + 12345UL;
    return lo + (long)((rnd_state >> 8) % (unsigned long)(hi - lo));
}

static long lerp_angle(long now, long want, long k)    /* 65536 per turn, k Q12 */
{
    long d = (want - now) & 0xFFFF;
    if (d > 32768) d -= 65536;
    return now + ((d * k) >> 12);
}

int medal_for(long t)
{
    long ms = t * 20;
    if (t <= 0) return MEDAL_NONE;
    if (C.gold_ms > 0 && ms <= C.gold_ms) return MEDAL_GOLD;
    if (C.silver_ms > 0 && ms <= C.silver_ms) return MEDAL_SILVER;
    if (C.bronze_ms > 0 && ms <= C.bronze_ms) return MEDAL_BRONZE;
    return MEDAL_NONE;
}

/* ---- wipeout debris (DeathFx.Burst): shards and sparks ---- */
Bit bits[MAXBITS];                         /* port: the view draws them (game.h) */
static uint8_t bits_live;                  /* port: bits alive (burst sets it), so an empty list costs nothing */

static void burst(V3 at, int look)
{
    int i, n = 0;
    for (i = 0; i < MAXBITS && n < 24; i++) {
        Bit *b = &bits[i];
        int spark = n >= 16;
        fix force = spark ? 18 * FX : 12 * FX, sp;
        long a = rnd(0, 65536), e = rnd(-16384, 16384), ca, sa, ce, se, r;
        if (b->life) continue;
        sp = rnd(force * 45 / 100, force);
        V3CPY(b->p, at);
        r = rnd(-1228, 1228); b->p.x += r;          /* port: calls before the arithmetic */
        r = rnd(-1228, 1228); b->p.y += r;
        r = rnd(-1228, 1228); b->p.z += r;
        ca = icos(a); sa = isin(a); ce = icos(e); se = isin(e);
        b->v.x = ((ca >> 2) * ((ce * (sp >> 4)) >> 14)) >> 8;
        b->v.z = ((sa >> 2) * ((ce * (sp >> 4)) >> 14)) >> 8;
        r = rnd(force * 3 / 10, force * 8 / 10);
        b->v.y = ((se * sp) >> 14) + r;
        b->size = spark ? rnd(328, 655) / 2 : rnd(819, 1802) / 2;
        b->look = spark ? SP_SPARK : look;
        b->life = spark ? rnd(25, 45) : rnd(60, 100);
        b->age = 0;
        bits_live = 1;
        n++;
    }
}

static void bits_step(void)
{
    int i;
    if (!bits_live) return;
    bits_live = 0;
    for (i = 0; i < MAXBITS; i++) {
        Bit *b = &bits[i];
        if (!b->life) continue;
        b->v.y -= 804;                                   /* 9.81 m/s^2 */
        b->p.x += (b->v.x * 1311L) >> 16;
        b->p.y += (b->v.y * 1311L) >> 16;
        b->p.z += (b->v.z * 1311L) >> 16;
        if (++b->age >= b->life) b->life = 0;
        else bits_live = 1;
    }
}

/* port: no ghost of your best run (Ghost.cs): it kept 65 KB of samples and
   drew a see-through marble */

/* ---- loading a course ---- */

static void cam_snap(Player *p);

static void load_level(int index, int reset_clock)
{
    int i;
    V3 across;
    if (!course_load(index + 1)) return;
    G.level = index;
    G.loaded = 1;
    phys_reset();
    G.timescale = FX;
    G.time_acc = 0;
    across.x = 2896; across.y = 0; across.z = -2896;       /* the course's +x, yawed 45 */
    phys_nmarbles = G.nplayers;
    for (i = 0; i < G.nplayers; i++) {
        Player *p = &G.p[i];
        Ball *m = &p->m;
        p->course_time = 0;
        p->finished = p->out_of_time = p->dying = p->fall_whistle = 0;
        p->dying_timer = 0;
        p->demo_wp = 0;
        if (reset_clock) p->time_left = 0;
        p->time_left += C.time_ms / 20;
        memset(m, 0, sizeof(*m));
        V3CPY(m->p, C.spawn);
        if (G.nplayers > 1) {
            long s = i ? 5325 : -5325;                     /* 1.3 units either side */
            m->p.x += (across.x * s) >> 12;
            m->p.z += (across.z * s) >> 12;
        }
        m->r = FX / 2; m->inv_mass = FX; m->fric = FX / 2; m->bounce = 614; m->gravity = 1787;
        m->rot_k = 5 * FX; m->live = 1; V3CPY(m->last_ground, m->p);
        phys_marbles[i] = m;
        p->hold = 0;
        cam_snap(p);
    }
    rs_log("level %d %s clock=%ld\n", index + 1, C.name, (long)(G.p[0].time_left / 5));
    snd_music(G.music_on ? (int)C.music : -1);
}

static void enter(int s)
{
    if (s != ST_PLAYING) G.timescale = FX;
    G.state = s;
    G.state_timer = 0;
    rs_log("state=%d level=%d falls=%d clock=%ld\n", s, G.level + 1, G.p[0].deaths, (long)(G.p[0].time_left / 5));
}

static void freeze_all(void)
{
    int i;
    for (i = 0; i < G.nplayers; i++) G.p[i].m.frozen = 1;
}

static void start_run(int from)
{
    int i;
    G.nplayers = G.want_players;
    for (i = 0; i < G.nplayers; i++) {
        G.p[i].deaths = 0;
        G.p[i].run_time = 0;
        G.p[i].wins = 0;
        G.p[i].death_reason = "";
    }
    G.started_at = from;
    G.idle = 0;
    G.last_winner = -1;
    load_level(from, 1);
    enter(ST_PLAYING);
    snd_play(SFX_START);
}

static void to_title(void)
{
    /* after an attract demo, the next one shows the next course */
    if (G.demo) G.title_select = (G.level + 1) % course_count;
    G.demo = 0;
    G.nplayers = 1;
    enter(ST_TITLE);
    G.loaded = 0;
    snd_music(G.music_on ? 0 : -1);
}

/* ---- the camera (IsoCamera.cs) ---- */

static void desired(const Player *p, V3 *f)   /* port: fills f */
{
    V3CPY(*f, p->m.p);
    /* look ahead along the flat velocity, 0.35 s */
    f->x += (p->m.v.x * 1434) >> 12;
    f->z += (p->m.v.z * 1434) >> 12;
}

static void cam_snap(Player *p)
{
    p->cam.yaw = p->yaw;
    p->cam.pitch = p->pitch;
    p->cam.size = p->size;
    desired(p, &p->cam.focus);
    p->cam_vel.x = p->cam_vel.y = p->cam_vel.z = 0;
}

static void cam_input(Player *p, unsigned long held, unsigned long pressed)
{
    /* a tap turns one 45-degree facet; holding spins freely and settles on 15 degrees */
    int spinning = 0;
    if (pressed & B_R) { p->yaw += DEG(45); p->r_hold = 0; }
    if (pressed & B_L) { p->yaw -= DEG(45); p->l_hold = 0; }
    if (held & B_R) { if (++p->r_hold > 17) { p->yaw += DEG(2); spinning = 1; } } else p->r_hold = 0;
    if (held & B_L) { if (++p->l_hold > 17) { p->yaw -= DEG(2); spinning = 1; } } else p->l_hold = 0;
    if (p->spinning && !spinning) {
        long step = DEG(15);
        p->yaw = ((p->yaw + step / 2) / step) * step;
    }
    p->spinning = spinning;
    /* C with up/down tilts; A zooms in, B out */
    if (held & B_C) {
        if (held & B_UP) p->pitch += DEG(0.9);
        if (held & B_DOWN) p->pitch -= DEG(0.9);
        if (p->pitch < DEG(15)) p->pitch = DEG(15);
        if (p->pitch > DEG(80)) p->pitch = DEG(80);
    }
    if (held & B_A) p->size -= 1147;                      /* 14 units/s */
    if (held & B_B) p->size += 1147;
    if (p->size < 7 * FX) p->size = 7 * FX;
    if (p->size > 28 * FX) p->size = 28 * FX;
}

static void cam_follow(Player *p)
{
    const long k = 874;                                   /* 1 - exp(-12 dt) */
    p->cam.yaw = lerp_angle(p->cam.yaw, p->yaw, k);
    p->cam.pitch += ((p->pitch - p->cam.pitch) * k) >> 12;
    p->cam.size += ((p->size - p->cam.size) * k) >> 12;
    if (!p->hold) {
        V3 d;
        desired(p, &d);
        p->cam.focus.x += ((d.x - p->cam.focus.x) * 500) >> 12;
        p->cam.focus.y += ((d.y - p->cam.focus.y) * 500) >> 12;
        p->cam.focus.z += ((d.z - p->cam.focus.z) * 500) >> 12;
    }
}

static long cine_yaw;

static void cam_orbit(Player *p, const V3 *fp, long spin_deg, long pitch_deg, fix size)   /* port: focus by pointer */
{
    V3 focus;
    V3CPY(focus, *fp);
    cine_yaw += spin_deg * 65536L / 360 / 50;
    p->cam.yaw = cine_yaw;
    p->cam.pitch += ((DEG(1) * pitch_deg - p->cam.pitch) * 278) >> 12;
    p->cam.size += ((size - p->cam.size) * 278) >> 12;
    p->cam.focus.x += ((focus.x - p->cam.focus.x) * 238) >> 12;
    p->cam.focus.y += ((focus.y - p->cam.focus.y) * 238) >> 12;
    p->cam.focus.z += ((focus.z - p->cam.focus.z) * 238) >> 12;
}

/* ---- the autopilot (GameDirector.DemoInput) ---- */
/* port: path_at, course_progress, demo_input, demo_resync and respawn_point
   are in game_path.c (another bank: game.c's code fills one); main.c's
   stubs map it */
void path_at(long t, V3 *ap);
void demo_input(Player *p, fix *wx, fix *wz);
void demo_resync(Player *p);
void respawn_point(const Player *p, V3 *rp);


/* ---- dying, respawning, finishing ---- */

static void kill(Player *p, const char *reason)
{
    if (G.state != ST_PLAYING || p->dying || p->finished || p->out_of_time) return;
    rs_log("death %s p%d course=%d at %ld %ld %ld\n", reason, (int)(p - G.p) + 1, G.level + 1,
           (long)(p->m.p.x >> 12), (long)(p->m.p.y >> 12), (long)(p->m.p.z >> 12));
    p->death_reason = reason;
    p->deaths++;
    p->time_left -= DEATH_PENALTY;
    if (p->time_left < 0) p->time_left = 0;
    p->m.frozen = 1;
    p->m.live = 0;
    p->hold = 1;
    p->shake = FXF(1.4);
    burst(p->m.p, p == G.p ? SP_MARBLE : SP_MARBLE2);
    snd_play(SFX_SHATTER);
    p->flash = 58;
    if (!strcmp(reason, "DISSOLVED")) { snd_play(SFX_SIZZLE); p->flash_r = 11; p->flash_g = 31; p->flash_b = 12; }
    else if (!strcmp(reason, "EATEN")) { snd_play(SFX_CHOMP); p->flash_r = 15; p->flash_g = 31; p->flash_b = 17; }
    else if (!strcmp(reason, "CRUSHED")) { snd_play(SFX_THUD); p->flash_r = 31; p->flash_g = 17; p->flash_b = 9; }
    else { if (!p->fall_whistle) snd_play(SFX_FALL); p->flash_r = 31; p->flash_g = 14; p->flash_b = 11; }
    if (G.nplayers == 1) G.timescale = 1229;               /* 0.3: the slow-motion beat */
    p->dying = 1;
    p->dying_timer = 0;
}

static void respawn(Player *p)
{
    G.timescale = FX;
    p->dying = 0;
    p->fall_whistle = 0;
    if (p->time_left <= 0) {
        p->out_of_time = 1;
        p->m.frozen = 1;
        return;
    }
    respawn_point(p, &p->m.p);
    p->m.v.x = p->m.v.y = p->m.v.z = 0;
    V3CPY(p->m.w, p->m.v);
    V3CPY(p->m.last_ground, p->m.p);
    p->m.frozen = 0;
    p->m.live = 1;
    p->m.touching_enemy = p->m.touch_sweeper = 0;
    phys_reset_enemies();
    p->hold = 0;
    cam_snap(p);
    demo_resync(p);
}

static void reach_goal(Player *p)
{
    if (G.state != ST_PLAYING || p->finished || p->dying) return;
    p->finished = 1;
    p->finish_time = p->course_time;
    p->m.frozen = 1;
    G.last_course_time = p->course_time;
    G.last_medal = medal_for(p->course_time);
    G.last_was_best = 0;
    if (G.nplayers == 1 && !G.demo) {
        long *b = &G.best[G.level];
        if (!*b || p->course_time < *b) {
            *b = p->course_time;
            G.last_was_best = 1;
            rs_save_progress();
        }
    } else if (G.nplayers > 1 && G.last_winner < 0) {
        G.last_winner = (int)(p - G.p);
        p->wins++;
    }
    rs_log("goal p%d course=%d time=%ld medal=%d best=%d\n", (int)(p - G.p) + 1, G.level + 1,
           (long)(p->course_time * 2), G.last_medal, G.last_was_best);
    snd_play(SFX_GOAL);
    freeze_all();
    enter(ST_CLEAR);
}

static void advance(void)
{
    if (G.demo) { to_title(); return; }
    if (G.level + 1 >= course_count) {
        if (G.started_at == 0 && G.nplayers == 1 && !G.demo &&
            (!G.best_run || G.p[0].run_time < G.best_run)) {
            G.best_run = G.p[0].run_time;
            G.best_run_falls = G.p[0].deaths;
            G.last_was_best = 1;
            rs_save_progress();
        }
        enter(ST_WON);
        freeze_all();
        return;
    }
    load_level(G.level + 1, 0);
    G.last_winner = -1;
    enter(ST_PLAYING);
    snd_play(SFX_START);
}

/* ---- one step of the race (scaled by the slow-motion) ---- */

/* port: play mode (phys_dt2, the default) runs the physics on every second
   race step, one step of 2/50 s (phys.asm doubles every per-step amount);
   the clocks, timers and camera here keep their 50 steps a second. Exact
   mode (phys_dt2 = 0) is the original's 50 Hz physics */
static unsigned char dt_phase;

static void world_step(void)
{
    if (!phys_dt2 || (G.clock & 1)) phys_world_step();
}

static void race_step(unsigned long held[2])
{
    int i, all_out = 1, do_phys = 1;
    if (phys_dt2) { dt_phase ^= 1; do_phys = dt_phase; }
    if (do_phys) phys_world_step();
    bits_step();
    for (i = 0; i < G.nplayers; i++) {
        Player *p = &G.p[i];
        Ball *m = &p->m;
        fix wx = 0, wz = 0;
        if (p->finished) { all_out = 0; continue; }
        if (p->dying) { all_out = 0; continue; }
        if (p->out_of_time) continue;
        all_out = 0;
        if (G.demo || autopilot) demo_input(p, &wx, &wz);   /* port: or B's autopilot (main.c) */
        else {
            /* the pad, relative to the camera: up is away from you */
            long h = 0, v = 0, sy, cy;
            sy = isin(p->cam.yaw) >> 2;                  /* port: the calls first */
            cy = icos(p->cam.yaw) >> 2;
            unsigned long k = held[i];
            if (!(k & B_C)) {
                if (k & B_RIGHT) h += FX;
                if (k & B_LEFT) h -= FX;
                if (k & B_UP) v += FX;
                if (k & B_DOWN) v -= FX;
            }
            wx = (h * cy + v * sy) >> 12;
            wz = (-h * sy + v * cy) >> 12;
            if (h && v) { wx = (wx * 2896) >> 12; wz = (wz * 2896) >> 12; }
        }
        m->kill = 0;
        m->goal = 0;
        if (do_phys) phys_marble_step(m, wx, wz);
        p->time_left--;
        p->course_time++;
        p->run_time++;
        if (p->time_left <= SECS(10) && p->time_left > 0 && G.clock - p->last_warn > 50) {
            p->last_warn = G.clock;
            snd_play(SFX_WARN);
        }
        if (p->time_left <= 0) {
            p->time_left = 0;
            p->out_of_time = 1;
            m->frozen = 1;
            snd_play(SFX_DEATH);
            continue;
        }
        if (m->kill) { kill(p, m->kill); continue; }
        if (m->goal) { reach_goal(p); continue; }
        {
            fix dropped = m->last_ground.y - m->p.y;
            int airborne = !m->grounded;
            if (airborne && dropped > FXF(4.5) && !p->fall_whistle) { p->fall_whistle = 1; snd_play(SFX_FALL); }
            if (!airborne || dropped < FX) p->fall_whistle = 0;
            if ((airborne && dropped > 12 * FX) || m->p.y < C.kill_y) kill(p, "FELL OFF");
        }
    }
    if ((G.p[0].course_time % 250) == 0 && G.p[0].course_time) {
        long pr = course_progress(&G.p[0]);          /* port: the call first */
        rs_log("run course=%d t=%ld progress=%ld falls=%d\n", G.level + 1, (long)(G.p[0].course_time / 50),
               (long)((pr * 100) >> 16), G.p[0].deaths);
    }
    if (all_out && G.state == ST_PLAYING) {
        enter(ST_GAMEOVER);
        freeze_all();
        snd_play(SFX_DEATH);
    }
}

/* ---- the 50 Hz step ---- */

void game_init(void)
{
    int i;
    memset(&G, 0, sizeof(G));
    G.want_players = G.nplayers = 1;
    G.music_on = 1;
    G.last_winner = -1;
    for (i = 0; i < 2; i++) {
        G.p[i].yaw = 0;
        G.p[i].pitch = DEG(35);
        G.p[i].size = DEFAULT_SIZE;
    }
    rs_load_progress();
    to_title();
}

void game_step(unsigned long held0, unsigned long pressed0, unsigned long held1, unsigned long pressed1)
{
    unsigned long held[2], pressed[2];
    int i;
    held[0] = held0; held[1] = held1; pressed[0] = pressed0; pressed[1] = pressed1;
    G.clock++;
    G.state_timer++;
    for (i = 0; i < 2; i++) {
        if (G.p[i].flash > 0) G.p[i].flash -= 4;
        if (G.p[i].shake > 0) G.p[i].shake -= 287;
        if (G.p[i].shake < 0) G.p[i].shake = 0;
    }
    if (G.demo && (pressed0 | pressed1)) { to_title(); return; }
    switch (G.state) {
    case ST_TITLE:
        if (!G.loaded || G.level != G.title_select) { load_level(G.title_select, 1); freeze_all(); }
        if (pressed0) G.idle = 0; else G.idle++;
        if (pressed0 & B_DOWN) { if (G.title_select < course_count - 1) G.title_select++; }
        if (pressed0 & B_UP) { if (G.title_select > 0) G.title_select--; }
        /* port: one player (no split screen), so left/right does nothing */
        if (pressed0 & B_C) { G.music_on = !G.music_on; snd_music(G.music_on ? 0 : -1); }
        if (pressed0 & (B_A | B_P)) { start_run(G.title_select); return; }
        if (G.idle > SECS(25)) {
            /* attract mode: the autopilot plays the first course */
            G.want_players = 1;
            start_run(G.title_select);
            G.demo = 1;
            rs_log("demo starts course=%d\n", G.title_select + 1);
            return;
        }
        world_step();                                /* port: see race_step */
        G.cine_t = (G.cine_t + 59) & 0xFFFF;               /* 0.045 of the course a second */
        {
            V3 f;
            path_at(G.cine_t, &f);
            cam_orbit(&G.p[0], &f, 9, 27, 20 * FX);
        }
        break;
    case ST_PLAYING:
        if (pressed0 & B_P) G.paused = !G.paused;
        if (G.paused) break;
        for (i = 0; i < G.nplayers; i++) {
            Player *p = &G.p[i];
            cam_input(p, held[i], pressed[i]);
            if (p->dying) {
                p->dying_timer++;
                if (G.nplayers == 1) {
                    G.timescale = 1229 + ((FX - 1229) * p->dying_timer) / DYING_HOLD;
                    if (G.timescale > FX) G.timescale = FX;
                }
                if (p->dying_timer >= DYING_HOLD) respawn(p);
            }
        }
        G.time_acc += G.timescale;
        while (G.time_acc >= FX && G.state == ST_PLAYING) {
            G.time_acc -= FX;
            race_step(held);
        }
        for (i = 0; i < G.nplayers; i++) cam_follow(&G.p[i]);
        break;
    case ST_CLEAR:
        world_step();
        bits_step();
        for (i = 0; i < G.nplayers; i++) {
            V3 f;
            V3CPY(f, C.goal_world);
            f.y += 7 * FX;
            if (i == 0) cam_orbit(&G.p[i], &f, 34, 30, 16 * FX);
            /* port: one player, no second camera */
        }
        if (G.state_timer >= CLEAR_HOLD) advance();
        break;
    case ST_GAMEOVER:
    case ST_WON:
        world_step();
        bits_step();
        if (G.state == ST_WON) {
            V3 f;
            V3CPY(f, C.goal_world);
            f.y += 7 * FX;
            cam_orbit(&G.p[0], &f, 34, 30, 16 * FX);
        }
        if (G.state_timer > SECS(1) && (pressed0 & (B_A | B_P))) to_title();
        else if (G.demo && G.state_timer > SECS(4)) to_title();
        break;
    }
    /* the world plays its sounds */
    if (phys_sound >= 0) { snd_play(phys_sound); phys_sound = -1; }
    {
        Player *p = &G.p[0];
        fix sp = 0;
        if (G.state == ST_PLAYING && !p->dying && !p->finished && p->m.grounded) {
            V3 f;
            f.x = p->m.v.x; f.y = 0; f.z = p->m.v.z;
            sp = vlenp(&f) * FX / (13 * FX);
            if (sp > FX) sp = FX;
        }
        snd_roll(sp);
    }
}

/* port: the frame is drawn by view.c from G, Wd and bits (the original's
   game_draw built the 3DO's cel list) */
