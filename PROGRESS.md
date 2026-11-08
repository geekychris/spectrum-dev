# Progress

Phase 1: infrastructure (emulator harness, toolchain, host tool + MCP, SDK,
docs) — **done**. Porting games is phase 2.

## Phase 1 (2026-11-06)

- [x] Evaluated ZEsarUX 13.0 ZRCP: registers, memory, breakpoints,
      membreakpoints, cpu-step, snapshots, smartload (.tap/.nex/.sna/.z80),
      set-machine, keyboard matrix + joystick injection (set-ui-io-ports),
      screenshots of the rendered frame (save-screen). Missing for agents:
      deterministic "run N frames" (its `run` counts opcodes, is paced by
      the host clock and polls the socket every opcode), a capturable guest
      log, run-until-log.
- [x] Patch (emulator/patches, emulator/ext/zxh.c): frame counter, no-wait
      deterministic mode, debug-log ring fed by ZXI register 1, `zxh-*`
      commands; macOS main-thread fixes for window sessions.
- [x] Native macOS arm64 build with Homebrew SDL2 (configure patch).
- [x] Toolchain image zx-dev-toolchain:1 = z88dk/z88dk:sha-1ead955 + sjasmplus 1.24.0.
- [x] Python host `zxdev`: Emulator, ZRCP client, symbols (.map/.sym),
      keys, sessions + registry, CLI, MCP server (42 tools, stdio + HTTP),
      DevBench (web UI + REST).
- [x] `./zx` CLI: setup, doctor, new, build, run, serve, test, log, shot,
      ctl, sessions, symbols, devbench, clean, selftest.
- [x] SDK: project.mk (z88dk C/asm for +zx/+zxn, sjasmplus mode), zx_log
      (C + asm macro), zx_print_at, template for 48K and Next.
- [x] Projects with tests: hello48 (48K .tap), hellonext (Next .nex,
      28 MHz, Layer 2), asm48 (sjasmplus .sna).
- [x] `./zx selftest`: 3 projects + template on both targets + 44
      remote/debugger/MCP/DevBench checks, all passing. Window mode checked
      (`ZX_TEST_WINDOW=1`, 6 checks, opens a real window).
- [x] Docs: README, CLAUDE.md, docs/emulator-harness.md, docs/devbench.md,
      emulator/patches/README.md, sdk/README.md; screenshots in docs/images.

## Phase 1b: required harness features (2026-11-06)

Repo: https://github.com/geekychris/spectrum-dev (private), pushed at each milestone.
Each item is exposed in the Python API (`zxdev.emu.Emulator`), the session
protocol / `./zx ctl` / DevBench REST, the CLI where it makes sense, and MCP,
and is checked by `tools/zxdev/tests/test_features.py` (82 checks) or
`test_remote.py` (sessions, MCP, DevBench; window checks with ZX_TEST_WINDOW=1).

- [x] Keys: single keys, CAPS/SYM combos, timed press/hold/release at given
      frames (`sequence` with `at`, CLI `--press/--hold/--release/--type K@F`,
      `--script`), typed text, Kempston / Sinclair 1 / Sinclair 2 / Cursor
      joysticks, Next extra keys (matrix combo + NextReg 0xB0/0xB1, patched in).
      Headless and window mode.
- [x] Debugging: PC/symbol/condition breakpoints, read/write watchpoints,
      stepi, step over, continue with a frame limit, all registers (alternates,
      IX/IY, I/R, IM, IFF), memory incl. 128K banks and Next 8K pages,
      memory_map, disassembly, stack backtrace, `last_stop` (where + why).
- [x] Screenshots: native composited PNG incl. border; ULA / Layer 2 / sprite
      layers rendered from memory at native size; GIF / MP4 / PNG-sequence
      recording.
- [x] Restart: soft reset, hard reset, power cycle, machine switch
      (48K/128K/Next), reload, reboot a wedged session (kills a stuck command).
- [x] Replay: input log with frame + T-state, checkpoints with screen hashes,
      save_replay / replay (`./zx replay`), mismatch detection; snapshots
      .zsf/.z80/.sna (Next: .zsf); screen_hash.
