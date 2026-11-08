# ZX Spectrum / Spectrum Next development workspace

Homebrew for the ZX Spectrum 48K/128K (Z80 @ 3.5 MHz, 256x192 + 8x8
attributes) and the Spectrum Next (Z80N @ up to 28 MHz, Layer 2, sprites).
C and asm built with z88dk / sjasmplus in Docker; runs in a patched ZEsarUX
we can script deterministically.

## Commands

```sh
./zx build <project>        # -> projects/<p>/build/<p>.tap (TARGET zx) or .nex (zxn) + .map
./zx test <project>         # projects/<p>/test.py if present, else waits for ZX:DONE / ZX:FAIL
./zx log <project> --frames 600 [--press SPACE@400]
./zx shot <project> -o out.png --until "ready" [--press Q@400] [--no-border] [--scale 2] [--layer ula|layer2|sprites]
./zx video <project> -o out.gif|.mp4 --until "ready" --frames 150
./zx log <project> --press Q@200 --hold W@250 --release ALL@300 --type "hi@320" --record r.json
./zx record <project> -o r.json --press Q@200   # input recorder (all sources) + per-frame hashes
./zx replay r.json --runs 2 # re-run it twice: every frame hash must match, both runs identical
./zx run <project>          # window for the human (free-running; registered session)
./zx serve <project>        # headless paused session (prints ports)
./zx ctl <cmd> key=value    # remote-control the newest session (./zx ctl help)
./zx new <name> [--next]    # from sdk/template
./zx selftest               # build + test everything (do this before saying "done")
./zx doctor                 # environment problems
./zx devbench               # web UI + REST (/api) + MCP over HTTP (/mcp) on :3580
./zx arcade [--no-build]    # every game + menu on an SD card (NextZXOS), window; projects/arcade
```

