# Orb Hunter: bugs in the original

The original is `amiga_mcp/examples/orb_hunter` (classic Amiga and AmigaOS 4;
the 3DO port has the same code). The port keeps its rules exactly
(`tests/host_diff.c` checks every step against them) and changes only the
drawing below, marked `port:` in `src/draw.c`.

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

**Port:** enemies are always drawn (their timer has no "hit" meaning to
flash on), and the invulnerable hunter flickers on the frames shown
(`src/draw.c`, `draw_sprites`). The rules are untouched.

## Not a bug that matters: creatures inside the citadel's floor

The citadel rooms (row 4) have a floor three tiles thick (`levels.c`
`gen_citadel`: `add_shell(..., 3, 2)`), but their creatures are placed at
tile row 13 (`game.c` `room_enemies`), which is inside it. They stay there
and can't reach the hunter, nor be reached. The port keeps this as it is.