- [x] Logging: zx_log, run_until (stops in the logging routine), frame
      counting, run_to exact frame/T-state.

## Phase 1c (2026-11-06): review follow-ups

- [x] Repo private on GitHub, `main` pushed at each milestone; install.sh points at it.
- [x] Input recorder in the patch (`zxh-rec`): all input sources, including the user's
      keyboard/joystick in the window, at the instruction boundary where the core sees
      them; per-frame screen hashes (`zxh-hash`). `record_input` start/stop/save (from a
      snapshot or from reset), `replay` with per-frame verification, `./zx record`,
      `./zx replay --runs 2`, MCP `emu_record_input` / `emu_replay` / `emu_record_gif`.
      Tests: 48K + Next, from reset + from snapshot, replays after different histories
      give identical sequences equal to the recording, a time-shifted log is caught, a
      window session with user + agent keys replays headless frame for frame.
- [x] Deterministic loads: ZEsarUX's hard reset kept RAM/registers/MEMPTR/FLASH phase;
      `zxh-sync ram` + fixed registers (tested: identical frames whatever ran before).
- [x] Snapshot resume keeps the beam position (`.beam` sidecar, `zxh-tstates`).
- [x] Window fast-forward keeps host input live (turbo on the main thread, 1 in 8 frames
      presented, ~770 fps); typing during fast-forward is tested.
- [x] Halt reasons: `last_stop.why` = breakpoint / watchpoint / step / frame_limit /
      log_match / position, tested for each.
- [x] Next composite test covers border + ULA + tilemap + Layer 2 + hardware sprites.
- [x] "did not open ZRCP": not reproduced (60+ sequential, 40 parallel starts under load
      22). Ruled out: TIME_WAIT (ZEsarUX sets SO_REUSEADDR), outgoing connections on the
      port (tested, no effect), duplicate probe ports (macOS allocates sequentially).
      Hardened anyway: ports from 20000-29999, the host checks the emulator PID via
      zxh-status (a collision is detected and retried; tested), 3 start attempts, and
      the error now includes the emulator's log tail.
- [x] Toolchain image survives `docker image prune -a` (placeholder container);
      bin/zx-make rebuilds a missing image.

### Known limitations

- No GDB stub; the debugger is ZEsarUX's (via the session API). DeZog
  (VS Code) should be able to attach to a session's ZRCP port, untested.
- Window sessions are not deterministic, and their window freezes while an
  agent fast-forwards or steps them. Machine switches in a window restart the
  emulator.
- No audio capture/inspection yet (beeper/AY/Next DAC). Layer renders cover
  ULA, Layer 2 and sprites; not the Next tilemap, LoRes or Timex modes, and
  unified (non-composite) relative sprites use their own transform.
