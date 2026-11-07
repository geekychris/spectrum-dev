# Bugs in the original (amiga_mcp/examples/warden_keep)

## Diamonds drawn as hourglasses

`render.cpp` `fill_top_diamond()` fills the top of every floor tile and
cube from the middle row outwards:

```c
for (LONG i = 0; i < half_h; i++) {
    LONG w = ((i + 1) * half_w) / half_h;
    RectFill(rp, cx - w, cy - i, cx + w - 1, cy - i);
    RectFill(rp, cx - w, cy + i, cx + w - 1, cy + i);
}
```

`w` grows with `i`, so the middle row is 2 pixels wide and the rows at
the top and bottom points are the full 32: an hourglass, not a diamond.
The outline drawn over it (`draw_floor_tile`, `outline_cube`) is a
diamond, so the floor and the cube tops show as spiky stars (the 3DO
port's screenshot `docs/images/games/warden_keep-play.png` in 3do_dev
shows it).

port: `tools/gen_gfx.py` `diamond_rows()` uses
`w = ((half_h - i) * half_w) / half_h`: full width at the middle row,
narrowing to the points. Nothing else about the shapes changes.

## Captions as black boxes

Already fixed in the original (its README, 2026-10-30: JAM1). The port's
first title had the same symptom for its own reason (black letters as
tilemap cells over a bar, whose background was transparent); its big
letters carry the bar's colour.

## A barrel over a jewel can be frozen there: the room can't be won

`blocked()` refuses walls and barrels only, so a barrel can be pushed
onto a jewel, and `object_at()` returns the barrel (the barrels come
before the jewels in the list), so the jewel can't be taken while it is
covered. Four barrels in a 2 x 2 block can never move again: along each
axis every barrel has another barrel either where the player would have
to stand or where it would have to go. So if the block covers a jewel the
room is lost, and the original (no clock, no restart key: only Esc
quits) can only be left by quitting.

Evidence: `tools/softlock.c` plays a 37-step route with the port's rules
(`src/wk_core.c`, which is `game.cpp`). The route takes the jewels at 2,6
and 6,3 and pushes the barrels to 5,5 6,5 5,6 6,6, over the jewel at 6,6.
It then searches every state reachable from there (45, the player's free
cells) and finds none that takes the last jewel:

```
cc -O2 -Isrc tools/softlock.c src/wk_core.c -o build/softlock && build/softlock
after the route (37 steps): player 6,4, jewels left 1, mode 1, barrels 6,6 6,5 5,6 5,5; jewel at 6,6
states reachable from there: 45; any with the last jewel taken: no
```

Route (cursor keys): `LUUULUURDDDD LDRRRR LUULDLDRR UUUURURDDD`.

port: the rules are kept. The game stays playable: in the timed game
(port:) the clock ends it, and in either mode BREAK goes back to the title
for a new room.

## Not bugs, but worth knowing

- A jewel under a barrel that can still move is drawn over the barrel
  (same depth key, later in the list), as in the original.
