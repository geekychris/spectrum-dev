"""zxdev command line (usually reached through ./zx).

  zxdev run <program> [--machine M] [--zoom Z]       window session (free-running)
  zxdev serve [program] [--window] [--mode paused|running]   session process
  zxdev test <program> [--frames N] [--machine M]    headless: pass on ZX:DONE, fail on ZX:FAIL
  zxdev log <program> [--frames N] [--press KEY@FRAME ...]
  zxdev shot <program> -o out.png [--until TEXT] [--frames N] [--press KEY@FRAME] [--no-border]
  zxdev ctl [--port N] <cmd> [key=value ...]        remote-control a session (ctl help)
  zxdev sessions                                     running sessions
  zxdev symbols <program> [pattern]
  zxdev devbench [--port N] [--open]                 web UI + REST + MCP over HTTP

<program> is a project name (newest build output) or a .tap/.nex/.sna/.z80/.zsf.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .emu import Emulator
from .session import resolve_program


def _presses(specs):
    """['SPACE@100', 'Q+A@200'] -> sorted [(frame, keys)]"""
    out = []
    for s in specs or []:
        keys, _, frame = s.rpartition("@")
        if not keys:
            raise SystemExit(f"--press wants KEYS@FRAME, got {s!r}")
        out.append((int(frame), keys))
    return sorted(out)


def _drive(emu: Emulator, frames: int, presses, until: str | None = None) -> bool:
    """Run `frames` frames from now (or until the log shows `until`), pressing
    keys at their frames (counted from now). Returns whether `until` was seen."""
    start = emu.frame()
    for at, keys in presses:
        n = start + at - emu.frame()
        if n > 0:
            if until:
                if emu.run_until(until, n):
                    return True
            else:
                emu.run(n)
        emu.press(keys)
    rest = max(0, start + frames - emu.frame())
    if until:
        return emu.run_until(until, rest)
    emu.run(rest)
    return True


def _machine_for(prog: str, machine: str | None) -> str:
    if machine:
        return machine
    return "next" if prog.lower().endswith((".nex", ".snx")) else "48k"


def cmd_test(a) -> int:
    prog = resolve_program(a.program)
    with Emulator(machine=_machine_for(prog, a.machine)) as emu:
        emu.load(prog)
        emu.run_until("ZX:", a.frames)
        text = emu.log(0)
    print(text, end="" if text.endswith("\n") else "\n")
    if "ZX:FAIL" in text:
        print(f"FAIL: {Path(prog).name}")
        return 1
    if "ZX:DONE" in text:
        print(f"PASS: {Path(prog).name}")
        return 0
    print(f"FAIL: {Path(prog).name}: no ZX:DONE within {a.frames} frames "
          "(add a test.py, or log ZX:DONE when your checks pass)")
    return 1


def cmd_log(a) -> int:
    prog = resolve_program(a.program)
    with Emulator(machine=_machine_for(prog, a.machine)) as emu:
        emu.load(prog)
        _drive(emu, a.frames, _presses(a.press))
        sys.stdout.write(emu.log(0))
    return 0


def cmd_shot(a) -> int:
    prog = resolve_program(a.program)
    with Emulator(machine=_machine_for(prog, a.machine)) as emu:
        emu.load(prog)
        found = _drive(emu, a.frames, _presses(a.press), a.until)
        if a.until and not found:
            print(f"warning: {a.until!r} not seen within {a.frames} frames", file=sys.stderr)
        emu.run(a.settle)
        img = emu.screenshot(a.output, border=not a.no_border, scale=a.scale)
    print(f"{a.output} ({img.width}x{img.height})")
    return 0 if found else 1


def cmd_ctl(a) -> int:
    from .control import ProtocolError, SessionClient, list_sessions
    port = a.port
    if port is None:
        ss = list_sessions()
        if not ss:
            print("no running session (./zx run <project>, or ./zx serve)", file=sys.stderr)
            return 1
        port = ss[-1]["control_port"]
    args = {}
    for kv in a.args:
        k, _, v = kv.partition("=")
        try:
            args[k] = json.loads(v)
        except json.JSONDecodeError:
            args[k] = v
    c = SessionClient(port)
    try:
        res = c.call(a.command, **args)
    except ProtocolError as ex:
        print(f"error: {ex}", file=sys.stderr)
        return 1
    if isinstance(res, dict) and "png_b64" in res:
        res = {k: v for k, v in res.items() if k != "png_b64"}
    if isinstance(res, dict) and len(res) <= 2 and any(k in res for k in ("listing", "dump", "text", "output")):
        for k in ("listing", "dump", "text", "output"):
            if k in res:
                print(res[k])
    else:
        print(json.dumps(res, indent=1))
    return 0


def cmd_sessions(a) -> int:
    from .control import list_sessions
    ss = list_sessions()
    if not ss:
        print("no sessions")
    for s in ss:
        print(f"pid {s['pid']:>6}  control :{s['control_port']}  zrcp :{s['zrcp_port']}  "
              f"{s['machine']:<6} {'window' if s['window'] else 'headless'} {s['mode']:<8} {s.get('program') or ''}")
    return 0


def cmd_symbols(a) -> int:
    from .symbols import Symbols
    prog = resolve_program(a.program)
    s = Symbols.load(Path(prog).with_suffix(".map"))
    for d in s.search(a.pattern or "", 10000):
        print(f"{d['hex']}  {d['name']}")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="zxdev", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("serve")
    p.add_argument("program", nargs="?")
    p.add_argument("--window", action="store_true")
    p.add_argument("--machine")
    p.add_argument("--mode", choices=["paused", "running"])
    p.add_argument("--port", type=int, default=0)
    p.add_argument("--zoom", type=int)
    p.add_argument("--quiet", action="store_true")

    p = sub.add_parser("run")
    p.add_argument("program", nargs="?")
    p.add_argument("--machine")
    p.add_argument("--port", type=int, default=0)
    p.add_argument("--zoom", type=int)

    for name in ("test", "log", "shot"):
        p = sub.add_parser(name)
        p.add_argument("program")
        p.add_argument("--machine")
        p.add_argument("--frames", type=int, default=3000 if name == "test" else 500)
        if name != "test":
            p.add_argument("--press", action="append", metavar="KEYS@FRAME")
        if name == "shot":
            p.add_argument("-o", "--output", default="shot.png")
            p.add_argument("--until")
            p.add_argument("--settle", type=int, default=2, help="frames to run after --until matches")
            p.add_argument("--no-border", action="store_true")
            p.add_argument("--scale", type=int, default=1)

    p = sub.add_parser("ctl")
    p.add_argument("--port", type=int)
    p.add_argument("command")
    p.add_argument("args", nargs="*")

    sub.add_parser("sessions")

    p = sub.add_parser("symbols")
    p.add_argument("program")
    p.add_argument("pattern", nargs="?")

    p = sub.add_parser("devbench")
    p.add_argument("--port", type=int, default=3580)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--open", action="store_true")

    a = ap.parse_args(argv)
    if a.cmd == "serve":
        from .session import serve
        serve(resolve_program(a.program), a.machine, a.window, a.mode, a.port, a.zoom, a.quiet)
        return 0
    if a.cmd == "run":
        from .session import serve
        prog = resolve_program(a.program)
        print("window session; ./zx ctl <cmd> drives it, Claude can emu_attach(). Ctrl-C or close the window to stop.")
        serve(prog, a.machine, True, "running", a.port, a.zoom)
        return 0
    if a.cmd == "devbench":
        from .devbench import main as devbench
        devbench(a.port, a.host, a.open)
        return 0
    return {"test": cmd_test, "log": cmd_log, "shot": cmd_shot, "ctl": cmd_ctl,
            "sessions": cmd_sessions, "symbols": cmd_symbols}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
