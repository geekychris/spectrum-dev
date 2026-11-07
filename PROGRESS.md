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

- **Miner Meteor** (48K): the original's rules (`game.cpp`) at 50 Hz on the
  half-size cavern (one 16-px tile per character cell), its six caverns
  through a level file; sprites composed per cell over the tiles (no flicker,
  per-cell colour), belts redrawn a few lines a cell, a quarter a frame; 46.5 fps
  over the demo (33.5 in cavern 6, 50 in the light ones); beeper effects a chunk
  a frame, AY tune on a 128K; a host-side search over `game_tick()` makes the
  demo and the test bot's routes; the bot feeds keys per logic tick from a
  breakpoint. SDK: `tmap` (tile maps), `plat` (platformer collision, the
  original's), `cellspr48`, `tools/levelpack.py`; tests in projects/sdktestroom.

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
- **Poor fit**: Lander 3D, ScAIry Fun, Boing Twist, Sphere Siege.

Suggested first steps: a small `sdk/` game layer (frame loop, input,
attribute/pixel sprite blitter for 48K; Layer 2 + hardware sprites for Next;
AY/beeper sound; 8.8 fixed point + lookup trig), then one 48K port (Nova
Defense or StakAttack) and one Next port (Boing Pinball) to shake it out.
