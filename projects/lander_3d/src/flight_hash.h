/*
 * flight_hash.h - a flight from a level's start, its state hashed every
 * step (FNV-1a over the craft, the fuel, the outcome and the result): the
 * original's code (gen_levels.c), the port on the host (host_check.c) and
 * the port on the Next (game.c's self-test) must give the same hashes.
 *
 *   kind 0: the original's autopilot flies it (until it lands, or `steps`)
 *   kind 1: a pilot's inputs made from the step count (throttle, tips
 *           both ways), until an outcome or `steps`: the pilot's paths
 *           through the rules, which the autopilot doesn't take
 */
#ifndef FLIGHT_HASH_H
#define FLIGHT_HASH_H

#define FLIGHT_STEPS 12000

static uint32_t fh_h;
static void fh_mix(int32_t v)
{
    uint8_t k;
    for (k = 0; k < 4; k++) {
        fh_h ^= (uint8_t)(v >> (k * 8));
        fh_h *= 16777619UL;
    }
}

static void fh_pilot(int32_t t, LdInput *in)
{
    int32_t ph = t % 700;
    in->throttle = ph < 300 ? FX(0.42) : ph < 420 ? FONE : FX(0.2);
    in->pitch = (t / 150) % 3 == 0 ? FONE : (t / 150) % 3 == 1 ? -FONE : 0;
    in->roll = (t / 230) % 4 == 1 ? FONE : (t / 230) % 4 == 3 ? -FX(0.5) : 0;
}

static uint32_t flight_hash_kind(Level *L, int kind, int32_t max_steps, int32_t *steps)
{
    LdInput in;
    int32_t t;
    uint8_t d;
    fh_h = 2166136261UL;
    for (t = 0; t < max_steps && !L->outcome; t++) {
        if (kind == 0) ld_autopilot(L, &in);
        else fh_pilot(t, &in);
        ld_step(L, &in);
        for (d = 0; d < 3; d++) { fh_mix(L->c.p[d]); fh_mix(L->c.v[d]); }
        fh_mix(L->c.pitch); fh_mix(L->c.roll); fh_mix(L->c.wp); fh_mix(L->c.wr);
        fh_mix(L->fuel); fh_mix(L->outcome); fh_mix(L->ap_down); fh_mix(L->ap_target);
    }
    fh_mix(L->result.score); fh_mix(L->result.soft); fh_mix(L->result.fuel_bonus); fh_mix(L->result.pad);
    fh_mix(L->result.vs); fh_mix(L->result.hs); fh_mix(L->result.tilt); fh_mix(L->result.slope);
    *steps = t;
    return fh_h;
}

#endif