- No .szx snapshots (ZEsarUX can't write them); the Next only as .zsf.
- Recordings that start from a snapshot need the program to be running (no tape state in
  snapshots); use from_reset for programs still loading.
- In a window, fast-forward stops a few frames after its condition (exact in paused sessions).
- SD card/NextZXOS: sessions can boot an `.mmc` (see the arcade below); only
  ZEsarUX's NextZXOS 2.06 distribution has been used.
- Only tested on macOS arm64. The Linux path (scripts, configure flags) is
  written but not run.
- The repo is private, so the one-line installer only works for people with access.

## Phase 2 (2026-11-06): SDK game layer + ports

- SDK game layer (`sdk/include/zxgame.h`, `nextgfx.h`; tests `sdktest`, `sdktestnext`):
  8.8 fixed point, trig table, atan2, isqrt, xorshift RNG, IM 2 frame timer with a
  hook, keys/Sinclair/Kempston input with interrupt latching, beeper effects, AY
  effects and a small music player, 48K screen tables, pre-shifted sprite blits
  (xor/or/clear/store), text, attributes, high-score table and initials screen;
  Next: 28 MHz, double-buffered Layer 2, hardware sprites, palettes, TurboSound.
- **Nova Defense** (48K): renders 35-48 fps with 50 Hz logic.
- **Frank the Frog** (48K): logic at 50 Hz, screen ~19 fps (each lane every other
  redraw): moving objects step ~4 px. Lanes and objects in asm.
- **StakAttack** (Next): Layer 2 well with per-buffer cell cache, sprites for
  pieces, the original MOD converted to two AY chips, 50 Hz.
- Every game: title, keys + Kempston, sound, high scores, bot test that plays a
  whole game, replay x2 with frame hashes, screenshots, README, GIF in docs/images.
  All in `./zx selftest` (about 3 minutes in all).

- **Boing Pinball** (Next, stretch goal): the original core is 61K of zsdcc code
  in 16.16 and far too slow, so the tables are built on the host by the original
  code and exported, the physics is the original's ported to 16-bit units
  (statistically the same game over 200 bot games; Z80 and host builds
  bit-identical), the rules are the original's; table pre-rendered on 320x256
  Layer 2, balls/flippers as sprites, lamps by palette. Comet Tour only.
- SDK: next_init really selects 28 MHz under ZEsarUX; hiscore.c and nextgfx.c
  split so Next games don't link the 48K screen code or the Layer 2 drawing.

- **Spectral Keep** (Next): the original's rules, rooms and physics in 16-bit Q11
  (the same events as the original's Q12 code on the host for the test's game);
  rooms drawn on entry into 320x256 Layer 2 from pieces the original's voxel
  renderer makes on the host, a palette per room with a half-bright upper half
  for shadows; moving things as sprite groups masked behind scenery in the
  original's draw order; tilemap HUD; copper panel shade; the original's music
  and effects as TurboSound register streams; code in three banks. Logic 50 Hz,
  picture 50 fps about 80% of the time. No score in the original, so no table.
  SDK: l2rle, nextspr (masks in asm), isosort, nextcopper, nexttile, palfx,
  aystream, zxrle, dma_to_port; projects/nextlibtest.

- **Rock Blaster** (48K): the original's logic (ship in 16.16, the rest in
  16-bit 1/128 px, world 320 x 240 shown at 0.8), XOR wireframes straight onto
  the screen redrawn only where they moved, compiled polygons, the object
  loops in asm; logic 50 Hz, screen 12-40 fps (half the rocks a frame when
  busy); beeper effects, the original's MOD on a 128K's AY. SDK: `line48`
  (lines, wrapped polygons, compiled polygons, `poly_sync`, points),
  `vec48` (rotation tables), `hit48` (circle collisions), `text_xor`; an optional
  faster key decode (`input_fast`); `modplay` fixed on a single-AY 128K.
- **Lunar Rider** (Next): the original's game logic (16.16, banked over the
  ROM), on 320x256 Layer 2 in three parallax bands scrolled by the copper
  (each a ring of 320 columns: only the columns entering view are drawn),
  the far mountains on the tilemap with its own scroll, sprites (explosions
  from four mirrored quarters), the original's bass and effects on the
  TurboSound; high scores added. 50 fps, logic 50 Hz. An original bug fixed
  (the clear start area missed the buggy: ORIGINAL_BUGS.md). Test: a bot
  rides (reading `lunar_probe`), checkpoint, game over, initials, replays.
  SDK: `l2scroll.h`, tested in projects/scrolltest.
- **Rotoball** (Next): the original's rules, physics (16.16, as the
  original) and CPU; a two-player mode added (`input2.h`). The two views are
  a fixed 320x256 Layer 2 picture moved by palette rotation (sideways) and
  copper palette switches per row (down the pitch); ball, skimmer and posts
  are sprites in host-made sizes, hardware zoom for the big ones; the
  copper gives each pane its own sprite clip window (`nextsplit.h`).
  Tilemap HUD, TurboSound title tune and the original's effects. 50 fps,
  no frame missed in the test (bot match to full time, two-player match,
  replays). SDK: input2, nextsplit, sprg_show_ex/sprg_hide,
  palfx_load_bytes, copper lists by DMA; tests in projects/splittest.
