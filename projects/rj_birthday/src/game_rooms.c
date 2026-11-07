/*
 * game_rooms.c - port: three of game.c's room updates (the original's
 * code, moved unchanged): the Amsterdam lounge's Tapper bar, the Tokyo
 * room's sushi conveyor, the Bay Area patio's scooters. In an 8K page of
 * their own at 0x6000 (see main.c).
 */
#include <string.h>
#include "game.h"
#include "game_int.h"

void update_amsterdam(GameState *gs, InputState *inp)
{
    WORD i;
    Player *p = &gs->player;
    WORD room_x = ROOM_AMSTERDAM * ROOM_W;

    /* Player can serve brownies by pressing fire anywhere in Amsterdam */
    if (inp->fire_edge && p->x >= room_x && p->x < room_x + ROOM_W) {
        /* Determine which lane based on player Y */
        WORD serve_lane = (p->y - TAPPER_LANE_Y0 + TAPPER_LANE_H / 2) / TAPPER_LANE_H;
        if (serve_lane < 0) serve_lane = 0;
        if (serve_lane >= TAPPER_LANES) serve_lane = TAPPER_LANES - 1;
        spawn_item(gs, ITEM_BROWNIE,
                   room_x + 30,
                   TAPPER_LANE_Y0 + serve_lane * TAPPER_LANE_H + 10,
                   4, 0, ROOM_AMSTERDAM);
        sfx_slide();
    }

    /* Move brownies right */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_AMSTERDAM) continue;
        if (it->type != ITEM_BROWNIE) continue;

        it->x += it->vx;

        /* Check collision with guests in same lane */
        {
            WORD j;
            for (j = 0; j < MAX_GUESTS; j++) {
                Guest *g = &gs->guests[j];
                if (!g->active || g->room != ROOM_AMSTERDAM) continue;
                if (g->state != GUEST_WANT) continue;

                /* Check if brownie hits guest */
                if (it->x + 8 >= g->x && it->x <= g->x + GUEST_W) {
                    WORD item_lane = (it->y - TAPPER_LANE_Y0 -
                                      ROOM_AMSTERDAM * 0 + 2) / TAPPER_LANE_H;
                    if (item_lane == g->lane) {
                        it->active = 0;
                        g->state = GUEST_HAPPY;
                        g->timer = 60;
                        gs->score += 200;
                        sfx_ding();
                        game_set_message(gs, "ENJOY!", 20);
                    }
                }
            }
        }

        /* Brownie fell off right edge */
        if (it->x > room_x + ROOM_W) {
            it->active = 0;
            sfx_crash();
        }
    }

    /* Spawn guests on right approaching bar - delayed start */
    if (gs->party_clock > 200 && (gs->party_clock % (180 - gs->wave * 8)) == 0) {
        Guest *g = spawn_guest(gs, ROOM_AMSTERDAM,
                               room_x + 300,
                               TAPPER_LANE_Y0 + rng_range(0, TAPPER_LANES - 1) * TAPPER_LANE_H + 14);
        if (g) {
            g->want = ITEM_BROWNIE;
            g->patience = 500 - gs->wave * 20;
            g->patience_max = g->patience;
            g->bubble_timer = g->patience;
            g->lane = (g->y - TAPPER_LANE_Y0) / TAPPER_LANE_H;
            g->target_x = room_x + 50;
        }
    }

    /* Move approaching guests left */
    for (i = 0; i < MAX_GUESTS; i++) {
        Guest *g = &gs->guests[i];
        if (!g->active || g->room != ROOM_AMSTERDAM) continue;

        if (g->state == GUEST_ENTERING) {
            g->x -= 1;
            if (g->x <= g->target_x) {
                g->state = GUEST_WANT;
            }
        }
        else if (g->state == GUEST_WANT) {
            g->patience--;
            if (g->patience <= 0) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 10;
                sfx_buzzer();
                game_set_message(gs, "CUSTOMER LEFT!", 30);
            }
            /* Slowly approach bar */
            if ((gs->party_clock & 15) == 0) g->x -= 1;
            if (g->x < room_x + 30) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 10;
                sfx_buzzer();
            }
        }
        else if (g->state == GUEST_HAPPY || g->state == GUEST_ANGRY) {
            g->timer--;
            if (g->timer <= 0) {
                g->state = GUEST_LEAVING;
                g->target_x = room_x + 330;
            }
        }
        else if (g->state == GUEST_LEAVING) {
            g->x += 2;
            if (g->x > room_x + 330) {
                g->active = 0;
            }
        }
    }

    /* Spawn plants around the room */
    if ((gs->party_clock % 150) == 50) {
        spawn_item(gs, ITEM_PLANT,
                   room_x + rng_range(40, 300),
                   rng_range(FLOOR_Y - 15, FLOOR_Y),
                   0, 0, ROOM_AMSTERDAM);
    }

    /* Player picks up plants by walking over them */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_AMSTERDAM || it->type != ITEM_PLANT)
            continue;
        {
            WORD dx = p->x - it->x;
            WORD dy = p->y - it->y;
            if (dx > -15 && dx < 15 && dy > -15 && dy < 15) {
                it->active = 0;
                gs->score += 100;
                gs->happiness += 2;
                gs->plants_collected++;
                sfx_pickup();
                /* Spawn smoke puffs from RJ's head */
                {
                    WORD si;
                    for (si = 0; si < 4; si++)
                        spawn_smoke(gs, p->x, p->y - PLAYER_H - 4);
                }
                gs->rj_bubble_timer = 40;
                {
                    static const char *puff_msgs[] = {
                        "Far out!",
                        "Groovy!",
                        "Mellow...",
                        "Nice!",
                        "Whoa dude!"
                    };
                    const char *pm = puff_msgs[rng() % 5];
                    WORD j;
                    for (j = 0; pm[j] && j < BUBBLE_LEN - 1; j++)
                        gs->rj_bubble[j] = pm[j];
                    gs->rj_bubble[j] = 0;
                }
            }
        }
    }
}

