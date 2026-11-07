/* the snapshot of one side (included by port_side.c and orig_side.c with
   their own GameState and room accessor) */
static GameState gs;

static void fill_snap(Snap *s)
{
    int i, x, y;
    memset(s, 0, sizeof(*s));
    s->hx = gs.hunter.x; s->hy = gs.hunter.y; s->hdx = gs.hunter.dx; s->hdy = gs.hunter.dy;
    s->health = gs.hunter.health; s->max_energy = gs.hunter.max_energy; s->missiles = gs.hunter.missiles;
    s->score = gs.score; s->room_x = gs.room_x; s->room_y = gs.room_y; s->items = gs.items_collected;
    s->on_ground = gs.hunter.on_ground; s->facing = gs.hunter.facing; s->alive = gs.hunter.alive;
    s->rolled = gs.hunter.rolled; s->gun = gs.hunter.gun_timer; s->invuln = gs.hunter.invuln_timer;
    s->state = gs.state; s->state_timer = gs.state_timer; s->frame = gs.frame;
#define OBJ(dst, src, A, B, C, D) dst.x = src.x; dst.y = src.y; dst.a = src.A; dst.b = src.B; dst.c = src.C; dst.d = src.D
    for (i = 0; i < 6; i++) { if (gs.bullets[i].active) { OBJ(s->bullets[i], gs.bullets[i], life, active, type, power); s->bullets[i].dx = gs.bullets[i].dx; } }
    for (i = 0; i < 3; i++) { if (gs.bombs[i].active) { OBJ(s->bombs[i], gs.bombs[i], timer, active, timer, active); } }
    for (i = 0; i < 12; i++) { if (gs.enemies[i].active) { OBJ(s->enemies[i], gs.enemies[i], health, state, timer, facing); s->enemies[i].dx = gs.enemies[i].dx; s->enemies[i].dy = gs.enemies[i].dy; } }
    for (i = 0; i < 6; i++) { if (gs.enemy_bullets[i].active) { OBJ(s->ebullets[i], gs.enemy_bullets[i], life, active, life, active); } }
    for (i = 0; i < 24; i++) { if (gs.particles[i].active) { OBJ(s->particles[i], gs.particles[i], life, color, active, life); s->particles[i].dx = gs.particles[i].dx; s->particles[i].dy = gs.particles[i].dy; } }
    for (y = 0; y < 16; y++) for (x = 0; x < 20; x++) s->room[y][x] = ROOM_TILE(y, x);
}

static void give_all(void)
{
    Hunter *h = &gs.hunter;
    h->has_roll_form = h->has_bombs = h->has_missiles = h->has_high_jump = h->has_spin_strike = 1;
    h->missiles = h->max_missiles = 20;
}
