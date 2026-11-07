# Bugs in the original Jump Quest

Found while porting `amiga_mcp/examples/jump_quest` to the Spectrum. The same
code is in the Amiga, AmigaOS 4, Atari ST (`atari_st_games/jump_quest`) and 3DO
(`3do_dev/projects/jump_quest`) versions. Those repos are unchanged; this note
is for fixing them later.

## 1. The jump is too low to finish level 1

`game.h`: `GRAVITY 6`, `JUMP_FORCE (-78)`, in 10.6 fixed point. `player.c`
moves the player by `FP_INT(vy)` whole pixels a tick, so a jump rises
**14 pixels**. The levels need more:

- level 1: the pipes at columns 21, 40, 64 and 86 are two tiles (32 px) tall;
- level 2: the stone pillars at columns 25-26, 49-50, 72-73 and 95-96 are 32 px tall;
- level 3: after column 18 there's no floor. Its platforms alternate between
  rows 9 and 6, so each climb is 3 tiles (48 px) with gaps of up to 4 tiles.

No route gets past the first pipe (x = 336; the solver stops at 322). The ST port's level 2 screenshot
was taken by poking the player's x through its agent API.

**Evidence.** `tools/solve.c` is a beam search over the game's own logic
(`src/jq.c`, a line-for-line port of `player.c`, `enemy.c`, `items.c`,
`level.c` and `main.c`'s state machine), built for the host. Run as
`tools/solve.sh -q -DJUMP_FORCE='(-N)'`; furthest x reached per level:

| JUMP_FORCE | jump height | level 1 (1600 px) | level 2 (1920 px) | level 3 (2400 px) |
|---|---|---|---|---|
| -78 (the original) | 14 px | 322 | 406 | 189 |
| -120 | 26 px | 322 | 1528 | 213 |
| -140 | 36 px | cleared | cleared | 216 |
| -160 | 46 px | cleared | cleared | 868 |
| -166 | | cleared | cleared | 2152 |
| -172 | | cleared | cleared | **cleared** (smallest found) |
| -180 (the port) | 56 px | cleared | cleared | cleared |

**Fix.** `#define JUMP_FORCE (-180)` in `game.h`, nothing else. The Spectrum
port uses it (`src/jq.h`), and with it the three levels clear in a row without
losing a life (`src/demo.h`, `tools/routes.json`). -172 is the threshold, but
it leaves no margin (the solver's route ends level 3 with one heart). The stomp
bounce, `JUMP_FORCE / 2`, scales with it.

## 2. Every life and level starts with 0 points and 3 lives

`main.c` `start_level()` calls `player_init()`, which sets `score = 0`,
`lives = 3` and `health = 3`. The comment right after it says
"Preserve score and lives from previous level". The `STATE_DYING` code does
`p->lives--` and then `start_level()`, so lives go back to 3: **the game can
never end**, and the level-complete bonus (`p->score += 500` just before
`start_level`) is wiped too.

**Fix** (as in the Spectrum port's `src/jq.c` `start_level`): keep `score`,
`lives` and `health` around the `player_init()` call:

```c
int32_t score = p->score;  /* LONG on the Amiga */
BYTE lives = p->lives, health = p->health;
player_init(p, p->character, s->tx * TILE_SIZE, s->ty * TILE_SIZE - PLAYER_H + TILE_SIZE);
p->score = score;
p->lives = lives;
p->health = health;      /* the death code sets health = 3 before start_level */
```

## 3. Smaller things (left as they are in the port)

- `update_camera()` snaps `cam_x` down to a tile every tick, after moving it by
  `(target - cam_x + 3) / 4`. Any step under 16 px is undone, so the camera
  only moves once it is 61+ px behind its target, and at the level's right end
  it can stop a tile short of `max_x`. The 320-px screen hides this.
- `sound.c` starts each sound effect but never stops it: Paula repeats the
  sample (the ST port documents this too).
- `check_level_end()` uses `PLAYER_H` for Dale as well (his height is
  `PLAYER_H_SHORT`); harmless.