void update_tokyo(GameState *gs, InputState *inp)
{
    WORD i;
    Player *p = &gs->player;
    WORD room_x = ROOM_TOKYO * ROOM_W;
    static const WORD sushi_types[] = { ITEM_SUSHI_R, ITEM_SUSHI_B, ITEM_SUSHI_G };

    /* Spawn sushi on conveyor */
    if ((gs->party_clock % (60 - gs->wave * 3)) == 0) {
        WORD lane = rng_range(0, SUSHI_LANES - 1);
        WORD stype = sushi_types[rng_range(0, 2)];
        spawn_item(gs, stype,
                   room_x - 10,
                   SUSHI_LANE_Y0 + lane * SUSHI_LANE_H + 8,
                   2, 0, ROOM_TOKYO);
    }

    /* Move sushi right */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_TOKYO) continue;
        it->x += it->vx;

        /* Player picks up sushi */
        if (p->carrying == ITEM_NONE && inp->fire_edge) {
            WORD dx = p->x - it->x;
            WORD dy = p->y - it->y;
            if (dx > -15 && dx < 15 && dy > -15 && dy < 15) {
                p->carrying = it->type;
                it->active = 0;
                sfx_pickup();
            }
        }

        /* Sushi falls off right edge */
        if (it->x > room_x + ROOM_W + 10) {
            it->active = 0;
        }
    }

    /* Serve sushi to guests */
    if (p->carrying >= ITEM_SUSHI_R && p->carrying <= ITEM_SUSHI_G && inp->fire_edge) {
        for (i = 0; i < MAX_GUESTS; i++) {
            Guest *g = &gs->guests[i];
            WORD dx, dy;
            if (!g->active || g->room != ROOM_TOKYO || g->state != GUEST_WANT) continue;
            dx = p->x - g->x;
            dy = p->y - g->y;
            if (dx > -20 && dx < 20 && dy > -20 && dy < 20) {
                if (p->carrying == g->want) {
                    g->state = GUEST_HAPPY;
                    g->timer = 60;
                    gs->score += 300;
                    sfx_ding();
                    sfx_chomp();
                    game_set_message(gs, "OISHII!", 25);
                } else {
                    g->state = GUEST_ANGRY;
                    g->timer = 40;
                    gs->happiness -= 5;
                    sfx_buzzer();
                    game_set_message(gs, "WRONG SUSHI!", 25);
                }
                p->carrying = ITEM_NONE;
                break;
            }
        }
    }

    /* Spawn guest diners */
    if ((gs->party_clock % (150 - gs->wave * 10)) == 0) {
        Guest *g = spawn_guest(gs, ROOM_TOKYO,
                               room_x + rng_range(200, 290),
                               SUSHI_LANE_Y0 + rng_range(0, SUSHI_LANES - 1) * SUSHI_LANE_H + 14);
        if (g) {
            g->want = sushi_types[rng_range(0, 2)];
            g->patience = 400 - gs->wave * 20;
            g->patience_max = g->patience;
            g->bubble_timer = g->patience;
            g->state = GUEST_WANT;
        }
    }

    /* Update guest patience */
    for (i = 0; i < MAX_GUESTS; i++) {
        Guest *g = &gs->guests[i];
        if (!g->active || g->room != ROOM_TOKYO) continue;
        if (g->state == GUEST_WANT) {
            g->patience--;
            if (g->patience <= 0) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 8;
                sfx_buzzer();
            }
        }
        else if (g->state == GUEST_HAPPY || g->state == GUEST_ANGRY) {
            g->timer--;
            if (g->timer <= 0) g->active = 0;
        }
    }
}

