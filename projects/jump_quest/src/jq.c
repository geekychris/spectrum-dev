/*
 * Jump Quest - the original's logic (see jq.h): player.c, enemy.c, items.c,
 * level.c and the state machine of main.c, line for line where it could be.
 * The drawing and the sound are the front end's (main.c); the original's
 * sound_*() calls set game.events here.
 */
#include <stdint.h>
#include <string.h>
#include "jq.h"
#include "tmap.h"
#include "zxrle.h"
#include "levels_data.h"

Game game;

const LevelDef levels[NUM_LEVELS] = {
    { LEVEL1_W, level1_packed, level1_ents, "OFFICE PARK" },
    { LEVEL2_W, level2_packed, level2_ents, "SERVER ROOM" },
    { LEVEL3_W, level3_packed, level3_ents, "ROOFTOP" },
};

#define sound_jump()     (game.events |= EV_JUMP)
#define sound_stomp()    (game.events |= EV_STOMP)
#define sound_collect()  (game.events |= EV_COLLECT)
#define sound_powerup()  (game.events |= EV_POWERUP)
#define sound_hurt()     (game.events |= EV_HURT)
#define sound_die()      (game.events |= EV_DIE)
#define sound_levelwin() (game.events |= EV_LEVELWIN)

/* the original's `v / TILE_SIZE` (rounds towards zero, also for v < 0) */
static int16_t div16(int16_t v)
{
    return v >= 0 ? (v >> 4) : -((-v) >> 4);
}

/* ------------------------------------------------------------ level.c */

static const uint8_t solid[NUM_TILES] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0
};

void jq_restore(void)
{
    tmap_init(game.tiles, levels[game.current_level].width, TILES_Y);
}

static void level_load(int8_t level_num)
{
    const LevelDef *ld;
    if (level_num < 0 || level_num >= NUM_LEVELS) level_num = 0;
    ld = &levels[level_num];
    rle_unpack(game.tiles, ld->tiles);     /* port: the levels are packed (tools/gen_levels.py) */
    tmap_init(game.tiles, ld->width, TILES_Y);
}

const LevelDef *level_current(void)
{
    return &levels[game.current_level];
}

uint8_t level_get_tile(int16_t tx, int16_t ty)
{
    return tmap_at(tx, ty, TILE_EMPTY);
}

static void level_set_tile(int16_t tx, int16_t ty, uint8_t tile)
{
    if (tx < 0 || tx >= tmap_w || ty < 0 || ty >= TILES_Y) return;
    TMAP(tx, ty) = tile;
    game.tile_tx = (uint8_t)tx;            /* port: the front end redraws it */
    game.tile_ty = (uint8_t)ty;
    game.events |= EV_TILE;
}

uint8_t level_is_solid(int16_t tx, int16_t ty)
{
    return solid[level_get_tile(tx, ty)];
}

static uint8_t level_is_platform(int16_t tx, int16_t ty)
{
    return level_get_tile(tx, ty) == TILE_PLATFORM;
}

int16_t level_width_pixels(void)
{
    return (int16_t)tmap_w * TILE_SIZE;
}

/* ------------------------------------------------------------ player.c */

static void player_init(Player *p, int8_t character, int16_t start_x, int16_t start_y)
{
    p->x = start_x;
    p->y = start_y;
    p->vx = 0;
    p->vy = 0;
    p->dir = DIR_RIGHT;
    p->on_ground = 0;
    p->character = character;
    p->health = 3;
    p->max_health = 5;
    p->lives = 3;
    p->score = 0;
    p->state = 0;
    p->frame = 0;
    p->frame_timer = 0;
    p->invuln = 0;
    p->speed_boost = 0;
    p->walk_frame = 0;
}

static uint8_t point_solid(int16_t wx, int16_t wy)
{
    return level_is_solid(div16(wx), div16(wy));
}

