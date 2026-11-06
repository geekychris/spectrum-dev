# Remote control, debugging and the emulator harness

Every running emulator is a **session**: one `zxdev serve` process holding a
patched ZEsarUX, a JSON control port, and (optionally) a window. Agents,
scripts, DevBench and people can all drive the same session at once.

```
                     ┌──────────── zxdev session process ─────────────┐
 MCP (Claude) ──┐    │  control port (JSON lines) ─ Session ─ Emulator │     ┌──────────────┐
 ./zx ctl ──────┼───▶│  registry: build/sessions/<pid>.json            ├────▶│ ZEsarUX      │
 DevBench ──────┘    │                                                 │ZRCP │ + zxh patch  │
                     └─────────────────────────────────────────────────┘ TCP │ (window or   │
 DeZog / telnet ───────────────────────────────────────────────────────────▶│  headless)   │
                                                                              └──────────────┘
```

The Python `Emulator` class (`tools/zxdev/src/zxdev/emu.py`) is the core: it
starts ZEsarUX with ZRCP on a free port and drives it. Tests use it
in-process; sessions wrap it for sharing.

## Starting sessions

| | |
|---|---|
| `./zx run hello48` | window, free-running, real time (registered; `./zx ctl`/MCP attach) |
| `./zx serve hello48` | headless, **paused**, ports printed + registered |
| `./zx serve hellonext --window --mode paused` | anything in between |
| `./zx sessions` | list running sessions (pid, ports, machine, program) |
| MCP `emu_boot("hello48")`, `emu_boot("hellonext", window=True)`, `emu_attach()` | Claude starts or joins one |

**Paused** sessions (the default headless) advance only on `run`,
`run_until`, `press`, `continue`, `stepi`, deterministically and as fast as
the host allows (~44x real time on a 48K, ~9x on the Next).

**Running** (window) sessions free-run in real time. The deterministic
commands (`run_until`, `continue`, `stepi`, `save_state`, `load`) briefly hold
the CPU, fast-forward, and let it go; `run N` just waits N/50 s. While the
CPU is held the window stops refreshing (see emulator/patches/README.md).
`pause` / `resume(turbo)` switch modes.

## Control API

`./zx ctl <cmd> key=value ...` (newest session, or `--port N`), MCP
`emu_cmd`, DevBench `POST /api/cmd/<cmd>`, or raw JSON lines on the control
port:

```json
{"id": 1, "cmd": "press", "args": {"keys": "SPACE"}}
{"id": 1, "ok": true, "result": {"frames": 3, "frame": 158, "reason": "frames", "pc": 56}}
```

`./zx ctl help` prints the full list. Grouped:

| Area | Commands |
|---|---|
| session | `status` (incl. `last_stop`: where and why it halted) `load(path, machine)` `machine(name)` `pause` `resume(turbo)` `quit` |
| restart | `reset(hard)` `reload` `power_cycle` `reboot` |
| running | `run(frames)` `run_until(text, max_frames, since)` `run_to(frame, tstates)` `position` `log(since, all)` `log_cursor` |
| input | `press(keys, hold_frames, release_frames)` `hold(keys)` `release(keys)` `type(text)` `joystick(kind)` `sequence(steps)` |
| screen | `screenshot(path, border, scale, layer)` (PNG, base64 in `png_b64`) `screen_hash(layer)` `record_video(path, frames, every, scale, layer)` |
| replay | `input_log` `checkpoint(label)` `save_replay(path)` `replay(path, verify)` |
| memory | `read_mem(addr, length, bank, page)` `write_mem(addr, hex, bank, page)` `find_mem(hex/text)` `memory_map` |
| symbols | `symbols(pattern)` `addr2sym(addr)` `disasm(addr, count)` |
| debugger | `regs` `set_reg(reg, value)` `break(addr / condition)` `watch(addr, length, kind)` `delete(index / addr)` `breakpoints` `continue(max_frames)` `stepi(count)` `step_over` `backtrace(items)` |
| states | `save_state(path)` `load_state(path)` (.zsf default; .z80, .sna) |
| raw | `zrcp(line)`: any ZEsarUX command (`zrcp line=help`, `zrcp line=zxh-help`) |

Anywhere an address is expected you can pass a number, `$8000`/`0x8000`/`8000h`,
a symbol (`main` or `_main`), `symbol+0x10`, or a register name (`pc`, `sp`, `hl`…).