- **Sky Knights** (48K): the original's rules in its 320 x 256 world with 1/64-pixel
  16-bit fixed point, drawn at 4/5 x 3/4; one or two players (input2). zsdcc's C
  was 5-10x too slow for the per-frame work, so physics, the enemy/pod/joust loops,
  the sprite pass and the attribute colouring are assembly calling the original C
  for rare events; the test plays the bot's recorded ticks on a build with the C
  loops (PHYS_IN_C) and requires the same game. Logic 50 Hz from a 64-frame input
  ring sampled by the interrupt; screen 48-50 fps at wave 1, 15-18 with eight
  enemies. SDK: sprclip (signed-x XOR sprites, sdktest checks).
- **Block Breaker 3D** (Next): the original's rules (bb_core.c) with only the
  16-bit int changes; its own 42 tests, including the 20,000-tick autopilot
  hash, pass on the host and inside the game. The tunnel is a 320x256 Layer 2
  picture from the original's camera and palette, cut into depth bands whose
  palette entries make the depth ring; the bricks are drawn into Layer 2 where
  they change (l2wpoly) with a depth plane; the ball, shadow, capsules, bits
  and paddle are 4-bit sprites, masked by the depth plane behind bricks;
  tilemap HUD; effects and an original tune on TurboSound; code in two banks
  at 0x0000 and two pages at 0x6000. Logic 25 Hz, picture ~49.6 fps. Port:
  the camera doesn't lean with the paddle. SDK: l2wpoly, nextsprl,
  tm_glyphnx. Two zsdcc miscompiles found (CLAUDE.md).

- **Miner Meteor** (48K): the original's rules (`game.cpp`) at 50 Hz on the
  half-size cavern (one 16-px tile per character cell), its six caverns
  through a level file; sprites composed per cell over the tiles (no flicker,
  per-cell colour), belts redrawn a few lines a cell, a quarter a frame; 46.5 fps
  over the demo (33.5 in cavern 6, 50 in the light ones); beeper effects a chunk
  a frame, AY tune on a 128K; a host-side search over `game_tick()` makes the
  demo and the test bot's routes; the bot feeds keys per logic tick from a
  breakpoint. SDK: `tmap` (tile maps), `plat` (platformer collision, the
  original's), `cellspr48`, `tools/levelpack.py`; tests in projects/sdktestroom.
- **Planet Chomp** (Next): the original's rules and maze carving with 16-bit
  vectors (sdk vec3), bit for bit the original's (host comparison over 300k
  ticks, the level-1 maze checked on the Next); the sphere drawn in real time
  on 256x192 Layer 2: a Q7 camera matrix patched into a Z80N MUL transform of
  the lattice corners, walls as quads from their corners, crumbs and walls in
  depth buckets, a clipped quad filler (draw.asm); sprites from the original's
  textures at 10 sizes; tilemap HUD and radar; TurboSound effects and a title
  tune; high-score table. Logic 50 Hz, picture ~9 fps after a speed pass
  (96 rows shown doubled by the copper, cull beyond ~70 deg, thin far walls
  as one quad, the disc in a cached background; 12 not reached: the 50 Hz
  rules ~330 lines a drawn frame, the lists walk every corner/cell/wall).
  SDK: vec3, isqrt32 3x.

- **Tank Duel** (Next): the original's amiga3d rules (16.16, its AI and
  autopilot) on the Z80N in two banks, its 9000-tick autopilot fight ending
  in the original's state hash; free first-person 3D on Layer 2: the
  original's models from the host, model-space culling, transform /
  projection / vertices in `MUL` asm, painter's order by object, copper line
  doubling and sky, LOD for far tanks, 1-pixel grid (sdk `l2line`); tilemap
  HUD, radar sprites, TurboSound effects, high scores. 5-11 fps (about 8).
  SDK: `l2line`, `muldiv16`.

