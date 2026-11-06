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
```

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
- Stop only our own emulators: the harness kills its own Popen (and checks the ZRCP
  port answers with its pid before using it); never pkill/killall by name, other
  agents and the user run ZEsarUX on the same machine (`./zx selftest` checks).
- Sinclair 2 is keys 1-5 = left, right, down, up, fire (not 5-1).
- The copper restarts at the vertical blank, raster line 248 at 50 Hz (ZEsarUX and the
  Next), not at line 0: a list does lines 248..311 (the top border) first, then 0..247.
- Don't write Next registers from the interrupt: a register write is two port writes
  (select 0x243B, value 0x253B) and an interrupt in between redirects the program's value.
- Banked code: a function at 0x0000/0x6000 must not call code in another bank, and an SDK
  call it makes must not map over the bank its arguments live in (nexttile uses slot 2 only).
  zsdcc won't initialise arrays of structs (`= {0}`): put big tables at a fixed address.
- `Emulator.hold()` adds to the keys already held: `release()` before holding a new set.
- z80asm rejected a label named `px` (syntax error at its use); Z80N instructions (`add hl,nn`) assemble in sdk .asm files as they are.


## Layout

- `projects/<name>/` – `Makefile` (NAME, TARGET, include sdk/project.mk), `src/*.c *.asm`,
  `test.py` (host-side test using `zxdev.emu.Emulator`), `build/` outputs.
- `sdk/` – project.mk, zxlog (C + sjasmplus macro), zxtext, template.
- `emulator/` – our ZEsarUX extension (`ext/zxh.c`) and patches (`patches/`, README).
- `tools/zxdev/` – Python: Emulator, ZRCP client, sessions, CLI, MCP, DevBench; `tests/`.
- `third_party/zesarux/` – fetched + built by setup (pinned in scripts/lib.sh), not committed.
- Game ports (phase 2) go in `projects/`; see PROGRESS.md for which games fit 48K vs Next.
