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

### Known limitations

- No GDB stub; the debugger is ZEsarUX's (via the session API). DeZog
  (VS Code) should be able to attach to a session's ZRCP port, untested.
- Window sessions are not deterministic, and their window freezes while an
  agent fast-forwards or steps them. Machine switches in a window restart the
  emulator.
- No audio capture/inspection yet (beeper/AY/Next DAC); no sprite/tilemap/
  copper inspection commands of our own (ZEsarUX has `tbblue-get-sprite`,
  `tbblue-get-register`, `tbblue-get-palette`, reachable via `zrcp`).
- No SD card/NextZXOS setup: .nex programs that use esxDOS/NextZXOS file
  APIs need `--mmc-file` with an SD image.
- 128K banking: `read_mem` reads the current 64K view; other banks need
  ZEsarUX memory zones (`zrcp line="set-memory-zone N"`).
- Only tested on macOS arm64. The Linux path (scripts, configure flags) is
  written but not run.
- The one-line installer points at a GitHub repo that does not exist yet.

## Phase 2 (next): game ports

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
