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
./zx shot <project> -o out.png --until "ready" [--press Q@400] [--no-border] [--scale 2]
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
`emu_load`, `emu_machine`, `emu_reset`, `emu_save_state`/`emu_load_state`, `emu_screenshot(path)`.

Debugging: `emu_break("func")` or `emu_break(condition="A=5 AND PC=32768")`, then
`emu_continue(max_frames)`; `emu_watch("global", 2, "w")`, `emu_stepi`, `emu_step_over`,
`emu_regs`, `emu_set_reg`, `emu_disasm`, `emu_symbols`, `emu_read_mem("sym")`,
`emu_write_mem`, `emu_find_mem`, `emu_backtrace`. `emu_cmd("help")` lists every session
command; `emu_cmd("zrcp", {"line": "help"})` reaches ZEsarUX's own protocol.
Details: docs/emulator-harness.md.

- Frames are 1/50 s of guest time; headless runs ~44x real time (48K), ~9x (Next).
- `emu_run_until` only matches output written after the last `emu_log` call, the last
  load/reset, or its own previous match. It stops on the instruction that logged it.
- Make programs observable: log state changes with `zx_log("tag: key=%u\n", v)`.
- `emu_press` runs frames (3 held + 3 released): it stops at a breakpoint like `emu_run`.
- A window session pauses its window while you fast-forward or step it.

## Rules learned the hard way

- **Use `-clib=sdcc_ix`** (project.mk does): it leaves IY alone for the ROM's IM 1
  interrupt handler, so `intrinsic_ei(); intrinsic_halt();` paces at 50 Hz.
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

## Layout

- `projects/<name>/` – `Makefile` (NAME, TARGET, include sdk/project.mk), `src/*.c *.asm`,
  `test.py` (host-side test using `zxdev.emu.Emulator`), `build/` outputs.
- `sdk/` – project.mk, zxlog (C + sjasmplus macro), zxtext, template.
- `emulator/` – our ZEsarUX extension (`ext/zxh.c`) and patches (`patches/`, README).
- `tools/zxdev/` – Python: Emulator, ZRCP client, sessions, CLI, MCP, DevBench; `tests/`.
- `third_party/zesarux/` – fetched + built by setup (pinned in scripts/lib.sh), not committed.
- Game ports (phase 2) go in `projects/`; see PROGRESS.md for which games fit 48K vs Next.