- **Boing Twist** (Next): the original's puzzle core (`bt_core.c`; one
  expression rewritten around a zsdcc miscompile of `h ^ slot.home`) runs
  the original's self-test to its tests/vectors.h hash on the Next; the
  textured, lit ball from per-pixel view tables made on the host from the
  original's camera and light (new sdk `texview`: Layer 2 write paging, an
  unrolled 68 T-state loop; test projects/texviewtest), the pieces put
  together into the texture in asm rows, twists by DMA row turns, flips from
  a second texture laid out round the flip axis; all 17 pictures and 10
  rooms, copper line doubling, tilemap text, the original's effects and a
  tune on TurboSound, high scores. Logic 50 Hz, 30-37 fps while moving, 42
  on the title; a bot solves by the keys in the test.
- **Lander 3D** (Next): the original's amiga3d rules on fix16.h, bit for bit
  (18 flights, the autopilot's and a pilot's on every world, hash-checked
  against the original's code on the host; three of them in the game's
  self-test on the Z80N). The levels (ground, pads, start, the probe's tank)
  are made at build time by the original's own generator: 30 levels in
  816K of banks, 3-byte heights read through slot 2. The view: the camera
  never turns, so the ground's strips are drawn near to far by columns
  (each column's highest row kept: every pixel once, hills hide what is
  behind them); the grid projected in asm (Z80N MUL, barrel shifts, a
  reciprocal table); pads, shadow and lander over it, clipped by snapshots
  of the columns; Layer 2 at half height by copper doubling. Tilemap HUD,
  sprite big text in the original's 16x16 font, the original's MOD through
  modplay, high scores. Logic 25 Hz (fixed timestep). SDK: fix16 `fatan2`/
  `fasin`/`a3_atan2`/`fhypot3`/`fexp_neg`, a faster `fmul` (by operand
  size). zsdcc dropped a term of `v |= b & ~w` on volatiles.
  Speed pass (2026-11-08): the picture from 4-9 fps to 12-14 high up, 13-14
  behind the title, 9-11 coming down, 7.5-8 low over a pad (camera close),
  10-16 falling: a frame profiler by raster line (`PROF=1`,
  `tools/frame_prof.py`); beyond the 6 nearest rows the ground uses every
  other grid line; the lander's corners by the perspective's first-order
  map; pads off the view culled; the step's attitude and integration in
  Z80N asm (66K to 43K T-states a step, still the original's hashes), the
  autopilot every 32 steps (`tools/ap_rate.c`: all 30 levels still land),
  kept answers for repeated inputs. Low flight is held by the fills of the
  near cells and pads and by the rules (~20% of the CPU). SDK: `a3_sin`/
  `a3_cos` in Z80N asm (`next/fix16_sin.asm`, fix16test at all angles).

- **Rolling Steel** (Next, 2 MB): the 3DO version's physics (phys.c: a
  spinning sphere against ~2,000 course triangles, Coulomb friction, PhysX's
  rules) in Z80N asm with its 32-bit arithmetic exact, game.c under zsdcc;
  the attract demos of all six courses hash the same as the original's C on the
  host at every change of state (test.py). Courses drawn on the host from
  the original's geometry, lighting and fog at its default view, scrolled as
  a 320x256 Layer 2 ring (pixel-exact); crushers, sweeper arms, crumbling
  tiles, balls and debris as depth-sorted 4-bit sprites; tilemap HUD with the
  copper switching to a darkened second palette; the original's themes and
  effects as TurboSound streams; SDK high scores. Exact shortcuts (an
  odometer-based triangle cache, 16-bit fast paths) took a step from ~4 to
  ~1 frame. Two modes: play mode (default) steps the physics at 25 Hz with
  every per-step amount doubled (phys_dt2), ~91% of real time at ~4 fps;
  exact mode (X on the title) is the original's 50 Hz rules, ~63% of real
  time at ~7.5 fps. Separate high-score tables per mode.

- **Orbit Docking** (Next): the original's amiga3d rules on fix16.h, bit for
  bit (the Next flies the original's long autopilot game to its
  tests/vectors.h hash; host check over every station); the cockpit view
  in real time on 256x192 Layer 2: the camera only rolls about the axis, so
  each plane of the station is a turned and scaled shape (vproj.asm, four
  MULs a corner), l2poly in the original's depth-bucket order, half height
  by copper doubling; instruments as sprites (the dial redrawn into its
  patterns), tilemap text, the original's 16x16 font for big text; effects
  from its synthesis settings, a title tune, high scores. Logic 25 Hz,
  picture 14-20 fps. zsdcc miscompiled four of the original's expressions
  (a multiply with a call as an operand returned its low 16 bits, the debris
  test's chain of 32-bit compares): rewritten, found tick by tick.

- **Marble Tilt** (Next): the original's amiga3d rules unchanged on 16.16
  (`fix16.h`, the engine's fixed point bit for bit: the original's whole
  autopilot game hashes as in its tests/vectors.h, on the Next and the host);
  the 3D board from the original's camera and geometry projected on the host
  for each of the 11 x 11 tilt steps, filled on 256x192 Layer 2 (`l2poly`;
  its own `l2fill` measured no faster and was dropped), cached in spare banks and shown by
  NextReg 0x12, new ones drawn in each frame's spare time; the marble a
  sprite cut by the board's row-coded colours, its shadow from a half-bright
  sprite palette; tilemap HUD, sprite-zoomed big text, copper sky; effects
  from the original's synthesis settings + a port tune on TurboSound; high
  scores. Loop 50 fps, rules 25 ticks/s, boards 6-8/s while tilting. SDK:
  fix16, isqrt32 fix, spr_pattern_dma; projects/fix16test.

- **Bullion Dash** (48K): the original's rules (player, guards, digging,
  regrowing bricks, gold stealing, hidden ladders) at 50 Hz in the original's
  10 x 14 coordinates, one 8 x 8 cell a tile through cellspr48; its 150 levels
  arithmetic-coded into 7.9 KB; the guards' BFS as one cached distance field
  from the player (same first step); the level editor (S / L keep a level in
  memory instead of files). 24-50 fps. The original's levels can't be won (no
  route to all the gold and row 0 on 149 of 150; guards 1.67x the player's
  speed; ORIGINAL_BUGS.md): the test
  clears an editor-built level (dig a trap, gold, climb) and checks every game
  loop against a host build of the rules. SDK: `gridpack` (+ tools/gridpack.py),
  `gridbfs`; tests in projects/sdktestgrid.

- **Jump Quest** (48K): the original's logic (`player.c`, `enemy.c`, `items.c`,
  `level.c`, the state machine) at 50 Hz; two of its bugs fixed (a 14-pixel jump
  that can't pass level 1's pipes: -180 instead of -78, found by a host-side
  beam search over `jq_tick`; score and lives reset every life). 16-pixel tiles
  as 2x2 cells, a 16x12-tile view that jumps half a screen, sprites per cell
  (cellspr48), items as tiles; graphics converted from the original's own
  drawing code built on the host. ~7 fps (9 while running): the logic's hot
  loops are asm and still take ~30K T-states a tick. A zsdcc miscompile of a
  struct-field `&&` chain was caught by comparing the Spectrum with the host
  build tick by tick. SDK: cellspr48 `cs_scroll` / `cs_put_tile` /
  `cs_wipe_sprites`, 16 sprite slots; `spr16_shift`.