static uint8_t on_platform(int16_t wx, int16_t wy)
{
    return level_is_platform(div16(wx), div16(wy));
}

static uint8_t enemies_check_stomp(int16_t px, int16_t py, int16_t pw, int16_t ph);
static uint8_t enemies_check_touch(int16_t px, int16_t py, int16_t pw, int16_t ph);
static int8_t items_check_collect(int16_t px, int16_t py, int16_t pw, int16_t ph);

/* port: player_update() works on a copy of the active player at a fixed
   address (pl): zsdcc's code for a fixed address is a third the size of its
   code through a pointer argument */
static Player pl;
#define p (&pl)

static void player_update(uint8_t input)
{
    int16_t ph = (p->character == CHAR_RJ) ? PLAYER_H : PLAYER_H_SHORT;
    int16_t speed = MOVE_SPEED;
    int16_t new_x, new_y;
    int16_t top_y, bot_y;
    int16_t tx, ty;

    if (p->speed_boost > 0) {
        speed = MOVE_SPEED + FP(1);
        p->speed_boost--;
    }
    if (p->invuln > 0) p->invuln--;

    /* Horizontal movement */
    p->vx = 0;
    if (input & INP_LEFT) {
        p->vx = -speed;
        p->dir = DIR_LEFT;
    }
    if (input & INP_RIGHT) {
        p->vx = speed;
        p->dir = DIR_RIGHT;
    }

    /* Jump */
    if ((input & INP_JUMP) && p->on_ground) {
        p->vy = JUMP_FORCE;
        p->on_ground = 0;
        sound_jump();
    }

    /* Gravity */
    p->vy += GRAVITY;
    if (p->vy > MAX_FALL) p->vy = MAX_FALL;

    /* Horizontal collision */
    new_x = p->x + FP_INT(p->vx);
    top_y = p->y + 2;
    bot_y = p->y + ph - 2;

    if (new_x < 0) new_x = 0;
    if (new_x + PLAYER_W > level_width_pixels())
        new_x = level_width_pixels() - PLAYER_W;

    if (p->vx < 0) {
        if (point_solid(new_x + 1, top_y) || point_solid(new_x + 1, bot_y) ||
            point_solid(new_x + 1, p->y + ph / 2)) {
            tx = div16(new_x + 1);
            new_x = (tx + 1) * TILE_SIZE - 1;
        }
    } else if (p->vx > 0) {
        if (point_solid(new_x + PLAYER_W - 1, top_y) ||
            point_solid(new_x + PLAYER_W - 1, bot_y) ||
            point_solid(new_x + PLAYER_W - 1, p->y + ph / 2)) {
            tx = div16(new_x + PLAYER_W - 1);
            new_x = tx * TILE_SIZE - PLAYER_W;
        }
    }
    p->x = new_x;

    /* Vertical collision */
    new_y = p->y + FP_INT(p->vy);

    if (p->vy < 0) {
        /* Moving up - check head */
        top_y = new_y;
        if (point_solid(p->x + 2, top_y) || point_solid(p->x + PLAYER_W - 3, top_y)) {
            ty = div16(top_y);
            new_y = (ty + 1) * TILE_SIZE;
            p->vy = 0;
            /* Hit block from below - check for question block */
            if (level_get_tile(div16(p->x) + 1, ty) == TILE_QBLOCK ||
                level_get_tile(div16(p->x + PLAYER_W - 3), ty) == TILE_QBLOCK) {
                int16_t btx = div16(p->x + PLAYER_W / 2);
                if (level_get_tile(btx, ty) == TILE_QBLOCK) {
                    level_set_tile(btx, ty, TILE_QBLOCK_HIT);
                    sound_collect();
                    p->score += 100;
                }
            }
        }
    } else {
        /* Moving down - check feet */
        bot_y = new_y + ph;
        p->on_ground = 0;

        if (point_solid(p->x + 2, bot_y) || point_solid(p->x + PLAYER_W - 3, bot_y)) {
            ty = div16(bot_y);
            new_y = ty * TILE_SIZE - ph;
            p->vy = 0;
            p->on_ground = 1;
        }
        /* Check platforms (only when falling) */
        else if (p->vy > 0) {
            int16_t foot_ty = div16(bot_y);
            int16_t prev_bot = p->y + ph;
            int16_t prev_ty = div16(prev_bot);
            if (foot_ty != prev_ty &&
                (on_platform(p->x + 2, bot_y) ||
                 on_platform(p->x + PLAYER_W - 3, bot_y))) {
                new_y = foot_ty * TILE_SIZE - ph;
                p->vy = 0;
                p->on_ground = 1;
            }
        }
    }
    p->y = new_y;

    /* Fall off bottom = death */
    if (p->y > TILES_Y * TILE_SIZE) {
        p->health = 0;
    }

    /* Animation state */
    if (!p->on_ground) {
        p->state = (p->vy < 0) ? 2 : 3; /* jump or fall */
    } else if (p->vx != 0) {
        p->state = 1; /* walk */
        p->frame_timer++;
        if (p->frame_timer >= 6) {
            p->frame_timer = 0;
            p->walk_frame = (p->walk_frame + 1) & 3;
        }
    } else {
        p->state = 0; /* idle */
        p->walk_frame = 0;
    }

    /* Check enemy collision */
    if (p->invuln == 0) {
        if (p->vy > 0 && enemies_check_stomp(p->x, p->y, PLAYER_W, ph)) {
            /* Stomped an enemy - bounce up */
            p->vy = JUMP_FORCE / 2;
            p->score += 200;
            sound_stomp();
        } else if (enemies_check_touch(p->x, p->y, PLAYER_W, ph)) {
            p->health--;
            p->invuln = 60; /* 1.2 seconds invulnerability */
            sound_hurt();
        }
    }

    /* Check item collection */
    {
        int8_t item_type = items_check_collect(p->x, p->y, PLAYER_W, ph);
        if (item_type > 0) {
            switch (item_type) {
            case ENT_BOARD:    p->score += 50;  break;
            case ENT_DISKETTE: p->score += 100; break;
            case ENT_BALL:     p->score += 75;  break;
            case ENT_PILLOW_C: p->score += 150; break;
            case ENT_PLANT:    p->score += 200; break;
            case ENT_BRIEFCASE:
                p->invuln = 150; /* 3 seconds */
                sound_powerup();
                break;
            case ENT_HEART:
                if (p->health < p->max_health) p->health++;
                sound_powerup();
                break;
            case ENT_PILLOW_P:
                p->lives++;
                sound_powerup();
                break;
            case ENT_BEER:
            case ENT_WINE:
                p->speed_boost = 200; /* 4 seconds */
                sound_powerup();
                break;
            }
            if (item_type >= ENT_BOARD && item_type <= ENT_PLANT)
                sound_collect();
        }
    }
}

