# Pea Shooter Blast: bugs in the original

The original (`amiga_mcp/examples/pea_shooter_blast`; the Atari ST and 3DO
ports use the same `game.c` and `levels.c`) can't be finished: levels 1
and 3 have no way through to their exit gate. The port keeps the rules
and the levels and makes the two smallest changes that let it be played
to the end (`src/port_fixes.c`, marked `port:`).

## Evidence

`tools/reach.c` searches every way through a level with the original's own
code (`src/game.c` and `src/levels.c`, unchanged). From the level's start
it tries 7 inputs for 8 frames at a time (right, right + jump, left,
left + jump, jump or climb, down, nothing). Enemies are taken away, and
any damage ends that branch. States are told apart by the tank's
position (in 4-pixel cells) and its vertical speed.

```
$ sh tools/reach.sh
the original:
level 1: the gate CAN'T be reached: the tank gets no further than x 544 (column 34)
level 2: the gate can be reached
level 3: the gate CAN'T be reached: the tank gets no further than x 860 (column 53)
with the port's fixes (src/port_fixes.c):
level 1: the gate can be reached
level 2: the gate can be reached
level 3: the gate can be reached
```

## 1. The jump is too low for the walls

`game.c` jumps with `g_tune.jump_power` (default 350: 3.50 pixels a
frame up), and gravity adds 0.25 a frame. The highest jump rises about
24 pixels. `levels.c` builds 2-tile (32-pixel) walls and says they are
jumpable ("leave a jumpable opening at the top so the player can always
progress along the ground route"):

- the brick wall at column 35 of level 1 (rows 12-13), right after the
  start;
- in level 3, the pit at columns 50-54 and the walls past it.

The tank stands with its top at y 210. To clear a wall whose top is at
y 192, its 14-pixel body must get above 192, which is a 33-pixel rise.
`game.h` also defines `TANK_JUMP_VEL` as -4, but nothing uses it, and 4.0
isn't enough either: the search stops at column 34 of level 1 with it.

**Port:** `g_tune.jump_power = 450` (4.50), through the original's own
tunables, which the Amiga version exposes over its bridge for tuning. With
fix 2, the search finds 4.40 to be the smallest that gets through both
levels (4.20 clears level 1 but not level 3).

## 2. Level 1's metal block closes the way

```c
/* Metal section near end */
for (y = 10; y < 14; y++)
    for (x = 95; x < 100; x++)
        level_maps[0][y][x] = TILE_METAL;
for (x = 95; x < 105; x++)
    level_maps[0][9][x] = TILE_PLATFORM;
```

The block fills rows 10-13 from the floor up (64 pixels), and its top is
a platform at row 9. No platform or ladder between columns 65 and 94 is
high enough to drop onto it, so even with fix 1 the tank stops at
column 94. The search reaches column 94 at every jump power up to 5.00.

**Port:** a 2-tile doorway at the floor (rows 12-13 of columns 95-99),
like the doorways `levels.c` cuts into the boss arenas' walls. The rest
of the block and its platform stay.
