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

## Not bugs, but worth knowing

- A barrel can be pushed onto a jewel (`blocked()` only refuses walls and
  barrels). `object_at()` returns the first object in the list, and the
  barrels come before the jewels, so the jewel can't be taken until the
  barrel is pushed off again; both are drawn, the jewel over the barrel
  (same depth key, later in the list). The port keeps this.