#undef p

/* ------------------------------------------------------------ enemy.c */

/* the original's sine_table[32], divided by 4 as enemies_update() does */
static const int8_t fly_dy[32] = {
    0, 0, 1, 2, 3, 3, 3, 4, 4, 4, 3, 3, 3, 2, 1, 0,
    0, 0, -1, -2, -3, -3, -3, -4, -4, -4, -3, -3, -3, -2, -1, 0
};

static void enemies_spawn(const EntitySpawn *spawns)
{
    uint8_t i = 0;
    const EntitySpawn *s;
    Enemy *e = game.enemies;

    memset(game.enemies, 0, sizeof(game.enemies));
    for (s = spawns; s->type != ENT_NONE && i < MAX_ENEMIES; s++) {
        if (s->type >= ENT_BEETLE && s->type <= ENT_ANT) {
            e->type = s->type;
            e->x = s->tx * TILE_SIZE;
            e->y = s->ty * TILE_SIZE;
            e->alive = 1;
            e->dir = DIR_LEFT;
            e->frame = 0;
            e->timer = 0;

            switch (s->type) {
            case ENT_BEETLE:
            case ENT_FLY:
                e->vx = FP(1);
                e->vy = 0;
                break;
            case ENT_SPIDER:
                e->vx = 0;
                e->vy = FP(1);
                e->dir = DIR_RIGHT; /* positive = dropping */
                break;
            case ENT_ANT:
                e->vx = FP(2);
                e->vy = 0;
                break;
            }
            e++;
            i++;
        }
    }
}

