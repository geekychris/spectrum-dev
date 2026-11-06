# Our ZEsarUX patches

Upstream: [chernandezba/zesarux](https://github.com/chernandezba/zesarux),
pinned in `scripts/lib.sh` (`ZESARUX_COMMIT`, tag `ZEsarUX-13.0`).
`scripts/build-emulator.sh` fetches that commit into `third_party/zesarux`,
resets it, copies `emulator/ext/zxh.{c,h}` to `src/zrcp/`, applies these
patches in order, configures and builds `third_party/zesarux/src/zesarux`.

ZEsarUX already has a remote protocol (ZRCP, TCP) with memory, registers,
breakpoints, memory breakpoints, single step, snapshots, `smartload`,
machine selection, keyboard-matrix and joystick injection
(`set-ui-io-ports`) and screenshots of the rendered frame (`save-screen`).
We keep all of that and add what agents need on top. The patches only add
hook calls; the logic lives in `emulator/ext/zxh.c`.

| Patch | What |
|---|---|
| `0001-configure-homebrew-sdl2.patch` | `configure` on macOS only knew the SDL2 *framework*; use `sdl2-config` (Homebrew/MacPorts) when present. |
| `0002-zxh-harness-hooks.patch` | ~35 added lines: calls into `zxh.c` (below), `#include "zxh.c"` at the end of `zrcp/remote.c`, and dispatch of `zxh-*` commands. |
| `0003-sdl2-macos-title-main-thread.patch` | `smartload` over ZRCP sets the window title from the network thread; AppKit aborts. Defer the title update to the SDL event loop (main thread). |

## Hooks (0002)

| Where | Hook | Why |
|---|---|---|
| `ula.c` ZXI register 1 write | `zxh_debug_char(c)` | Guest debug log: `OUT (0xCF3B),1` selects ZXI register 1, each `OUT (0xDF3B),c` is one character. Captured into a 4 MB ring (absolute offsets, oldest half dropped when full). Needs `--hardware-debug-ports` (our launcher passes it). |
| `core_spectrum.c` end of frame | `zxh_end_frame()` | Frame counter. In *nowait* mode also marks the host timer as already fired, so the core never sleeps. |
| `timer.c` `timer_check_interrupt` | `zxh_timer_tick()` | In nowait mode, ZEsarUX's 20 ms housekeeping tick comes from emulated frames instead of the host clock (deterministic). Also releases a core that was waiting for the host timer when nowait started (this was a deadlock when grabbing a free-running CPU). |
| `screen.c` `screen_if_refresh` | `zxh_force_render()` | In nowait mode and while frames are hashed, every frame is rendered (no frameskip), so screenshots and hashes never see a skipped frame. |
| `cores/core_spectrum.c` start of each core-loop iteration | `zxh_input_poll()` | Input recorder: while `zxh-rec` is on, compares the 12-byte input state (8 half-rows, joystick bits, joystick type, NextReg 0xB0/0xB1) with the last one and logs changes with frame + T-state. Catches every source: ZRCP injection and the host keyboard/joystick in a window. |
| `machines/tbblue.c` NextReg read | `zxh_next_extkeys(reg)` | ZEsarUX doesn't emulate the Next's extended-key registers 0xB0/0xB1; they return what `zxh-nextkeys` set (the harness sets them with the matrix combo when a Next extra key is pressed). |
| `core_spectrum.c` 50 Hz input | `zxh_host_io_allowed()` | True only on the main thread. ZRCP runs the core on its network thread (`run`, `cpu-step`, our `zxh-run`); pumping SDL events or presenting the window from there aborts on macOS. The rendered frame (rainbow buffer) is still produced; the window catches up when the CPU is released. |
| `screen.c` window refresh | `zxh_window_refresh()` | Same main-thread rule, and in turbo only 1 frame in 8 is presented (window presentation otherwise caps fast-forward at ~120 fps; with it ~770). |
| `core_spectrum.c` end of frame (via `zxh_end_frame`) | per-frame hash | While `zxh-hash on`, an FNV-1a hash of the rendered frame is stored per frame. |

Nothing changes unless a `zxh-*` command is used, except that host
keyboard/window refresh no longer happen while ZRCP drives the core (they
would crash on macOS, and are meaningless headless).

## Commands added (zxh.c)

| Command | Reply |
|---|---|
| `zxh-status` | `frames=N tstates=T log=END nowait=0/1 step=0/1 pc=XXXX menu=0/1 brk=0/1 pid=PID` (the PID lets the host check it is talking to the emulator it started) |
| `zxh-run frames=N [until=HEX] [since=OFF] [at=F:T]` | cpu-step mode only. Runs up to N whole frames as fast as possible. Stops early when the log (after absolute offset OFF; default: the log end at call time) contains the bytes HEX, *on the instruction that wrote the last byte*; when a breakpoint/watchpoint fires; or at the first instruction boundary at/after frame F, T-state T (what replays use to re-apply input at the exact recorded point). `frames=RAN total=N tstates=T reason=frames/log/break/at pc=XXXX log=END [match=OFF] [break=MSG]` |
| `zxh-log [since] [max]` | `base=B end=E next=X since=S`, newline, the bytes `[S, X)` as hex (max default 32768) |
| `zxh-log-clear` | forget buffered text (offsets keep counting) |
| `zxh-turbo 0/1` | free-run without host pacing |
| `zxh-joystick [type]` | get/set the emulated joystick (`Kempston`, `Sinclair 1`, `Sinclair 2`, `Cursor`, `Fuller`, `None`…) |
| `zxh-nextkeys [B0 B1]` | get/set the Next extended-key registers (hex bytes) |
| `zxh-rec start/stop/status/log [since]` | input recorder; `log` lines are `FRAME TSTATE HEX24` (the 12-byte state) |
| `zxh-input-set HEX24` | set the whole input state (replay) |
| `zxh-hash on/off/clear/now/status/get [since]` | per-frame hashes; `get` lines are `FRAME HASH` |
| `zxh-tstates [T L D]` | get/set the beam position (T-state, scanline, draw line). Snapshots, even `.zsf`, don't store it and a load restarts the frame at T-state 0; `save_state` keeps it in `<file>.beam` and `load_state` restores it |
| `zxh-sync [ram]` | reset the FLASH phase, MEMPTR, the frame buffer and (ram) zero RAM. ZEsarUX's hard reset leaves RAM and most registers alone (for a plain Spectrum it does nearly nothing), so without this a boot depended on what ran before; the wrapper also sets every register to fixed power-on values |
| `zxh-help` | list |

Speed, headless on an M-series Mac: about 2200 frames/s on a 48K (44x real
time) and 450-500 frames/s on the Next (9x; the Next renders at 2x
resolution and runs up to 28 MHz).

## Determinism

With the CPU held in cpu-step mode and `zxh-run`, emulation is a pure
function of the state and inputs: the Z80 interrupt comes from the T-state
count, the 20 ms housekeeping from frames, and input only changes between
commands. Two fresh emulators loading the same program reach the same log
line on the same frame with the same registers (`projects/hello48/test.py`
checks this), and a recorded input log replays to identical screen hashes
(`tools/zxdev/tests/test_features.py`). Free-running (window) sessions are
paced by the host clock and are not deterministic, but input injected into
them is logged with its exact frame/T-state, so a replay of it is.

## Updating ZEsarUX

1. Change `ZESARUX_COMMIT` in `scripts/lib.sh`.
2. `scripts/build-emulator.sh`; if a patch no longer applies, fix it in
   `third_party/zesarux` and regenerate it there with
   `git diff -- <files> > ../../emulator/patches/000N-....patch`.
3. `./zx selftest` (and `ZX_TEST_WINDOW=1 .venv/bin/python tools/zxdev/tests/test_remote.py` for the window path).