- **Warden Keep** (Next): the original's rules (`game.cpp` in C) in its one
  8x8 room; the room on 320x256 Layer 2 drawn on the host from the
  original's iso geometry, shaded for 256 colours (its hourglass-shaped
  diamonds drawn as diamonds: ORIGINAL_BUGS.md); barrels, jewels and the
  player as sprite groups numbered in its painter's order; the original's
  auto-repeat; port: a 60 s clock (the original can't be lost) with a time
  bonus, high scores, a tune and effects (it has no sound). 50 fps.
- **Emberkeep** (Next): the original's rules at 50 Hz; Layer 2 320 x 256 holds
  each chamber, drawn once from the original's tile sheet in 256 colours
  (160 used); player and guards are hardware sprites; the lantern is the
  Layer 2 and sprite clip windows rewritten by the copper on its corner lines
  (no drawing); tilemap HUD; TurboSound effects and tunes (the original's
  audio is a stub). 49.6 fps over a whole game. The test bot plans on a host
  build of the rules and each loop is checked against it.

### Cleanup (2026-11-06)

SDK modules pulled out of the games, each with a test (sdktest,
sdktestnext, sdk/tools/test_tools.py): `font5` + `l2_text5`, the initials
editor `hs_edit_*` with latched letters (lost initials fixed in every
game), Layer 2 320x256 helpers `l2w_*`, `spr_show_quad`,
`spr_patterns_from_pages`, `pal_load`, `modplay` + `tools/mod2ay.py`,
`fixmath.h`, `tools/nextpic.py`, zxnDMA (`dma.c`; StakAttack changes
screens by DMA). "Next features used" in StakAttack's and Boing Pinball's
READMEs; docs/48k-vs-next.md.

### Uranus Lander (2026-11-06)

- [x] 48K port of amiga_mcp/examples/uranus_lander: the original's terrain
      generator, 16.16 physics, landing rules, scoring and levels in a
      320x256 world drawn scaled to 256x192; the ST port's autopilot moved
      into the game (Z); ~26 fps while flying, logic 50 Hz; beeper / AY.
      test.py: the autopilot lands twice, crashes x3, game over, initials,
      replays x2; terrain checked against the original's generator.
- [x] SDK: `bitline` (lines into the screen or any buffer through a row table,
      clipped to a box, fast unclipped path; line48 is screen-only), `dots48`, `hmap48` (heightmap terrain), `gauge48`, `attrtint` (tint
      open-sky cells under moving objects), `spr32_shift`, fixmath
      `fix_sin14` / `muls` / `rot2d`; tests in `projects/sdktest48gfx`. project.mk also checks
      the code end and a BSS moved below the code. Fixed: latched input reported a
      press twice when input_read saw it before the interrupt's sample (initials
      "BOT" -> "BBO", the arcade's S toggled twice). Uranus Lander is in the arcade.

### Known limitations (games)

- Uranus Lander: a new level's screen takes ~0.5 s to draw (the game waits);
  frames where the ship turns cost ~2x (rotation + lines, ~70K T-states).

- Frank's redraw stays ~19 fps: the floor is ~5K T-states per lane object
  (about 17 a frame); moving draw_lane's loop to asm would give ~20 fps.
- Bullion Dash: 24-30 fps where guards with no path wander (the original's
  32-bit LCG per random step, `input_read` ~15% of a frame); a new level's
  first frame takes ~0.3 s (unpack, path flags, whole-room draw).
- Jump Quest draws ~7 fps (logic 50 Hz): a tick of the original's logic is
  ~30K T-states (13-17 enemies, the player's probes and box tests), and each
  frame composes the sprites' cells. Ideas: the player's update in asm, enemies
  far off-screen fast-forwarded (they move deterministically), a 128K version
  with pre-shifted sprites.
- Miner Meteor's caverns 4 and 6 draw 33-36 fps: three guardians and the miner
  move every frame (~10 cells composed, ~2,500 T-states each), the logic is
  ~14K T-states a tick in C and `input_read` ~15K (with `input_fast`).

## Arcade (2026-11-06)

- [x] `./zx arcade`: every project with an `arcade.json` + the menu (projects/arcade) on a
      32 MB SD image with NextZXOS (ZEsarUX's tbblue.mmc, trimmed), opened in a window.
- [x] Menu: machine code that autoexec.bas loads and runs with USR; Layer 2 320x256 list +
      picture + description, A-Z / most played (stats.dat on the card), keys + Kempston.
      Returns the choice to BASIC, which starts it like NextZXOS's browser: `.nexload` for
      .nex, `SPECTRUM` for 48K games (the .tap snapshotted on the host at its USR). Reset
      returns to the menu on the last game.
- [x] Host: `sdk/tools/fatimg.py` (FAT16 images in Python), `nextbasic.py` (tokeniser,
      byte-identical to NextZXOS's), `zxdev/arcade.py`; Emulator(sd=...), `.mmc` sessions,
      `--sd-scratch`.
- [x] SDK: `sdfile.h` (esxDOS), `listui.h`, `l2w_text`, `frame_exit`, `OUTPUT := bin`.
- [x] MCP `arcade_games/picture/build/play`, DevBench `/arcade` + REST, docs/arcade.md
      generated from the manifests.
- [x] Test: every registered game started from the menu and back by reset, picture bytes,
      sort, persistence, BREAK; host tool tests; remote (MCP + REST play).

## ScAIry Fun (2026-11-06)

- [x] Next port of the self-driving car game (rated a poor fit in the survey): the
      original simulation narrowed to 16 bits where it can be, banked, hot loops in asm;
      `tools/check_trace.sh`: all four routes at 50% and 100% traffic bit-identical to the
      original built on the host.
- [x] Renderer: the original's segments and cross-sections in pseudo-3D on Layer 2 (sdk
      `l2poly` bands/polygons, `persp`), drawn 80 rows high and doubled by the copper, copper
      sky; vehicles as sprites pre-drawn from the original's models; tilemap HUD and screens;
      the MOD on TurboSound; high scores.
- [x] test.py: a bot plays route 1 (two saves, the embankment, the tow truck, the
      cockpit view), arrives, Rush Hour to game over, initials; replays x2.
- [x] Speed pass (2026-11-07): 5.1 to 6.2-7.5 fps in play (the view 3.66M to ~2.4M T-states):
      the cross-section projected per edge in one batch (`persp_xs`/`persp_ys`), the skyline
      as a DMA-copied panorama, segments in pairs from 6 and fours from 14, lines to 16,
      sprites/scenery/AI view cheaper, DMA set-up by one OTIR (`fastdma.c`). SDK: faster
      `pmul`, `persp_ys`; the link fails on a BANK_n/PAGE_n overflow.
- [ ] 12.5 fps would need the view at ~1.2M T-states: a different renderer (rows front to
      back, all asm); the simulation alone is 21% of the CPU at 25 Hz.

## Game candidates (survey)


Which existing games fit (survey of amiga_mcp/examples, atari_st_games,
3do_dev/projects; the ST and 3DO ports reused the Amiga game logic through
a compatibility layer — the Spectrum needs more rewriting: 1-bit + attribute
graphics on 48K, no FPU, 16.16 fixed point is slow on a Z80):

- **48K fit**: Rock Blaster, Nova Defense, Dot Chase, StakAttack, Frank the
  Frog, Bullion Dash, Miner Meteor, Warden Keep, Uranus Lander, Sky Knights.
- **48K with simplification**: Orb Hunter (flip-screen), Jump Quest and
  Lunar Rider (character/strip scrolling), Pea Shooter Blast, Orbital Patrol
  (fewer objects), RJ Birthday (no MODs), Emberkeep (attribute light radius),
  Spectral Keep (Knight Lore-style mono iso), Ace Pilot, Void Trader, Tank
  Duel (wireframe).
- **Next**: Rolling Steel, Planet Chomp, Rotoball, Canyon Rescue, Block
  Breaker 3D, Marble Tilt, Harbour Crane, Orbit Docking, Stack Tower, Boing
  Pinball, Terra Strike, Ridge Runner, Drone Courier, Canyon Rail, Ace Pilot 2.
- **Poor fit**: Lander 3D, ScAIry Fun (done anyway: ~6.5 fps), Boing Twist (done anyway: host-made view tables, 30+ fps), Sphere Siege.

Suggested first steps: a small `sdk/` game layer (frame loop, input,
attribute/pixel sprite blitter for 48K; Layer 2 + hardware sprites for Next;
AY/beeper sound; 8.8 fixed point + lookup trig), then one 48K port (Nova
Defense or StakAttack) and one Next port (Boing Pinball) to shake it out.
