# The 48K Spectrum and the Spectrum Next as targets

Both build from the same C (`TARGET := zx` or `zxn` in a project's
Makefile) with the same SDK (`sdk/`). What differs, and what it meant for
the games in `projects/`:

| | 48K / 128K Spectrum | Spectrum Next |
|---|---|---|
| CPU | Z80, 3.5 MHz (69,888 T-states a frame) | Z80N, up to 28 MHz (~560,000 a frame); extra instructions (`MUL`, `NEXTREG`, `LDIRX`...) |
| memory | 48K (128K: banks at 0xC000) | 1-2 MB in 8K pages, any page in any of the eight 8K slots (MMU) |
| graphics | 256 x 192, 1 bit a pixel, 2 colours per 8 x 8 cell (attributes) | the ULA screen, plus Layer 2 (256 x 192, 320 x 256 or 640 x 256, up to 8 bits a pixel), a tilemap, 128 hardware sprites, 9-bit palettes, the copper |
| sound | beeper; 128K: one AY (3 channels) | three AYs (TurboSound, 9 channels), DACs |
| other | | zxnDMA (memory copies at ~2 T-states a byte) |
| files | `.tap` (BASIC loader + code) | `.nex` (loads code and data into banks directly) |

## What the games did on each

**48K** (Nova Defense, Frank the Frog, Rock Blaster, Uranus Lander, Sky Knights): every pixel is drawn by the CPU, and
the CPU is slow, so the work goes into drawing less:

- sprites pre-shifted into 8 copies so a draw is byte moves, written in
  assembly where it mattered (`spr_blit.asm`, Nova's swarm rows, Frank's
  lane objects)
- colour from attributes: objects are laid out on 8-pixel rows so each lane
  or row has its own colours (Frank's lanes are two character rows)
- only what moved is redrawn (Nova's shields, Frank's trails, Sky Knights'
  knights), and Frank draws half the lanes each frame
- vector graphics as XOR wireframes (Rock Blaster, sdk `line48`): drawing a
  shape twice erases it, so each frame redraws only the shapes that moved,
  straight onto the screen; shapes are compiled polygons (each edge's octant
  and length worked out at start-up) and the ship has one for each of 128
  angles
- Sky Knights keeps the original's 320 x 256 world and 16-bit fixed point
  and only scales the drawing; its every-frame loops are assembly that
  calls the original C for rare events, checked against the C tick for tick
- tables built at start-up can go in the RAM below the program
  (0x5D00-0x7FFF once the BASIC loader is done), as Rock Blaster's and Sky
  Knights' do; it is contended, so code stays out of it (Sky Knights' C ran
  ~40% slower there)
- the game logic runs at a fixed 50 Hz while the screen updates when it can
  (Nova 35-48 fps, Frank about 19, Rock Blaster 12-40, Uranus Lander ~26,
  Sky Knights 50 down to 15 with eight enemies), so the games play at the
  original speed
- sound on the beeper (which stops the CPU), or on the AY on a 128K (Rock
  Blaster plays the original's MOD there through `modplay`)
- Uranus Lander draws its terrain once per level from a heightmap, renders
  its line-drawn ship into a small image only when it turns and shifts that
  image into place when it moves; moving things take the colour of the
  open-sky cells they cross, never of the terrain's. Its 31 KB of code
  leaves no room for the variables above 0x8000, so they go below (in
  contended memory: hot loops are in assembly)

**Next** (StakAttack, Boing Pinball, Spectral Keep, Rotoball): the hardware does what the CPU did on
the 48K:

- Layer 2 gives 8-bit colour anywhere, so the original art and colours
  survive; Boing's whole table is a 320 x 256 picture loaded into banks
- hardware sprites move everything that moves without touching the
  background
- palette entries animate lamps without drawing
- DMA copies whole screens (StakAttack changes screens without losing a
  frame)
- 28 MHz and `MUL` make Boing's 16-bit physics (120 steps a second) fit in
  half a frame; zsdcc's C is still the cost to watch
- MMU banking holds more code and data than a 64K address space: Boing runs
  its rules from a bank mapped over the ROM
- TurboSound plays a four-channel MOD and the effects at once
- Spectral Keep draws each isometric room once into Layer 2 and moves
  everything as sprites; a sprite behind scenery has the scenery's
  silhouette cut out of its patterns, in the original's draw order. Shadows
  are a palette trick (the upper half of the palette is the lower half
  darker), the panel's shade is the copper switching palettes, the HUD is
  the tilemap. Its code is in three banks the main loop maps in turn
- Rotoball's two perspective views are one fixed Layer 2 picture: each
  floor pixel holds its sideways phase, a camera's sideways movement is a
  rotation of its pane's palette, and its movement down the pitch is the
  copper switching floor rows to the second palette. The copper also gives
  each half of the split screen its own sprite clip window. The CPU draws
  no pixels; ball, skimmer and posts are sprites in many sizes, zoomed by
  the hardware when big

## Things to know

- z88dk's zsdcc makes large, slow code (Boing's original 16.16 core: 61K);
  keep hot loops simple or in assembly, and see CLAUDE.md for its bugs.
- On the 48K, games end below 0xF000 (the SDK's IM 2 table); on the Next,
  `src/main.c` of boing_pinball shows a banked memory map.
- ZEsarUX runs the Next at 28 MHz only after the SDK's `next_init()` (see
  CLAUDE.md); our tests check it.
- Tools: `sdk/tools/mod2ay.py` (MOD music for the AY), `sdk/tools/nextpic.py`
  (pictures to Layer 2 banks and sprite patterns, column RLE, sprite groups),
  `sdk/tools/aystream.py` (AY register streams), `sdk/tools/zxrle.py`.
