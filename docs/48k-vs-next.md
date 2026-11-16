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

**48K** (Nova Defense, Frank the Frog, Rock Blaster, Uranus Lander, Sky Knights, Miner Meteor, Jump Quest, Dot Chase): every pixel is drawn by the CPU, and
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
- tile rooms (Miner Meteor, sdk `cellspr48`): the room is tile ids and a
  frame composes only the character cells something changed in: the tile,
  then each sprite ORed in, written once, so sprites don't flicker and each
  colours its own cells. Tiles are one cell each, so every tile keeps its
  colours (the cavern is drawn at half size to make that so)
- zsdcc's code through pointers to structs is many times the size of the
  same code on a static copy; the hot loops are asm, and `tools/profile.py`
  / `tools/timefn.py` in Miner Meteor show where a frame's time goes
- tables built at start-up can go in the RAM below the program
  (0x5D00-0x7FFF once the BASIC loader is done), as Rock Blaster's and Sky
  Knights' do; it is contended, so code stays out of it (Sky Knights' C ran
  ~40% slower there)
- Dot Chase keeps the maze in a shadow screen (`shadow48.h`) and, right
  after the interrupt, puts the sprites' old places back from it and draws
  them again top first, ahead of the beam: no flicker or tearing with
  sprites over a detailed background; its 31-row maze is drawn in 6-pixel
  tiles to fit 192 lines
- zsdcc's code through pointers is several times slower than with fixed
  addresses: Dot Chase keeps its game state at a fixed address and unrolls
  per-spider code
- the game logic runs at a fixed 50 Hz while the screen updates when it can
  (Nova 35-48 fps, Frank about 19, Rock Blaster 12-40, Uranus Lander ~26,
  Sky Knights 50 down to 15 with eight enemies, Miner Meteor 33-50, Jump
  Quest ~7: its logic alone takes ~40% of the CPU; Dot Chase 49.5), so the games play at the
  original speed
- scrolling: Jump Quest tried character-strip scrolling of a 32x23-cell tile
  view (cellspr48 `cs_scroll`) and measured ~130K T-states a 16-pixel step on
  a busy view, more than its frame; it jumps the view half a screen instead
  (`cs_put_tile` draws only the cells that change)
- sound on the beeper (which stops the CPU), or on the AY on a 128K (Rock
  Blaster plays the original's MOD there through `modplay`)
- Uranus Lander draws its terrain once per level from a heightmap, renders
  its line-drawn ship into a small image only when it turns and shifts that
  image into place when it moves; moving things take the colour of the
  open-sky cells they cross, never of the terrain's. Its 31 KB of code
  leaves no room for the variables above 0x8000, so they go below (in
  contended memory: hot loops are in assembly)

- Bullion Dash keeps the original's 10 x 14 pixel coordinates for its rules
  and draws one 8 x 8 cell a tile; 150 levels go into 8 KB by arithmetic
  coding (sdk `gridpack`), and its guards share one breadth-first distance
  field from the player instead of a search each (sdk `gridbfs`). Code, levels
  and variables take the whole RAM from 0x5D00, with tables made at start-up
  above the IM 2 table

**Next** (StakAttack, Boing Pinball, Spectral Keep, Rotoball, ScAIry Fun, Block Breaker 3D, Marble Tilt, Planet Chomp, Stack Tower, Warden Keep, Lunar Rider, Emberkeep, Tank Duel, Orb Hunter, Harbour Crane, Orbital Patrol, Ace Pilot, RJ's Birthday Bash, Drone Courier): the hardware does what the CPU did on
the 48K:

- Layer 2 gives 8-bit colour anywhere, so the original art and colours
  survive; Boing's whole table is a 320 x 256 picture loaded into banks
- hardware sprites move everything that moves without touching the
  background
- hardware scrolling: Orbital Patrol's 320 x 256 Layer 2 is a window on a
  1920-pixel world; the copper changes the scroll row by row so the scanner
  and HUD stay still, and only the columns that scroll in are drawn. Its
  stars are the tilemap, scrolled at a quarter of the speed behind Layer 2
- palette entries animate lamps without drawing
- DMA copies whole screens (StakAttack changes screens without losing a
  frame)
- 28 MHz and `MUL` make Boing's 16-bit physics (120 steps a second) fit in
  half a frame; zsdcc's C is still the cost to watch
- MMU banking holds more code and data than a 64K address space: Boing runs
  its rules from a bank mapped over the ROM
- TurboSound plays a four-channel MOD and the effects at once
- Planet Chomp draws a perspective 3D sphere every frame: `MUL` rotates the
  maze's lattice corners by a camera matrix patched into the code, Layer 2
  takes the polygons at half height (the copper doubles each row), DMA copies
  the starfield and the planet's disc in, sprites carry everything
  that moves; its rules are the original's 32-bit code on 16-bit vectors
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
- ScAIry Fun redraws a whole 3D road every frame, which the hardware can't do
  for it: a Z80N asm filler draws the original's road segments as bands and
  polygons, and the copper halves the work by showing each Layer 2 row twice.
  Even so it runs at ~6.5 frames a second (the original's 32-bit simulation,
  banked across three memory maps a frame, keeps its 25 steps). Vehicles are
  sprites streamed into pattern memory by DMA
- Block Breaker 3D keeps the original's 3D: its tunnel is a Layer 2 picture
  from the original's camera, its bricks are drawn into Layer 2 where they
  change, with a depth byte per pixel; sprites deeper than a brick face are
  masked against those depths, so the ball goes behind bricks. The ring
  showing the ball's depth is palette entries, one set per depth band of
  the walls. Its rules are bit-exact with the Amiga's (the original's tests
  run in the game)
- Marble Tilt keeps an amiga3d game's 16.16 rules exactly (`fix16.h`,
  `MUL` for the 64-bit products) and its live 3D board: the projection is
  done on the host for every tilt step, the Next only fills polygons, keeps
  finished boards in spare banks and shows one by pointing Layer 2's start
  bank (NextReg 0x12) at it; new boards are drawn in each frame's spare
  time so the marble and the rules never wait
