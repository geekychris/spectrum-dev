# Lunar Rider: bugs in the original

The port keeps the original's rules (`amiga_mcp/examples/lunar_rider`).
Where a bug made a game unwinnable from its first second, the port fixed it
and marked the change `port:` in `src/game.c`.

**Fixed in the original** (geekychris/amiga_games #52, 2026-11-15) **and in
this port:**

| bug | the original now | this port |
|---|---|---|
| a game can start over a crater (below) | start area clear to `BUGGY_X + BUGGY_W + 30` | already fixed the same way |
| the title and GAME OVER took a held fire/jump as a press (one held from play skipped GAME OVER) | presses only | GAME OVER already took a new press; the title now does too (`main.c`) |
| a forward bullet's world x was a WORD: past 32768 px it tested the terrain 256 px away from the drawn rock | a LONG | a LONG (`game.c` `update_bullets`). A host build of `update_bullets` shoots a rock at world x 32815, 40115 and 200115 now; before, only below 32768 |
| the mountains' parallax wrapped at 640, not at its 512 period: they jumped (below) | wraps at 512 | the port scrolls them continuously (no jump) |
| mines 1 px wide, drawn on every other pair of columns only: often invisible | 2 px on every column | the port draws them 4 px wide |
| HI showed the current score | a session high score | the port shows its high-score table's best (or the score, if higher) |

The notes below describe the original as it was.

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

## Kept as the original (as it was)

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