void update_bayarea(GameState *gs, InputState *inp)
{
    WORD i;
    Player *p = &gs->player;
    WORD room_x = ROOM_BAYAREA * ROOM_W;

    /* Spawn scooters (obstacles) */
    if ((gs->party_clock % (40 - gs->wave * 2)) == 0) {
        WORD dir = rng() & 1;
        spawn_item(gs, ITEM_SCOOTER,
                   dir ? room_x - 10 : room_x + 330,
                   FLOOR_Y - 20,
                   dir ? 5 : -5, 0, ROOM_BAYAREA);
    }

    /* Spawn party favor pickups */
    if ((gs->party_clock % 200) == 0) {
        spawn_item(gs, ITEM_FAVOR,
                   room_x + rng_range(30, 290),
                   FLOOR_Y - 10,
                   0, 0, ROOM_BAYAREA);
    }

    /* Move items, check collisions */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_BAYAREA) continue;

        if (it->type == ITEM_SCOOTER) {
            /* Moving obstacle (scooter) */
            it->x += it->vx;

            /* Hit player? */
            {
                WORD dx = p->x - it->x;
                WORD dy = p->y - it->y;
                if (dx > -10 && dx < 10 && dy > -10 && dy < 10) {
                    gs->lives--;
                    gs->happiness -= 10;
                    sfx_crash();
                    game_set_message(gs, "SCOOTER HIT!", 30);
                    /* Push player back */
                    p->x += (it->vx > 0) ? 20 : -20;
                    it->active = 0;
                }
            }

            /* Off screen */
            if (it->x < room_x - 20 || it->x > room_x + 340)
                it->active = 0;
        } else {
            /* Stationary pickup */
            if (inp->fire_edge && p->carrying == ITEM_NONE) {
                WORD dx = p->x - it->x;
                WORD dy = p->y - it->y;
                if (dx > -15 && dx < 15 && dy > -15 && dy < 15) {
                    p->carrying = ITEM_FAVOR;
                    it->active = 0;
                    sfx_pickup();
                }
            }
        }
    }

    /* Deliver favors to guests */
    if (p->carrying == ITEM_FAVOR && inp->fire_edge) {
        for (i = 0; i < MAX_GUESTS; i++) {
            Guest *g = &gs->guests[i];
            WORD dx, dy;
            if (!g->active || g->room != ROOM_BAYAREA || g->state != GUEST_WANT) continue;
            dx = p->x - g->x;
            dy = p->y - g->y;
            if (dx > -20 && dx < 20 && dy > -20 && dy < 20) {
                g->state = GUEST_HAPPY;
                g->timer = 60;
                gs->score += 200;
                p->carrying = ITEM_NONE;
                sfx_ding();
                game_set_message(gs, "PARTY ON!", 20);
                break;
            }
        }
    }

    /* Spawn patio guests */
    if ((gs->party_clock % (160 - gs->wave * 10)) == 0) {
        Guest *g = spawn_guest(gs, ROOM_BAYAREA,
                               room_x + rng_range(50, 280),
                               FLOOR_Y);
        if (g) {
            g->want = ITEM_FAVOR;
            g->patience = 350 - gs->wave * 15;
            g->patience_max = g->patience;
            g->bubble_timer = g->patience;
            g->state = GUEST_WANT;
        }
    }

    /* Update guest patience */
    for (i = 0; i < MAX_GUESTS; i++) {
        Guest *g = &gs->guests[i];
        if (!g->active || g->room != ROOM_BAYAREA) continue;
        if (g->state == GUEST_WANT) {
            g->patience--;
            if (g->patience <= 0) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 8;
                sfx_buzzer();
            }
        }
        else if (g->state == GUEST_HAPPY || g->state == GUEST_ANGRY) {
            g->timer--;
            if (g->timer <= 0) g->active = 0;
        }
    }

    (void)inp;
}
