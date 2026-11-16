# Orb Hunter: bugs in the original

The original is `amiga_mcp/examples/orb_hunter` (classic Amiga and AmigaOS 4;
the 3DO port has the same code). The port keeps its rules exactly
(`tests/host_diff.c` checks every step against them).

**Fixed in the original** (geekychris/amiga_games #50, 2026-11-15) **and in
this port** (the same behaviour, marked `fix` in the code):

| bug | the original now | this port |
|---|---|---|
| enemies never drawn, the invulnerable hunter hidden (below) | `gs.frame` goes up once a loop; enemies flash on their own 8-frame `hit_timer` | the same rules (`game.c`); drawing flashes on the shown frames (`draw.c`). Before, the port drew enemies always and never flashed them |
| the title, item box and game over took a held fire as a new press | presses only | the item box and game over: presses only (the title and game over already were) |
| the item box (90 frames) and game over (180 frames) waited for fire: `game_update`'s timers never ran there | they run | they run (`main.c`); game over goes on to the initials if the score makes the table, else to the title |
| the title said "START - MISSILE MODE" | "UP+FIRE - MISSILE" | the port's title already said "Q+SPACE - MISSILE" |
| the score was never shown (a debug line in the HUD) | `SC 000000` in the HUD, the score on GAME OVER | the same |
| the joystick's decode was wrong (right alone read 3) | the standard JOY1DAT decode | not applicable: the SDK reads the keys, Kempston and Sinclair |
| citadel creatures at row 13, inside the floor (below) | row 12 | row 12 |

`tests/host_diff.c` compares the port's rules with the fixed original's, the
hit timer included, and now also jumps both into random rooms so the citadel's
creatures are compared. `test.py` checks that the item box and game over go on
by themselves after 90 and 180 frames.

The notes below describe the original as it was.

## Enemies are never drawn (and the hunter vanishes while invulnerable)

The frame counter `gs.frame` goes up twice in each pass of the main loop,
and the drawing always sees it odd:

```c
/* main.c, the loop (state PLAYING) */
game_update(&gs, ...);          /* game.c:975   gs->frame++;   -> odd */
draw_frame(rp, &gs);            /* draw.c:1010  g_frame = gs->frame;   (odd) */
...
gs.frame++;                     /* main.c:750                   -> even */
```

The drawing then hides an enemy whose timer is running, on odd frames, meaning
to make it flash:

```c
/* draw.c:622 */
if (e->timer > 0 && (g_frame & 1)) continue;
```

but every enemy's timer counts up on every step (`game.c:592  e->timer++;`),
and `g_frame` is always odd there. So no enemy is ever drawn: the creatures,
the turrets and both bosses are invisible, only their shots show. The same
test hides the hunter for the whole of its invulnerability instead of making
it flicker (`draw.c:424`), for 1.2 seconds at the start of every game and
after every hit.

The parity only changes when the item screen (STATE_ITEM_GET, during which
the loop increments `gs.frame` without calling `game_update`) lasts an odd
number of frames, after which enemies are always shown and the hunter
flickers properly, until the next item screen.

**Port, before the fix:** enemies were always drawn (their timer has no "hit"
meaning to flash on), and the invulnerable hunter flickered on the frames
shown. **Now** (as in the fixed original): enemies flash while their own hit
timer runs, 8 frames after a beam or a bomb hits them.

## Creatures inside the citadel's floor (fixed)

The citadel rooms (row 4) have a floor three tiles thick (`levels.c`
`gen_citadel`: `add_shell(..., 3, 2)`), but their creatures are placed at
tile row 13 (`game.c` `room_enemies`), which is inside it. They stay there
and can't reach the hunter, nor be reached. Both the original and the port
now place them at row 12, on the floor.