- Stack Tower's 3D tower turns only about its axis, under a fixed camera, so
  everything the view shows is drawn on the host with the original's own
  geometry: the backdrop and core for 8 steps within a column, and every
  brick at each of 41 positions round the tower and each row (2,952 images
  in 1.3 MB of banks, drawn as base pen + offset so one image serves every
  colour). The board as drawn is kept in four pages and copied back by DMA
  when only the piece moved (50 fps); the drop guide is the copper changing
  the core panels' own palette entries between two lines
- Lunar Rider's parallax is the hardware's: Layer 2 (320 x 256) is three bands
  of rows, each scrolled by its own X offset that the copper sets at its top
  row, and each band redraws only the columns scrolling into view (1-3 a
  frame); the far mountains are the tilemap with its own scroll register,
  behind Layer 2's transparent sky. 50 fps with the original's 32-bit logic
  in a bank over the ROM
- Tank Duel is free-moving first-person 3D, so nothing can be drawn ahead:
  its models are the original's (built on the host by amiga3d's own rules),
  culled in their own frame before a vertex is touched, and turned,
  projected (a reciprocal table) and clipped in Z80N assembly; the view is
  drawn 96 rows high and shown 192 by the copper, which also paints the
  sky's gradient behind a transparent pen. About 8 fps: the hardest of the
  ports for a 28 MHz Z80 (its rules alone are a third of a frame a tick)
- Boing Twist's ball is a textured, lit sphere that turns, twists and flips:
  the host works out, for every pixel at each tilt, which texel it shows and
  how lit it is (sdk `texview`), so the Next only looks texels up (68
  T-states a pixel) and every motion is an add: a turn adds to the
  longitude, a twist turns the band's rows of the texture by DMA, a flip is
  drawn from a second texture laid out round its axis. Layer 2's write
  paging lets the renderer read the texture and write the screen at the same
  addresses; the copper doubles the rows. 30-37 fps while it moves

- Emberkeep's lantern costs no drawing: the light square is the clip window of
  Layer 2 and of the sprites, and the copper narrows it on the lines of its
  cut corners; outside it the screen is the fallback black and guards vanish
- Orb Hunter is flip-screen: a room is drawn into Layer 2 once when it is
  entered and everything that moves is a 4-bit sprite (nextsprl), so the
  original's 16.16 rules have the frame to themselves at 50 fps; they run
  from a 16K code bank mapped over the ROM, which the game doesn't need
- Drone Courier's chase camera turns freely, so its city is drawn in full
  every frame: only 96 rows of it, each shown twice by the copper (sdk
  `l2double`), the transform in 16-bit Z80N asm, polygons wholly on the
  screen filled by its own edge stepper into l2poly's span store, and the
  drone as pre-rendered sprites (10 of ~45 polygons gone); ~12 fps

- Harbour Crane fixes the original's camera so most of the picture never
  changes: a backdrop copied in by DMA each frame (its sea's drifting bands
  are palette entries), the crane's near side as sprites, and only the ship,
  her load and the swinging container drawn as polygons, in the original's
  depth order; its 16.16 rules run unchanged on fix16

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