**Machines:** `48k` `16k` `48k+` `128k` `plus2` `plus2a` `plus3` `pentagon`
`next`. Loading a `.nex` selects the Next. Headless sessions switch with
ZEsarUX's `set-machine`; window sessions restart the emulator (new ZRCP
port, log restarts, breakpoints dropped) because ZEsarUX re-creates its
window from the ZRCP thread, which macOS forbids.

## Input

**Keys:** `A`–`Z`, `0`–`9`, `ENTER`, `SPACE`, `CAPS` (Caps Shift), `SYM`
(Symbol Shift); combine with `+` (`CAPS+1`, `SYM+P`, `CAPS+SYM`). Input goes
straight into the keyboard matrix (`set-ui-io-ports`), so it works with any
key-reading code (ROM, `in_inkey`, direct port reads), headless and in a
window. In a window session the host keyboard feeds the same matrix, and
ZEsarUX clears it on window focus changes.

**Joysticks:** `UP` `DOWN` `LEFT` `RIGHT` `FIRE` go to the emulated joystick,
set with `joystick kind=...`:

| kind | what the program sees |
|---|---|
| `Kempston` (default) | port 0x1F, bits R L D U F = 0..4 (1 = pressed) |
| `Sinclair 1` | keys 6 7 8 9 0 = left right down up fire (half-row 0xEFFE) |
| `Sinclair 2` | keys 1 2 3 4 5 = left right down up fire (half-row 0xF7FE) |
| `Cursor` | 5 6 7 8 = left down up right, 0 = fire |
| `Fuller`, `None`, … | see `zrcp line="help"`; ZEsarUX's list |

