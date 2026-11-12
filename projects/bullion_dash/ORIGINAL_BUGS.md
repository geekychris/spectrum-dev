# Bullion Dash: problems in the original

Found while porting `amiga_mcp/examples/bullion_dash` to the Spectrum. The
Atari ST (`atari_st_games/bullion_dash`) and 3DO (`3do_dev/projects/bullion_dash`)
versions use the same `player.c`, `enemy.c` and `level.c` (no differences in
the rules) and the same `level_data[]`, so everything below applies to all
three. The Spectrum port keeps the rules as they are; only the two small
items at the end are changed there (marked `port:`).

**The levels are now repaired** in every version (section 1, "Repaired").
The analysis below is of the levels as released (`src/levels_original.txt`).

## 1. The levels can't be won

A level is won when all its gold is collected and the player is on row 0
(`main.c`: `gold_collected >= gold_total && player.gy == 0`). Hidden ladders
appear once the gold is in.

### Routes (no guards)

Method: `tools/level_report.py` reads the 150 levels (`src/levels.txt`,
extracted byte for byte from `level.c` by `tools/extract_levels.py`). It
explores every cell the player can get to from the start, with a movement
model that is generous to the player:

- walking with support or on a bar or ladder, climbing ladders, falling (`player.c`'s rules);
- digging a brick below-left or below-right and dropping into it;
- holes that never close, and no guards.

| | levels |
|---|---|
| no ladder (or hidden ladder) top leading into row 0 at all | 4 |
| row 0 reachable from the start (hidden ladders shown) | 48 |
| every gold reachable from the start | 3 |
| both | 1 (level 24) |

So 149 of the 150 levels can't be won even without guards. In level 1, for
example, the only ladder up the right side ends at row 2, and the gold on top
of the brick platforms can't be reached.

Two rules make the routes worse than the maps suggest:

- **Up only on a ladder.** Moving up needs the player's own cell to be a
  ladder (`is_on_ladder(gx, gy)`), so a ladder that ends at row 1 leaves the
  player standing on row 1, one cell short.
- **No dropping from a bar.** Pressing down on a bar sets `PS_FALLING`, but
  the next frame's gravity check skips a player on a bar (`!is_on_bar(...)`),
  so the player stays where he is (`player.c`, "Release from bar").

### Guard speed (the original's; fixed: "Guards: fixed" below)

- Player: `PLAYER_SPEED` 2 pixels a frame on 10 x 14 tiles, so 5 frames a
  tile across and 7 up and down.
- Guards: a whole tile every third frame (`move_timer < PLAYER_SPEED + 1`
  returns), which is 1.67x the player's speed across and 2.3x on ladders.
  They fall at 1 pixel a frame (14 frames a row).
- The player always starts at (0, 14). In 111 of the 150 levels a guard is on
  the same row with open floor between them (median 16 tiles away), and walks
  straight in. With nothing to dig in the solid bottom row, nothing can
  stop it.

In level 24, the one level whose routes exist, the guard at (19, 14) reaches
the start in 57 frames. The player's only way out is along that same floor.

### A bot that plays with the real rules

Method: `tools/try_levels.py` plays every level on the host with the test bot
(`tools/bot.py`). The bot runs the game's own C (`player.c`, `enemy.c`,
`level.c`, `play.c`, built by `tools/hostsim.c`). At each cell it tries every
move (step, climb, dig and drop, dig a trap, wait) followed by every second
move, then heads for the gold for 90 frames. It keeps the move that collects
the most gold soonest without getting caught. It is given 6,000 frames
(2 minutes) a level.