static void enemies_update(void)
{
    uint8_t i;
    Enemy *e = game.enemies;

    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        int16_t l, r, m, f;
        if (!e->alive) continue;

        e->timer++;
        e->frame = (e->timer >> 3) & 1;

        switch (e->type) {
        case ENT_BEETLE:
        case ENT_ANT:
            /* Walk left/right, reverse at walls/edges */
            if (e->dir < 0) e->x -= FP_INT(e->vx);
            else e->x += FP_INT(e->vx);
            l = div16(e->x);
            r = div16(e->x + 14);
            m = div16(e->y);
            f = div16(e->y + 12);

            /* Wall collision */
            if (e->dir < 0) {
                if (level_is_solid(l, m) || level_is_solid(l, f))
                    e->dir = DIR_RIGHT;
            } else {
                if (level_is_solid(r, m) || level_is_solid(r, f))
                    e->dir = DIR_LEFT;
            }

            /* Edge detection - don't walk off platforms */
            f = div16(e->y + TILE_SIZE);
            if (!level_is_solid(l, f) && !level_is_platform(l, f) && e->dir < 0)
                e->dir = DIR_RIGHT;
            if (!level_is_solid(r, f) && !level_is_platform(r, f) && e->dir > 0)
                e->dir = DIR_LEFT;
            break;

        case ENT_FLY:
            /* Fly in sine wave pattern */
            if (e->dir < 0) e->x -= FP_INT(e->vx);
            else e->x += FP_INT(e->vx);
            e->y += fly_dy[e->timer & 31];

            /* Reverse at screen bounds relative to spawn */
            if (e->x < 0 || div16(e->x) >= (int16_t)tmap_w - 1)
                e->dir = -e->dir;
            /* Simple boundary reversal */
            if (e->timer > 120) {
                e->dir = -e->dir;
                e->timer = 0;
            }
            break;

        case ENT_SPIDER:
            /* Drop down, then climb back up */
            if (e->dir < 0) e->y -= FP_INT(e->vy);
            else e->y += FP_INT(e->vy);
            if (e->dir > 0) {
                /* Dropping */
                if (level_is_solid(div16(e->x), div16(e->y + 14)))
                    e->dir = DIR_LEFT; /* negative = climb up */
                if (e->timer > 100) {
                    e->dir = DIR_LEFT;
                    e->timer = 0;
                }
            } else {
                /* Climbing */
                if (e->timer > 60) {
                    e->dir = DIR_RIGHT;
                    e->timer = 0;
                }
            }
            break;
        }
    }
}

static uint8_t enemies_check_stomp(int16_t px, int16_t py, int16_t pw, int16_t ph)
{
    uint8_t i;
    Enemy *e = game.enemies;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        if (!e->alive) continue;
        if (px + pw > e->x + 2 && px < e->x + 14 &&
            py + ph > e->y && py + ph < e->y + 10) {
            e->alive = 0;
            return 1;
        }
    }
    return 0;
}

