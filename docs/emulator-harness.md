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
| session | `status` `load(path, machine)` `reset(hard)` `machine(name)` `pause` `resume(turbo)` `quit` |
| running | `run(frames)` `run_until(text, max_frames, since)` `log(since, all)` `log_cursor` |
| input | `press(keys, hold_frames, release_frames)` `hold(keys)` `release(keys)` `type(text)` `joystick(kind)` `sequence(steps)` |
| screen | `screenshot(path, border, scale)` (PNG, base64 in `png_b64`) |
| memory | `read_mem(addr, length)` `write_mem(addr, hex)` `find_mem(hex/text)` |
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

**Keys:** `A`–`Z`, `0`–`9`, `ENTER`, `SPACE`, `CAPS` (Caps Shift), `SYM`
(Symbol Shift); combine with `+` (`CAPS+1`, `SYM+P`). **Joystick:** `UP`
`DOWN` `LEFT` `RIGHT` `FIRE`, mapped onto the emulated joystick (Kempston by
default; `joystick kind="Sinclair 1"` etc.). Input goes straight into the
keyboard matrix (`set-ui-io-ports`), so it works with any key-reading code
(ROM, `in_inkey`, direct port reads). In a window session the host keyboard
feeds the same matrix, and ZEsarUX clears it on window focus changes.

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

## Screenshots

`screenshot` saves what the display shows (ZEsarUX's rendered frame,
including border and Next layers). 48K/128K: 352x304 (paper 256x192 at
48,56). Next: 704x608 — ZEsarUX renders the Next at 2x so 640-wide modes fit;
`border=false` crops to the paper area (256x192 or 512x384), `scale` enlarges.

## ROMs and the Next

ZEsarUX ships the Sinclair/Amstrad ROMs (Amstrad allows their
redistribution for emulator use), so nothing needs downloading. For the Next,
ZEsarUX loads `.nex` files directly (no SD card image or NextZXOS boot
needed): the harness does a hard reset and `smartload`. Programs that use
NextZXOS/esxDOS file APIs need an SD image (`--mmc-file`, ZEsarUX ships a
small `tbblue.mmc`); not set up in phase 1.
