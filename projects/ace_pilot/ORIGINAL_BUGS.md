# Ace Pilot: a bug in the original

The port keeps the original's rules (`amiga_mcp/examples/ace_pilot`,
`game.c`), checked tick by tick against it on the host (`tools/trace.sh`).
One change, marked `port:` in `src/game.c`, makes the game playable.

## Enemy planes fly at ten times their speed

The tunables say both speeds are "base * 10":

```c
    60,     /* enemy_speed (base * 10) */
    ...
    80,     /* player_speed (base * 10) */
```

The player's speed is divided by 10 (`p->speed = g_tune.player_speed / 10`,
8 units a tick), but the enemies' isn't:

```c
e->speed = (WORD)(g_tune.enemy_speed / 3);              /* blimp: 20 */
e->speed = (WORD)(g_tune.enemy_speed + w->wave * 5);    /* biplane: 65, +5 a wave */
```

so a biplane covers 65 units a tick against the player's 8 (at most 16 with
the throttle open) and the world (4000 units, wrapping) in about a second.
They can't be caught or aimed at: with the original's speeds, the
autopilot in `tools/trace.c` shoots down one plane in ten minutes; with
the speeds divided by 10 as the tunables say, 81 (wave 13). The port
divides by 10. `tools/trace.sh` builds the port with
`-DACE_ORIGINAL_ENEMY_SPEED` to compare it with the original tick by tick,
and reports both autopilot runs.