static uint8_t enemies_check_touch(int16_t px, int16_t py, int16_t pw, int16_t ph)
{
    uint8_t i;
    Enemy *e = game.enemies;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        if (!e->alive) continue;
        if (px + pw > e->x + 3 && px < e->x + 13 &&
            py + ph > e->y + 3 && py < e->y + 13)
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ items.c */

static void items_spawn(const EntitySpawn *spawns)
{
    uint8_t i = 0;
    const EntitySpawn *s;
    Item *it = game.items;

    memset(game.items, 0, sizeof(game.items));
    for (s = spawns; s->type != ENT_NONE && i < MAX_ITEMS; s++) {
        if (s->type >= ENT_BOARD && s->type <= ENT_WINE) {
            it->type = s->type;
            it->x = s->tx * TILE_SIZE;
            it->y = s->ty * TILE_SIZE;
            it->active = 1;
            it++;
            i++;
        }
    }
}

/* the original's sine_table_small(t) = s[(t >> 2) % 12], for t = 0..255 */
static const int8_t bob[12] = { 0, 1, 2, 2, 2, 1, 0, -1, -2, -2, -2, -1 };

static void items_update(void)
{
    uint8_t i;
    Item *it = game.items;
    for (i = 0; i < MAX_ITEMS; i++, it++) {
        if (!it->active) continue;
        it->frame++;
        it->float_off = bob[(uint8_t)(it->frame >> 2) % 12];
    }
}

static int8_t items_check_collect(int16_t px, int16_t py, int16_t pw, int16_t ph)
{
    uint8_t i;
    Item *it = game.items;
    for (i = 0; i < MAX_ITEMS; i++, it++) {
        if (!it->active) continue;
        if (px + pw > it->x + 2 && px < it->x + 14 &&
            py + ph > it->y + 2 && py < it->y + 14) {
            it->active = 0;
            return it->type;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ main.c */

static void particles_spawn(int16_t x, int16_t y, uint8_t count, int8_t color)
{
    uint8_t i, j;
    Particle *q = game.particles;
    for (i = 0, j = 0; i < MAX_PARTICLES && j < count; i++, q++) {
        if (q->life <= 0) {
            q->x = x;
            q->y = y;
            q->vx = (int16_t)((i * 7 + 3) % 9 - 4);
            q->vy = (int16_t)(-(i * 5 % 8 + 2));
            q->life = 20 + (i & 7);
            q->color = color;
            j++;
        }
    }
    game.events |= EV_PARTICLES;
}

static void particles_update(void)
{
    uint8_t i;
    Particle *q = game.particles;
    for (i = 0; i < MAX_PARTICLES; i++, q++) {
        if (q->life > 0) {
            q->x += q->vx;
            q->y += q->vy;
            q->vy += 1;
            q->life--;
        }
    }
}

static void start_level(int8_t level_num)
{
    const LevelDef *ld;
    const EntitySpawn *s;
    Player *p;

    game.current_level = level_num;
    level_load(level_num);
    ld = level_current();

    enemies_spawn(ld->entities);
    items_spawn(ld->entities);
    memset(game.particles, 0, sizeof(game.particles));

    p = &game.player[game.active_player];

    for (s = ld->entities; s->type != ENT_NONE; s++) {
        if (s->type == ENT_PLAYER_START) {
            /* port: player_init() reset the score, lives and health too,
               though the original's comment here says they are kept: every
               level and every life started at 0 points with 3 lives, so no
               game could end. They are kept (the death code sets health 3). */
            int32_t score = p->score;
            int8_t lives = p->lives, health = p->health;
            player_init(p, p->character, s->tx * TILE_SIZE, s->ty * TILE_SIZE - PLAYER_H + TILE_SIZE);
            p->score = score;
            p->lives = lives;
            p->health = health;
            break;
        }
    }

    game.cam_x = 0;
    game.state = STATE_PLAYING;
    game.transition = 0;
    game.level_ticks = 0;
    game.events |= EV_LEVEL;
}

static void update_camera(Player *p)
{
    int16_t target_x = p->x - SCREEN_W / 3;
    int16_t max_x = level_width_pixels() - SCREEN_W;

    if (target_x < 0) target_x = 0;
    if (target_x > max_x) target_x = max_x;

    /* Smooth camera, snap to tile boundary */
    if (game.cam_x < target_x)
        game.cam_x += (target_x - game.cam_x + 3) / 4;
    else if (game.cam_x > target_x)
        game.cam_x -= (game.cam_x - target_x + 3) / 4;

    game.cam_x = (game.cam_x >> 4) << 4;     /* cam_x >= 0 */
}

static uint8_t check_level_end(Player *p)
{
    const EntitySpawn *s;
    int16_t px_center = p->x + PLAYER_W / 2;
    int16_t py_center = p->y + PLAYER_H / 2;

    for (s = level_current()->entities; s->type != ENT_NONE; s++) {
        if (s->type == ENT_LEVEL_END) {
            int16_t ex = s->tx * TILE_SIZE;
            int16_t ey = s->ty * TILE_SIZE;
            if (px_center > ex && px_center < ex + TILE_SIZE * 2 &&
                py_center > ey - TILE_SIZE && py_center < ey + TILE_SIZE * 2)
                return 1;
        }
    }
    return 0;
}

static void new_player(Player *p, int8_t character)
{
    memset(p, 0, sizeof(*p));
    p->character = character;
    p->lives = 3;
    p->health = 3;
    p->max_health = 5;
}

void jq_new_game(uint8_t character, uint8_t num_players)
{
    memset(&game, 0, sizeof(game));
    game.num_players = num_players;
    new_player(&game.player[0], character);
    if (num_players == 2)
        new_player(&game.player[1], character == CHAR_RJ ? CHAR_DALE : CHAR_RJ);
    game.active_player = 0;
    start_level(0);
}

void jq_tick(uint8_t inp)
{
    Player *p = &game.player[game.active_player];
    game.events = 0;
    game.level_ticks++;

    switch (game.state) {
    case STATE_PLAYING:
        memcpy(&pl, p, sizeof(Player));
        player_update(inp);
        memcpy(p, &pl, sizeof(Player));
        enemies_update();
        items_update();
        particles_update();
        update_camera(p);

        if (check_level_end(p)) {
            game.state = STATE_LEVELWIN;
            game.transition = 100;
            sound_levelwin();
        }
        if (p->health <= 0) {
            game.state = STATE_DYING;
            game.transition = 80;
            sound_die();
        }
        break;

    case STATE_DYING:
        game.transition--;
        particles_update();
        if (game.transition == 70)
            particles_spawn(p->x, p->y, 8, 0);
        if (game.transition <= 0) {
            p->lives--;
            if (p->lives <= 0) {
                if (game.num_players == 2 && game.active_player == 0 &&
                    game.player[1].lives > 0) {
                    game.active_player = 1;
                    start_level(game.current_level);
                } else if (game.num_players == 2 && game.active_player == 1 &&
                           game.player[0].lives > 0) {
                    game.active_player = 0;
                    start_level(game.current_level);
                } else {
                    game.state = STATE_GAMEOVER;
                    game.transition = 150;
                    game.events |= EV_GAMEOVER;
                }
            } else {
                p->health = 3;
                start_level(game.current_level);
            }
        }
        break;

    case STATE_LEVELWIN:
        game.transition--;
        if (game.transition <= 0) {
            p->score += 500;
            if (game.current_level + 1 >= NUM_LEVELS) {
                game.current_level = NUM_LEVELS;   /* game won */
                game.state = STATE_GAMEOVER;
                game.transition = 200;
                game.events |= EV_GAMEOVER;
            } else {
                if (game.num_players == 2)
                    game.active_player ^= 1;
                start_level(game.current_level + 1);
            }
        }
        break;

    case STATE_GAMEOVER:
        if (game.transition > 0) game.transition--;
        else if (inp & (INP_JUMP | INP_START))
            game.state = STATE_TITLE;
        break;
    }
}