**Next extra keys:** `EDIT` `CAPSLOCK` `TRUEVIDEO` `INVVIDEO` `CURSOR_LEFT`
`CURSOR_DOWN` `CURSOR_UP` `CURSOR_RIGHT` `GRAPH` `DELETE` `BREAK` `EXTEND`
`SEMICOLON` `QUOTE` `COMMA` `PERIOD`. Like the real keyboard, each produces
its matrix combo (EDIT = CAPS+1, BREAK = CAPS+SPACE, EXTEND = CAPS+SYM, `;` =
SYM+O, …) *and* sets its bit in NextReg 0xB0/0xB1, which our patch provides
(ZEsarUX doesn't emulate those registers).

**Timing:** `press` holds for `hold_frames` then releases for
`release_frames`; `hold`/`release` are open-ended; `type` presses one key
per character (capitals with CAPS, symbols with SYM). `sequence` steps can
carry `"at": N` (frames since load, optional `"t"`: T-state) for scripts
that act at exact frames:

```json
[{"at": 200, "hold": "RIGHT"}, {"at": 260, "release": "RIGHT"},
 {"at": 300, "press": "FIRE", "hold": 4}, {"at": 400, "type": "HI\n"}]
```

CLI: `./zx log p --press SPACE@200 --hold Q@250 --release ALL@300 --type "hi@320"`
or `--script steps.json`.

**Window sessions while fast-forwarding:** `run N` and `run_until` in a
free-running window run turbo *on the emulator's main thread*, so the user's
keyboard and joystick keep working. They poll for the frame count, log text
or a breakpoint, then go back to real time. They stop within a few frames
of the condition, not on the exact instruction; use a paused session for
that. The window shows 1 frame in 8 meanwhile. `continue`, `stepi` and
`run_to` hold the CPU (exact, but host input waits).

## The guest log

Programs write text to ZEsarUX's ZXI debug port: `OUT (0xCF3B),1` selects
register 1, then each `OUT (0xDF3B),c` is one character. The patch captures
it; on real hardware or other emulators the ports do nothing.

- C: `zx_log("game: score=%u\n", score)` / `zx_log_s("game: ready\n")`
  (`sdk/include/zxlog.h`; `-DZX_NO_LOG` / `make ZX_NO_LOG=1` compiles them out)
- sjasmplus: `ZXLOG "game: ready\n"` (`sdk/include/zxlog.inc`)

`run_until(text)` stops on the exact instruction that wrote the last byte of
`text`. It only matches output written after the session's *mark*: the last
`log()` call, `load`/`reset`, or the previous match. Pass `since=` (an
absolute offset from `log_cursor`) to choose. Text is Latin-1; the ring keeps
the last 4 MB.

## Recording and replay

**Input recorder** (`record_input`, MCP `emu_record_input`, `./zx record`).
The patched core compares the whole input state at every instruction
boundary: 8 keyboard half-rows, joystick bits, joystick type, and the Next
extended keys. It logs each change with its frame and T-state, whatever the
source: keys you inject, or the user typing or using a joystick in the window.
It also hashes every rendered frame.

- `record_input action=start`: from the current point. A `.zsf` snapshot
  (plus its `.beam` beam position) is taken under the same CPU hold as the
  start, so the replay starts at that exact instruction. The program must
  already be running: snapshots don't contain tape/loader state.
- `record_input action=start from_reset=true`: reloads the program and
  records from the load.
- `record_input action=save path=r.json`: JSON with the input changes, the
  per-frame hashes and the end position, with `r.zsf` beside it.
  `stop` and `status` also exist.
- `replay path=r.json` (`./zx replay r.json [--runs 2]`): restarts from the
  reset or snapshot, re-applies each input state at its exact position
  (`zxh-run at=F:T`, then `zxh-input-set`), hashes every frame and compares.
  The result has `ok`, `compared`, `mismatches` and `first_mismatch`. With
  `--runs N`, the N hash sequences must also be identical.

Frames are compared from the first whole frame. The frame the recording
started in was partly drawn before it, and that part can't be redone.
Window recordings replay exactly too: the input is logged where the core
saw it, and the 20 ms housekeeping doesn't affect what the program sees.
The test suite records a window session with keys typed through ZEsarUX's
host-keyboard path and with agent keys, then replays it headless. Every
frame hash matches.

Determinism of loads: `load`, hard `reset` and fresh emulators start from a
defined state (RAM zeroed, registers, MEMPTR and FLASH phase fixed; see
`zxh-sync`). Two emulators with different histories therefore produce
identical frames after loading the same program.

**Checkpoint replays** (the older, lighter form): every input change since
`load` is logged with its exact position (frames
since load, T-state in the frame): `input_log`. `checkpoint(label)` stores
the screen hash at the current point; `save_replay(path)` writes program,
machine, joystick type, the input log, the checkpoints and a final
checkpoint as JSON. `replay(path)` reloads the program in a fresh state,
runs to each recorded position with `zxh-run at=F:T` (stopping on the same
instruction boundary), re-applies the input, and compares every checkpoint's
screen hash:

```sh
./zx log hello48 --frames 600 --press Q@200 --type "hi@300" --record build/r.json
./zx replay build/r.json            # PASS/FAIL per checkpoint, exit code 1 on mismatch
```

Replays are deterministic: two replays give identical screens, and a changed
input log is caught (`test_features.py`). Inputs injected into a window
session are logged too, so a replay of a free-running session is exact even
though the session itself was not. The user's own typing in the window is
not logged.

**Snapshots** resume a test from a point: `save_state` / `load_state` with
`.zsf` (ZEsarUX's own format, complete: all machines incl. the Next), `.z80`
and `.sna` (48K/128K). None of them stores the position inside the frame;
`save_state` writes it to `<file>.beam` and `load_state` restores it, so a
resumed run stays in step with the original. ZEsarUX has no `.szx` writer, and can't write `.sna`
or `.z80` for the Next (the wrapper refuses instead of letting ZEsarUX block on
an error dialog). A load restores state but doesn't re-render the display:
compare screens one frame after `load_state`. `screen_hash(layer)` gives an
md5 of the pixels for cheap comparisons.

## Restart

| | |
|---|---|
| `reset hard=false` | Z80 reset only: PC=0, RAM and paging untouched |
| `reset` (hard) | ZEsarUX hard reset: all hardware state reinitialised; the ROM restarts (and clears RAM itself, as on real hardware) |
| `reload` | reset + load the same program again |
| `power_cycle` | a fresh ZEsarUX process with the same machine, no program |
| `reboot` | recover a wedged session: kills the emulator even in the middle of another command (that command fails), starts a new one and reloads the program |
| `machine name=...` | 48k 128k plus2 plus2a plus3 pentagon next |

## Debugging

ZEsarUX's own debugger does the work; we add symbols and a simple API.

- **Breakpoints** are ZEsarUX conditions: `break addr=main` becomes
  `PC=<addr>`; `break condition="A=5 AND PC=32768"`, `"MWA=23560"` (memory
  written at) etc. — see `zrcp line="help set-breakpoint"`. A breakpoint
  fires when its condition turns true, so resuming from it does not
  immediately re-fire.
- **Watchpoints** (`watch addr=score length=2 kind=w`) are ZEsarUX memory
  breakpoints on CPU reads/writes.
- `continue(max_frames)` runs until either fires; the result has `stopped`,
  `pc`, `symbol`, `break` (the condition) and `regs`. `run`, `press` and
  `run_until` also stop at breakpoints (`reason: "break"`).
- `stepi(n)`, `step_over`, `disasm`, `backtrace` (stack words with symbols),
  `regs`/`set_reg`.
- **Symbols** come from the z88dk `.map` (or sjasmplus `.sym`) next to the
  loaded file. C symbols have a leading underscore; lookups accept either.
- **Registers** (`regs`): AF BC DE HL, the alternates AF' BC' DE' HL', IX IY
  SP PC I R, IM, IFF, flags, MEMPTR, and the Next MMU string.
- **Where and why it stopped:** every run and step records `last_stop`;
  `status` reports it. `why` is one of `breakpoint`, `watchpoint`, `step`,
  `frame_limit`, `log_match` or `position`, with the PC, symbol, the condition
  that fired, the frame and the T-state.
- **Memory banks:** `read_mem addr=0 length=256 bank=3` reads 16K RAM bank 3
  on a 128K/+2/+3/Next whether it is paged in or not; `page=N` reads an 8K
  Next page; `write_mem` takes the same. `memory_map` lists what is paged in
  (128K: ROM/RAM per slot + screen bank; Next: the eight MMU pages).
- **Backtrace:** Z80 code has no frame chain, so `backtrace` lists the 16-bit
  words from SP with symbols; return addresses show up as `func+0xNN`.

There is no GDB stub: Z80 gdb is rare and ZEsarUX's native debugger covers
the same ground. For source-level stepping in VS Code, point
[DeZog](https://github.com/maziac/DeZog) at the session's ZRCP port (`zrcp_port`
in `./zx sessions` / `status`); ZRCP serves several clients at once.

## Python

```python
from zxdev.emu import Emulator                 # in-process, used by test.py files
with Emulator(machine="next") as emu:
    emu.load("projects/hellonext/build/hellonext.nex")
    assert emu.run_until("hellonext: ready", 2000)
    emu.press("X")
    emu.break_at("zx_print_at"); print(emu.continue_(50))
    emu.screenshot("shot.png")

from zxdev.control import SessionClient        # or a separate session process
s = SessionClient.spawn("projects/hello48/build/hello48.tap")
s.call("run_until", text="hello48: ready")
s.close(stop=True)
```

`tools/zxdev/tests/test_remote.py` exercises all of this end to end (run by
`./zx selftest`; `ZX_TEST_WINDOW=1` adds the window checks).

## Screenshots and recording

`screenshot` (layer `screen`) saves what the display shows: ZEsarUX's
rendered, composited frame, border included. 48K/128K: 352x304 (paper
256x192 at 48,56). Next: 704x608 — ZEsarUX renders the Next at 2x so
640-wide modes fit; `border=false` crops to the paper area (256x192 or
512x384), `scale` enlarges.

Single layers are rendered host-side from memory and registers at their
native resolution (`tools/zxdev/src/zxdev/video.py`), so a test can check a
layer even where another one covers it:

| layer | |
|---|---|
The composited screen includes everything the Next shows: border, ULA,
tilemap, Layer 2 and sprites. hellonext has all of them, and
test_features checks each one in a single frame.

| `ula` | 256x192 from the bitmap + attributes (bank 5, or 7 when the 128K shadow screen is shown); matches the displayed paper pixel for pixel |
| `layer2` | Next Layer 2 in its mode (256x192, 320x256 or 640x256), bank from NextReg 0x12, palette and offset; transparent pixels magenta |
| `sprites` | the Next's 320x256 sprite plane (RGBA): visibility, 8/4-bit patterns, palette offset, mirror, rotate, 2/4/8x scaling, relative (composite) sprites |

`record_video(path, frames, every, scale, layer)` / `./zx video p -o x.gif`
records `.gif`, `.mp4` (needs ffmpeg) or a directory of PNGs; a paused
session advances deterministically while recording.

## ROMs and the Next

ZEsarUX ships the Sinclair/Amstrad ROMs (Amstrad allows their
redistribution for emulator use), so nothing needs downloading. For the Next,
ZEsarUX loads `.nex` files directly (no SD card image or NextZXOS boot
needed): the harness does a hard reset and `smartload`. Programs that use
NextZXOS/esxDOS file APIs need an SD image (`--mmc-file`, ZEsarUX ships a
small `tbblue.mmc`); not set up in phase 1.