Arcade: a game joins with `projects/<p>/arcade.json` (title, description ~100 chars,
controls, picture, ready = its title's log line); see projects/arcade/README.md.

`--press` frames and `--frames` count from the load; a 48K `.tap` takes
~140 frames to load and start, a `.nex`/`.sna` starts at once.

## MCP tools (server "zx", .mcp.json)

Inner loop: `build` -> `emu_boot(project)` -> `emu_run_until("<your log line>")` ->
`emu_look` / `emu_press` / `emu_log`. `emu_boot(p)` = private headless session
(paused: frames advance only on run/press/continue); `emu_boot(p, window=True)` =
user-visible, free-running; `emu_attach()` = join `./zx run X`. Also `emu_run(frames)`,
`emu_hold`/`emu_release`, `emu_type`, `emu_sequence`, `emu_joystick`, `emu_pause`/`emu_resume`,
`emu_load`, `emu_machine`, `emu_save_state`/`emu_load_state`, `emu_screenshot(path, layer)`.
Restart: `emu_reset(hard)`, `emu_reload`, `emu_power_cycle`, `emu_reboot` (wedged session).
Arcade: `arcade_games`, `arcade_picture(game)`, `arcade_build`, `arcade_play(game, window=True)`.
Screens: `emu_look(layer="screen"|"ula"|"layer2"|"sprites")`, `emu_record_video(path.gif|.mp4)`,
`emu_screen_hash`, `emu_record_gif`. Recording: `emu_record_input("start"|"stop"|"save", path,
from_reset)` (captures your input AND the user's in the window) + `emu_replay(path)` (per-frame
hash check). Also `emu_checkpoint`, `emu_save_replay`, `emu_input_log`, `emu_position`, `emu_run_to`.

Debugging: `emu_break("func")` or `emu_break(condition="A=5 AND PC=32768")`, then
`emu_continue(max_frames)`; `emu_watch("global", 2, "w")`, `emu_stepi`, `emu_step_over`,
`emu_regs`, `emu_set_reg`, `emu_disasm`, `emu_symbols`, `emu_read_mem("sym")`,
`emu_write_mem`, `emu_find_mem`, `emu_backtrace`, `emu_read_mem(addr, bank=N)` / `page=N`
(128K banks, Next 8K pages), `emu_memory_map`. `emu_status` has `last_stop` (where + why). `emu_cmd("help")` lists every session
command; `emu_cmd("zrcp", {"line": "help"})` reaches ZEsarUX's own protocol.
Details: docs/emulator-harness.md.

- Frames are 1/50 s of guest time; headless runs ~44x real time (48K), ~9x (Next).
- `emu_run_until` only matches output written after the last `emu_log` call, the last
  load/reset, or its own previous match. It stops on the instruction that logged it.
- Make programs observable: log state changes with `zx_log("tag: key=%u\n", v)`.
- `emu_press` runs frames (3 held + 3 released): it stops at a breakpoint like `emu_run`.
- A window session pauses its window while you fast-forward or step it.

## Rules learned the hard way

- **Use `-clib=sdcc_ix`** (project.mk does). Games use the SDK's IM 2 frame timer
  (`frame_init()`/`frame_wait()`): the ROM's IM 1 handler writes through IY, which
  compiled code doesn't keep, and that corrupted memory. Keep programs below 0xF000
  (vector table; project.mk checks the map).
- zsdcc (z88dk 2.3): no `--opt-code-speed` (it miscompiled text_num); 2D/struct array
  indexing and `*` become multiply calls, so walk pointers in hot code; compound
  bit-sets like `a[i] |= m` in loops have miscompiled; printf evaluates arguments right
  to left (store rnd16() results first); for the 48K, `((int32_t)a << 14) / d`
  came out wrong (fixmath.c writes it as a multiply). Profile with breakpoints + `get-tstates-partial`
  before guessing, and move real hot loops to asm.
- zsdcc also miscompiled `dz = (o->z - c->z) >> 4` (int32_t fields through pointers: byte 2
  of the subtraction was never loaded) and once emitted an impossible `ld iyl,l` for
  `z = ((int32_t)i << 7) + c->dz[side]` (assembler "syntax error" in the .asm line). Compare
  ported code with a host build (scairy_fun's tools/check_trace.sh) rather than trust it.
- More code than 0x8000-0xEFFF: put files in banks with `--codesegBANK_n --constsegBANK_n
  --datasegBANK_n --bsssegBANK_n` (`#pragma output CRT_ORG_BANK_n = (2n << 16) | addr` in
  zpragma.inc; 8K pages: `PAGE_n`, `(n << 16) | addr`) and map them before calling. SDK C
  modules can be banked the same way from a project Makefile (target-specific EXTRA_CFLAGS on
  `$(OBJDIR)/sdk/...c.o`). A banked file's variables are only visible while it is mapped:
  copy what resident code needs (scairy_fun's main.c reads game.c's `game_in` before mapping
  the simulation). The linker doesn't report a bank or page section that overflows;
  `sdk/project.mk` checks every `__BANK_n_size` / `__PAGE_n_size` after the link and fails
  the build (it used to crash at run time).
- Game loops: fixed timestep (`ticks = frame_wait()`, logic `ticks` times, draw once),
  so game speed stays the original's when drawing is slow. A tap shorter than a slow
  frame is lost unless input is latched: `frame_hook = input_sample; input_latch(1);`
  (presses are then edges collected by the interrupt; a press also counts as held for
  that read, and a release a slow frame hid is not seen - see stakattack's DAS note).
- ZEsarUX's .nex loader sets 28 MHz while the PC is in ROM, where its default "deny
  turbo in ROM" caps it at 14 MHz, and it only re-applies NextReg 7 when the value
  changes: next_init() writes 2 then 3. Check with ZRCP `get-cpu-turbo-speed` (8 =
  28 MHz, ~560K T-states a frame).
- newlib's terminal ignores ZX control codes (`printf("\x16...")` prints `?`):
  draw text with `zx_print_at()` (sdk/include/zxtext.h, ROM font). The default
  STARTUP is 31 (no terminal); STARTUP=0 brings the printf terminal back.
- z88dk printf: `%d/%u/%x` are 16-bit; use `%ld/%lu` with `long`. `zx_log` output
  is cut at 127 characters.
- On the Next include `<arch/zxn.h>` only (it includes `arch/zx.h`; both = errors).
  `__ZXNEXT` is defined for `+zxn`.
- Next Layer 2 starts at the 16K bank in NextReg 0x12 (9 after NextZXOS boots, not 8):
  map 8K pages `2*bank .. +5`. Pixel 0xE3 is
  transparent (global transparency), so avoid it in pictures.
- Keep heavy maths out of inner pixel loops: one `/` per pixel made hellonext's
  Layer 2 fill take 4 s at 28 MHz.
- ZRCP numbers are decimal (`disassemble 8ceb` = address 8); the Python side
  always sends decimal. `snapshot-save/load` take the path verbatim (no
  quotes), so the wrapper copies through a temp file for paths with spaces.
- ZEsarUX's `set-machine` drops cpu-step mode and leaves a menu open; the
  wrapper closes it and re-enters step mode. In a *window* it re-creates the
  SDL window from the ZRCP thread and macOS aborts, so window sessions restart
  the emulator to switch machine (new ZRCP port, log restarts, breakpoints gone).
- Anything that touches SDL/AppKit from the ZRCP thread aborts on macOS
  ("nextEventMatchingMask should only be called from the Main Thread"). The patch
  guards host input and window refresh (`zxh_host_io_allowed`); keep new hooks
  inside that rule.
- When nowait mode grabs a free-running CPU that is waiting for the host timer at
  the end of a frame, it must release that wait (`zxh_timer_tick`), or `zxh-run`
  hangs forever.
- On stdout ZEsarUX prints ZXI log bytes < 32 as `?`; the harness log keeps the
  real bytes.
- Window sessions: the host keyboard feeds the same matrix as injected keys, and
  ZEsarUX clears it on focus changes (wait ~1 s after opening a window).
- A snapshot load doesn't re-render the display: compare screens one frame after
  `load_state`. The Next can only be snapshotted as .zsf (ZEsarUX blocks on an error
  dialog for .sna/.z80 there; the wrapper refuses). ZEsarUX has no .szx writer.
- ZEsarUX's hard reset does not clear RAM (the ROM does, once it runs); use
  `power_cycle` for a really fresh machine.
- ZEsarUX doesn't emulate NextReg 0xB0/0xB1 (Next extended keys); the patch does.
- ZEsarUX's hard reset does almost nothing on a plain Spectrum (RAM, most registers,
  MEMPTR and the FLASH phase survive), so boots depended on history; `load`/`reset`
  now go through `zxh-sync ram` + fixed registers. Keep it that way.
- Snapshots (.zsf too) don't store the T-state within the frame; save_state writes
  `<file>.beam`. A snapshot taken while a tape loads misses the tape state: record from
  reset (`from_reset=True`, which `./zx record` uses) for programs still loading.
- A free-running frame right after the CPU is released can be frame-skipped; anything that
  hashes or screenshots frames must force rendering (zxh_force_render does while hashing).
- In a window, `run`/`run_until` fast-forward on the main thread (host input live) and
  stop a few frames late; paused sessions stop on the exact instruction.
- Loading a .tap/.sna/.z80 in a Next session switches it to 48K (tapes don't autoload on
  the TBBlue boot); pass machine= to override.
- ZRCP ports come from 20000-29999 (not the OS ephemeral range) and the host checks the
  emulator PID in zxh-status before using a port. The intermittent "did not open ZRCP"
  was never reproduced (40 parallel starts at load 22 all fine); see PROGRESS.md.
- Docker images that no container uses get pruned on this machine: a stopped
  `zx-dev-toolchain-keep` container pins ours, and bin/zx-make rebuilds a missing image.
- Memory zones: 128K RAM bank N is zone 0 offset N*16384; the Next's RAM page N is
  zone 0 offset 0x40000 + N*8192 (ROMs sit below). Palettes come back as 9-bit RGB333.
- Banked symbols (z88dk BANK_n/PAGE_n sections) are linked at bank << 16 | address:
  read_mem/write_mem (and MCP emu_read_mem) by name go to their Next RAM page,
  mapped or not; breakpoints/watchpoints use the CPU address they run at. A bare
  number above 0xFFFF is an error (it used to hang read_mem).
- Emulators started by the harness (ZXH_PARENT_WATCH=1) exit by themselves when
  their parent process dies (zxh.c watchdog): no orphans from killed scripts.
- Builds go through a shared cache (scripts/buildcache.py, used by bin/zx-make for a
  plain build): key = the project's files (not build/, screenshots, docs, test.py,
  arcade.json) + the SDK + zx-make + the toolchain image id; outputs (the files in
  build/) are copied on a hit, in every worktree. `ZX_BUILD_CACHE=off` to bypass,
  `ZX_BUILD_CACHE=<dir>` elsewhere; default `<repo's parent>/.spectrum_dev-buildcache`.
  The arcade test also caches 48K snapshots and only restarts games whose launch
  key (menu + card + program bytes) isn't verified yet, plus the first and last;
  `--all` / `ZX_ARCADE_ALL=1` restarts every game.
- Stop only our own emulators: the harness kills its own Popen (and checks the ZRCP
  port answers with its pid before using it); never pkill/killall by name, other
  agents and the user run ZEsarUX on the same machine (`./zx selftest` checks).
- Sinclair 2 is keys 1-5 = left, right, down, up, fire (not 5-1).
- The copper restarts at the vertical blank, raster line 248 at 50 Hz (ZEsarUX and the
  Next), not at line 0: a list does lines 248..311 (the top border) first, then 0..247.
- Don't write Next registers from the interrupt: a register write is two port writes
  (select 0x243B, value 0x253B) and an interrupt in between redirects the program's value.
- zsdcc miscompiles small struct copies and stores: `*e = g->ev[0]` (an 8-byte
  struct, and a memcpy in its place) became an LDIR whose source came from an
  uninitialised stack slot; `g->nev = 0` after three 32-bit assignments became
  `LD L,H / LD (nev),HL` with H taken to still be 0 (at -SO0 too: it's codegen).
  Copy structs field by field; if a value is wrong, read the disassembly
  (`emu_disasm`) and a write watchpoint before suspecting the logic.
  block_breaker_3d runs the original's tests in the game to catch these.
- `dma_copy_banks`/`dma_fill_banks` map MMU slots 0-1 and give the ROM back:
  don't call them from code banked at 0x0000, and remap your bank after.
- `frame_count` counts frame_wait() calls (loop iterations), not frames.
- Banked code: a function at 0x0000/0x6000 must not call code in another bank, and an SDK
  call it makes must not map over the bank its arguments live in (nexttile uses slot 2 only).
  zsdcc won't initialise arrays of structs (`= {0}`): put big tables at a fixed address.
- `Emulator.hold()` adds to the keys already held: `release()` before holding a new set.
- SD images: ZEsarUX `chdir`s into its bundle before opening `--mmc-file`, so a relative path
  gives "Error initializing SD card"; the harness passes it absolute. ZEsarUX writes the guest's
  changes back to the image (`sd_persist=False` / `--sd-scratch` don't).
- NextZXOS's boot loader matches 8.3 short names: a mixed-case name like `enNxtmmc.rom` must keep
  its upper-case short name (`ENNXTMMC.ROM`), not a `~1` alias (fatimg does).
  `machines/next/config.ini` `timing=8` runs the video-mode test on every boot; the arcade sets 0.
- Dot commands take esxDOS paths (`.nexload /arcade/x.nex`); `c:/...` fails. BASIC's `LOAD` and
  `SPECTRUM "c:/x.sna"` (token 0xA3, the 128K keyword) take `c:/`. Run these from BASIC
  (autoexec.bas): the menu returns to BASIC instead of calling them itself.
- `run_until` stops on the instruction that wrote the match: the rest of that log line isn't
  there yet. Run a frame (or wait for the `\n`) before parsing values out of it.
- The linker pulls whole SDK files: adding a function to a file a game links can push it over
  0xF000 (Boing Pinball has ~20 bytes spare). Put new functions in their own file.
- On a 48K, 0x5D00-0x7FFF is free once a C program runs (the BASIC loader is done with):
  put tables built at start-up there when 0x8000-0xF000 is full (Rock Blaster's lowmem.h).
  It is contended memory, so keep hot loops' data above 0x8000.
- A plain 128K has one AY and ignores the TurboSound chip select: writing "chip 1/2"
  registers overwrites chip 0's (modplay now only touches the chips a song routes to).
- To profile, sample: stop at random (frame, T-state) points with `run_to` and histogram
  the PC by the map's labels (local `l_func_NNNNN` labels locate C hot spots in
  `zcc -S` output); breakpoint-to-return timings include any interrupt in between.
- ZEsarUX draws a scanline at its end, so a copper move anywhere on line L shows on L; on the Next
  a move after the right border (h >= 36) shows from L + 1. `split_row(y)` (nextsplit.h) waits for
  the horizontal blank above row y: right on the Next, one row early in ZEsarUX.
- With a Sinclair/cursor joystick selected, ZEsarUX's Kempston port floats (0xFF: every direction
  held). Treat a Kempston read with bits 5-7 set as nothing (input2.c does).
- On the Next with the ULA off and the tilemap's definitions below 0x2000 in bank 5, page 11
  (0x6000-0x7FFF) is free: `ORG := 24576` gives a C program 8K more (rotoball).
- `./zx shot --until X --frames N` can grab a frame before the program has drawn its first
  screen; check with a Python `Emulator` and `run(n)` before chasing a missing layer.
- 0x4000-0x7FFF is contended on the 48K/128K: code there ran ~40% slower while the screen
  is drawn (Sky Knights at ORG 24576). Keep code from 32768 up; put tables there instead.
- A 48K misses an interrupt now and then without any DI: the INT pulse lasts 32 T-states
  and an instruction that straddles it (IX/IY ones take 19-23) hides it. Frame counts
  drift by build; compare builds tick for tick (Sky Knights' tick tape), not by frame.
- `ldir` with BC = 0 copies 64K: a zero-width rectangle wiped the IM 2 table (and the
  game crashed into the ROM a few frames later). Guard widths of 0 in asm loops.
- zsdcc pushes a char argument as one byte (`push af; inc sp`): an asm function with
  (int16, char, ptr, char, char) finds them at sp+2, +4, +5, +7, +8.
- zsdcc got `v += v >= 0 ? K / 2 : -(K / 2); return v / K;` wrong for a long `v`
  (0 came out as 2); Marble Tilt's view_step rounds with plain loops instead.
- `l2_init()`, `l2_clear()` and `dma_*_banks()` work through MMU slots 0-1 and put the
  ROM back: map banked code at 0x0000 after them (Marble Tilt's main.c).
- With the ULA off, ZEsarUX still shows the ULA's border colour (ULA palette entry
  16 + border) unless that entry is the transparency colour; set it to 0xE3
  (`pal_set8(0, 16, 0xE3)`) and the border shows the fallback colour (NextReg 0x4A),
  as on the Next.
- `run_until` stops on the instruction that starts the log line: `run(1)` before
  parsing the line. A test's `write_mem` is not in an input recording: anything it
  changes (even only log output, which costs time) breaks the replay.
- Code that maps Layer 2 pages into a slot must check its data before putting the
  slot back (a list read through slot 2 read 0 after the restore: boards half drawn).
- z80asm rejected a label named `px` (syntax error at its use); Z80N instructions (`add hl,nn`) assemble in sdk .asm files as they are.
- `dma_copy_banks` / `l2_copy_to_back` / `l2_clear` map slots 0-1 and give them back to the ROM:
  remap banked code (and its data) after them, before calling into a bank.
- zsdcc: an `extern` declared inside a function body gave "undefined symbol" at link; declare at
  file scope. Its `int` is 16 bits: `3 * q`, sums of four Q14 dots or of four depths overflow
  silently (the same C is fine on a 32-bit host): halve before adding.
- Measure the cost per row and per call of any polygon routine before relying on it: l2poly
  is ~660 T-states a row and ~10K a call; Planet Chomp's draw.asm (edge buffers, a span jump
  table) is ~275 a row. Profile with a PC sampler (projects/planet_chomp/tools/profile.py):
  estimates of zsdcc and vec3 costs were 2-3x low.
- Hand-written asm loops that keep state in BC/DE/IXL must save them around any new call in
  the loop (a threshold loaded into BC broke Planet Chomp's corner rows silently).
- zsdcc: `long + 0x8000` became unsigned (`0x8000` is an unsigned int), so `(WORD)((x + 0x8000) >> 16) > 256`
  was true for x = -0.7: write `0x8000L`, and compare through a local. A function returning a pointer found
  by walking a struct array returned garbage (uranus_lander's choose_pad): return an index. 32-bit
  arithmetic through struct pointers costs ~2,000 T-states a statement (Uranus's 32 particles took
  more than a frame): such loops go to asm. `char` arguments are pushed as one byte each (asm reads
  them at consecutive offsets).
- A 48K game too big for 0x8000-0xF000 can move its variables below the code: `#pragma output
  CRT_ORG_BSS = 0x5D80` in zpragma.inc (past the BASIC loader) and a lower ORG (uranus_lander); appmake
  and project.mk catch overlaps. Data below 0x8000 is contended: memory-heavy loops run up to 2x slower.
- Latched input counted a press twice when input_read saw it before the interrupt sampled it (the
  sampler then latched it as a new edge): typed "BOT" became "BBO", the arcade's S toggled twice.
  input_read now marks the presses it found itself in the sampler's state (input_last_rows), so they
  don't come back. Copying all of its view there lost presses (its view can be stale), and reporting
  only the interrupt's edges changed timing enough to break Spectral Keep's bot. Timing changes
  (here: a faster input_read) expose such bugs.
- zsdcc: `int8_t * uint8_t` zero-extended the `int8_t` (-1 * 6 came out 0x05FA): Stack Tower's icon x became
  0x06E0 and `l2w_column` mapped a page far past Layer 2 (its board cache). Make both `int16_t` first.
  A wild x in `l2w_*` writes some other page, not the screen: look there for "stray" pixels that persist.
- zsdcc got `px + pw > e->x + 3 && px < e->x + 13 && ...` (int16 arguments against fields of a
  struct walked by pointer) wrong: true for an enemy 90 pixels away. Read the fields into locals
  first. Build the game logic for the host too and compare the two tick by tick (jump_quest's
  trace): that is how it showed up. `f(a, g(b))` with `f` testing its arguments: g runs anyway
  (jump_quest re-shifted every enemy's sprite a frame until a visibility test came first).
- A demo/route player called before the logic tick sees `level_ticks == 0` on a level's first
  tick, not 1 (jump_quest's demo started its routes a tick late).
- When the logic costs a large part of a frame, a slow frame runs several ticks, the sprites move
  further, more cells change and the next frame is slower: measure frames drawn against ticks
  over a stretch of play (jump_quest's test), not single calls.
- Fixed-timestep loops: cap the catch-up ticks (a level's first frame can take seconds), but high enough
  (Uranus: 6) that a slow frame doesn't lose game time; count ticks vs frames in a perf log and test it.

- zsdcc through a pointer to a struct (`s->x` in a loop) is several times the code
  of the same work on a static copy (`memcpy(&cur, s, n)`, `cur.x`): cellspr48's
  first sprite loop took 17,000 T-states doing nothing. Measure a function with
  a breakpoint at its entry and one at its return address (Miner Meteor's
  `tools/timefn.py`), or sample PCs (`tools/profile.py`); estimates were 2-5x low.
- `run_until(text)` stops on the instruction that wrote the text: the rest of the
  log line isn't written yet (run a frame, or match the whole line).
- A bot that must feed exact keys per logic tick: read the keys before each tick
  and set the next tick's keys at a breakpoint on the tick function; set them at
  the frame boundary instead and a slow frame (two ticks) or the interrupt's
  latch (`input_latch`) gives a tick the wrong keys (miner_meteor/test.py).
- A test bot can plan with the game's own C built for the host (Bullion Dash's
  tools/hostsim.c): `#pragma pack(push, 1)` on the state structs (not -fpack-struct:
  it breaks pointer alignment in other structs) gives `gs` the Z80's layout, so it
  is copied from the emulator; checking each game loop against the host build
  catches asm/C differences. Stop at `frame_wait` to read a consistent state: at an
  arbitrary frame boundary the loop may be half way through its ticks.
- zsdcc: a 32-bit multiply with a call as an operand (`return d->amp * a3_sin(a);`, `amp` an int32
  field) returned only its low 16 bits; with the same call in a local first it was right. Orbit Docking's
  debris then sat on the axis. Put the call's result in a local; Orbit Docking routes such maths through
  fix16's asm `fmul`/`fdiv`. Each rewrite moved the next fault: compare the whole game tick by tick.
- zsdcc passes `uint8_t`/`int8_t` varargs as one byte: `zx_log("%d", gs.gx)` with an
  8-bit field prints garbage; cast to `(int)`.
- A key held for fewer frames than a slow frame (a full-screen redraw) is seen by the
  latch only as edges: SPACE's fire edge survives, BREAK (CAPS + SPACE, a held
  combination) does not. Tests press BREAK after the redraw, or hold it ~10 frames.

## Layout

- `projects/<name>/` – `Makefile` (NAME, TARGET, include sdk/project.mk), `src/*.c *.asm`,
  `test.py` (host-side test using `zxdev.emu.Emulator`), `build/` outputs.
- `sdk/` – project.mk, zxlog (C + sjasmplus macro), zxtext, template.
- `emulator/` – our ZEsarUX extension (`ext/zxh.c`) and patches (`patches/`, README).
- `tools/zxdev/` – Python: Emulator, ZRCP client, sessions, CLI, MCP, DevBench; `tests/`.
- `third_party/zesarux/` – fetched + built by setup (pinned in scripts/lib.sh), not committed.
- Game ports (phase 2) go in `projects/`; see PROGRESS.md for which games fit 48K vs Next.
- A screenshot taken at a breakpoint (e.g. at `frame_wait` to "settle") shows the rows above the
  beam from this frame and the rows below it from the last: ZEsarUX draws a grey line at the beam.
  Run one more frame (`e.run(1)`) before the screenshot (lunar_rider's test does).
- Layer 2 320 x 256 parallax: `l2scroll.h` (bands scrolled by the copper, column rings). A tilemap
  scrolled as a ring of tile columns has a seam (the left and right partial tiles share a map
  column at sub-tile offsets): make its content repeat every 320 pixels instead.
- Banked code must not call a function of another bank mapped at the same addresses (Tank
  Duel's front end at 0x0000 called `td_enemies_left` in the rules' bank at 0x0000: garbage).
  A resident function may switch banks and switch the caller's bank back before returning.
- A slow picture loses keys held for a short tap between reads: read letter keys with
  `input_letter_pressed()` (latched), not `key_down()`.
- Fixed-step catch-up: cap the ticks per loop high enough for the slowest picture (Tank Duel:
  12 frames), or the game runs slower than the original when the picture is slow.
- Layer 2's write paging (port 0x123B, NextReg 0x13 = any 16K bank) splits 0x0000-0x3FFF: reads come from
  the MMU, writes go to the bank. Never `memset` through it: z88dk's memset is an LDIR from dst to dst + 1,
  which reads back whatever is mapped for reading (Boing Twist's texture filled with noise); use texview's
  `tv_fill`. Keep variables out of 0x0000-0x3FFF while the window is open.
- zsdcc built `h = (h ^ p->slot[i].home) * K` (uint32_t h, a uint8_t field through a pointer) with byte 1 of
  `h` replaced by 0, at -SO0 to -SO3; reading the byte into a local first fixed it (Boing Twist's bt_hash: the
  original's self-test hash showed it). Run an original's own self-test on the Z80 when there is one.
- zsdcc compiled `isr_pressed |= b & ~isr_held;` (volatile globals) as `isr_pressed |= 0`: the
  `b & ~isr_held` term was dropped and no press was ever latched. Write such updates through locals
  (Lander 3D's game_sample) and read the `zcc -S` output of interrupt code.
- LDI/LDIR count BC down: a pointer kept in BC across an LDI copy is gone (Lander 3D's vf_seg).
- At 28 MHz ZEsarUX's T-states for the Next ran ~1.5-2x what instruction counts predicted
  (Lander 3D's measured routines): measure with breakpoints (tools/timefn.py) before planning a budget.
- A computed jump into an unrolled block needs the block within one 256-byte page: `ALIGN 256` inside a
  PAGE_n/BANK_n section does not align the absolute address; put the block in `data_align_256`.
- A polygon filled as columns (upper edges down to a buffer the lower edges wrote) must clear that
  buffer over its columns first: a stale value ran a fill far past the view into resident code.
- z80asm only warns ("integer range") when an 8-bit operand overflows: `ld d,CS_SIZE` with
  CS_SIZE = 521 assembled as 9 and Rolling Steel's cache slots overlapped (course 1 passed by luck).
  Read the build's integer range warnings for .asm files. A macro parameter named like a register
  (`a`) is substituted into `or a` too: give parameters names like `p_a`.
- NextReg 0x15 bits 4-2 = 110 / 111 are blend modes, not a layer order: "tilemap over sprites
  over Layer 2" (U S L) is 0x13 (with sprites on and over the border).
- `tm_init()` doesn't define tile 0, and bank 5 still holds the ULA screen there: an undefined
  tile 0 covers the screen. Define it blank (Rolling Steel's hud.c).
- A banked C file's initialised globals are in its bank (`int course_count = 6;` with
  --datasegBANK_14): another bank reading it gets garbage. Uninitialised ones stay resident unless
  --bssseg moves them.
- The build cache restores `build/` without execute bits: keep host programs a test compiles
  (e.g. tools/hostsim.c) in `build-trace/`.
- The build cache keeps only the files directly in `build/`: a test reading `build/levels/...` failed
  after a restore. Copy what a test needs to `build/` (Lander 3D's `build/levels_expect.txt`).
- `far` (and `near` in some versions) is an SDCC keyword: a variable named `far` is a syntax error.
- A breakpoint on banked code (tools/timefn.py) also stops in the other banks mapped at that
  address, and its timings are garbage then. Time a frame's parts with raster-line marks instead
  (Lander 3D's src/prof.h: NextReg 0x1E/0x1F + frames counted by the interrupt; read the sums signed).
- The SDK's `input_sample()` keeps its own last view of the keys: started (or resumed) while a key is
  held, it latches that key as a new press. Call it and `input_read()` before handing the keys to an
  editor (Lander 3D's initials took the fire that opened them once the frame rate rose).
- Exact 32-bit physics (Rolling Steel): an odometer that adds |move| + 1 for every change of the
  ball's position bounds how far it can have moved since a triangle's test, so a sphere, plane or
  edge test that failed with more slack than that is still failing: skip it, same result.