| | levels |
|---|---|
| finished | 0 |
| caught by a guard | 138 (half within 69 frames, 87 within 2 s) |
| still alive after 2 minutes (guards can't reach; gold out of reach) | 12 |
| at least one gold collected | 88 |

On a level built in the editor (`test.py`) the same bot does trap a guard,
take the gold and climb out. So the bot isn't the reason these levels fail.

### Likely causes

The levels look generated (the player always starts at (0, 14), a solid
bottom row, platforms and ladders placed without checking for a route). The
guards' timer looks like a frame counter that was meant to be larger, for
example a step every 3 *pixels*, or `PLAYER_SPEED + 1` frames between pixel
steps rather than between tile jumps.

### Possible fixes (for the originals)

- Check each level in the generator with a route search like
  `tools/level_report.py`, guards included, and regenerate the ones that fail.
- Slow the guards: move them by pixels like the player, at
  `ENEMY_SPEED` 1 pixel a frame (10 frames a tile), or keep the tile jumps
  but every 6-8 frames.
- Let the player climb out of the top of a ladder into row 0 (or treat
  reaching row 1 above a ladder as the exit), and let down release a bar.
- Start the guards a second or two after the player, or away from his row.

### Repaired

All versions now use repaired levels:
- **The Amiga original:** `bullion_dash/level.c` in geekychris/amiga_games.
- **This port:** `src/levels.txt`.

The original's `tools/repair_levels.py` made them, using the route search
described above with digging left out (a dug brick grows back) and
guards ignored. Each fix is the smallest edit that works, and every level
keeps its layout:
- a new or longer ladder, through space, a brick or a bar's cell;
- a hidden ladder up to row 1 for the way out;
- 1042 cells in all, 6.9 a level, listed in
  [REPAIRED_LEVELS.md](REPAIRED_LEVELS.md).

Results:
- `tools/level_report.py` now reports all 150 levels as winnable.
- `tools/check_levels.py` plays every level to the exit with the port's
  rules built for the host (guards out): 150 of 150 won.
- The guards are fixed too (below).

### Guards: fixed

The guards now follow a new rule, the same in every version:
- **Speed:** a tile every 8 frames across and 10 up or down, about 0.6-0.7x
  the player's speed (he takes 5 and 7).
- **Start:** every guard waits 3 s at the start of a level, and one placed
  on the player's start row enters from the top instead, as a respawned
  guard does.
- **Stolen gold:** a guard drops it again after 4 s, on the first free cell
  with a floor that it stands on. In the original it kept the gold until
  it was trapped or died.

`tools/try_levels.py` measures this on the repaired levels with this
port's rules. Its bot now takes the gold in the route search's order and
looks 150 frames ahead:

| | cleared | caught | out of time |
|---|---|---|---|
| no guards (the bot's own limit) | 124 | 0 | 26 |
| the original's guards | 3 | 139 | 8 |
| guards at 6/8 frames a tile, nothing else | 8 | 89 | 53 |
| 8/10 frames, the 3 s start, start-row guards from the top | 43 | 56 | 51 |
| the fix (the above, and stolen gold dropped after 4 s) | 66 | 56 | 28 |

The fix gives 62-74 cleared across runs, depending on the guards' random
numbers. Speed alone wasn't enough, for two reasons:
- **The start:** the player starts in a corner on a floor that can't be
  dug, and on many levels a guard reaches the only way out first.
- **Stolen gold:** a guard that keeps its gold leaves the level unfinishable
  until it is trapped.

## 2. A player buried by a brick freezes

`level_tick_bricks` sets `player.state = PS_DEAD` when a brick grows back on
the player, and the player also gets `PS_DEAD` after falling off the bottom
of the grid. But `main.c` only ends a life when a guard touches the player
(`enemy_check_collision`). A dead player doesn't move (`player_update`
returns at once), so the game stands there until a guard arrives, and on a
level whose guards can't reach him it never ends. The Spectrum port ends the
life on `PS_DEAD` (`src/play.c`, `port:`).

## 3. The sound effects are never played

`sound.c` builds samples for digging, falling, gold, death, level complete,
a trapped guard and footsteps, but nothing calls `sound_dig()` and the
others. Only the MOD tune plays. The Spectrum port plays dig, gold, trap,
death, level complete and game over (beeper, AY on a 128K).
