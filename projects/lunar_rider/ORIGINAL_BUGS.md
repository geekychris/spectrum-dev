# Lunar Rider: bugs in the original

The port keeps the original's rules (`amiga_mcp/examples/lunar_rider`,
unchanged there). Where a bug made a game unwinnable from its first second,
the port fixes it and marks the change `port:` in `src/game.c`.

## A game can start over a crater (fixed)

`generate_terrain_chunk()` keeps the start clear:

```c
if (world_x > 30) {  /* leave initial area clear */
```

but the buggy doesn't stand at world x 0..30: it sits at screen x
`BUGGY_X` = 80, 24 pixels wide, so at the start it covers world x 80..104.
Craters, rocks and mines can be generated right under it or a few pixels
ahead.

Evidence: the original's main loop calls `game_init()` at start-up and again
when the player starts, so the first game is the random generator's second
terrain. With the original's seed (54321) that terrain has a 16-pixel crater
under the buggy's back half and a rock 28 pixels ahead of its front (the
port's probe, the terrain from the buggy's left edge on, before the fix:
`cccccccccccccccc..........hhhhhhhhhhhhhhhhhhhhhhhh..R`). The rock is hit
after about 20 frames unless the player jumps at the right moment: an early
build of the port that ignored input for the first 15 frames lost a life
there every time (the log: `death lives=3 cause=rock x=28`).

The port keeps 30 pixels beyond the buggy's front clear instead
(`world_x > BUGGY_X + BUGGY_W + 30`), which is what the comment says the
code does.

## Kept as the original

These change how the game plays but don't stop it being played, so they are
the original's behaviour in the port too:

- **Rocks and mines are one pixel of terrain.** The rock test looks at the
  single terrain column under the buggy's front (`BUGGY_X + BUGGY_W`), the
  mine test under its centre, once a frame. At normal speed (1.5 pixels a
  frame) the buggy steps over about a third of them, at full speed (3) two
  thirds, without touching them; a forward bullet (5 pixels a frame) also
  misses most of them. The port draws them 5 and 4 pixels wide so they can
  be seen, but the collisions are the original's.
- **A respawn is where the buggy died.** After a crater death the new
  buggy appears over the same crater, on the ground for one frame: jumping
  on that frame or speeding across (holding right) saves it, otherwise it
  falls again.
- **The far and near mountains jump.** The original wraps the parallax
  offsets at 640 (`(scroll_x / 400) % 640`), which shifts each mountain
  profile by a fraction of its period every 640 pixels. The port scrolls
  them continuously (and the far mountains have a 160-pixel period, so
  they repeat every 320 pixels, the tilemap's width).
