"""zxdev command line (usually reached through ./zx).

  zxdev run <program> [--machine M] [--zoom Z]       window session (free-running)
  zxdev serve [program] [--window] [--mode paused|running]   session process
  zxdev arcade [--no-build] [--image-only] [--list]         the game menu on an SD image
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


def _timed(a) -> list[tuple[int, str, object]]:
    """--press/--hold/--release/--type KEYS@FRAME and --script file -> sorted
    [(frame since load, action, arg)]."""
    out = []
    for kind in ("press", "hold", "release", "type"):
        for spec in getattr(a, kind, None) or []:
            arg, _, frame = spec.rpartition("@")
            if not arg or not frame.isdigit():
                raise SystemExit(f"--{kind} wants VALUE@FRAME, got {spec!r}")
            out.append((int(frame), kind, arg))
    if getattr(a, "script", None):
        for st in json.loads(Path(a.script).read_text()):
            out.append((int(st.get("at", 0)), "step", st))
    return sorted(out, key=lambda x: x[0])


def _drive(emu: Emulator, frames: int, actions, until: str | None = None) -> bool:
    """Run to `frames` frames since load (or until the log shows `until`),
    doing the timed actions at their frames. Returns whether `until` was seen."""
    for at, kind, arg in actions:
        if until:
            n = at - emu.position()["since_load"]
            if n > 0 and emu.run_until(until, n):
                return True
        emu.run_to(at)
        if kind == "press":
            emu.press(arg)
        elif kind == "hold":
            emu.hold(arg)
        elif kind == "release":
            emu.release(None if arg in ("", "ALL", "all") else arg)
        elif kind == "type":
            emu.type(arg.replace("\\n", "\n"))
        else:
            emu.sequence([{k: v for k, v in arg.items() if k != "at"}])
    rest = frames - emu.position()["since_load"]
    if until:
        return emu.run_until(until, max(rest, 0))
    if rest > 0:
        emu.run_to(frames)
    return True


def _finish(emu: Emulator, a):
    if getattr(a, "record", None):
        rec = emu.save_replay(a.record)
        print(f"replay saved: {a.record} ({len(rec['events'])} input events)", file=sys.stderr)


def _machine_for(prog: str, machine: str | None) -> str:
    if machine:
        return machine
    return "next" if prog.lower().endswith((".nex", ".snx")) else "48k"


def cmd_test(a) -> int:
    prog = resolve_program(a.program)
    with Emulator(machine=_machine_for(prog, a.machine)) as emu:
        emu.load(prog)
        if emu.run_until("ZX:", a.frames):
            emu.run(2)                    # let it finish the line ("ZX:DONE")
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
        _drive(emu, a.frames, _timed(a))
        sys.stdout.write(emu.log(0))
        _finish(emu, a)
    return 0


def cmd_shot(a) -> int:
    prog = resolve_program(a.program)
    with Emulator(machine=_machine_for(prog, a.machine)) as emu:
        emu.load(prog)
        found = _drive(emu, a.frames, _timed(a), a.until)
        if a.until and not found:
            print(f"warning: {a.until!r} not seen within {a.frames} frames", file=sys.stderr)
        emu.run(a.settle)
        img = emu.screenshot(a.output, border=not a.no_border, scale=a.scale, layer=a.layer)
        _finish(emu, a)
    print(f"{a.output} ({img.width}x{img.height})")
    return 0 if found else 1


def cmd_video(a) -> int:
    prog = resolve_program(a.program)
    with Emulator(machine=_machine_for(prog, a.machine)) as emu:
        emu.load(prog)
        if a.until and not emu.run_until(a.until, 5000):
            print(f"warning: {a.until!r} not seen", file=sys.stderr)
        r = emu.record_video(a.output, a.frames, a.every, a.scale, a.layer)
    print(f"{r['path']} ({r['frames']} frames, {r['fps']:g} fps, {r['size'][0]}x{r['size'][1]})")
    return 0


def cmd_record(a) -> int:
    """Record a scripted run from the load (agent input) as a v2 recording."""
    prog = resolve_program(a.program)
    with Emulator(machine=_machine_for(prog, a.machine)) as emu:
        emu.load(prog)
        # from the reset: a snapshot taken while a tape is loading misses the tape state
        emu.record_input("start", from_reset=True)
        _drive(emu, a.frames, _timed(a))
        r = emu.record_input("save", a.output)
    print(f"{r['path']}: {r['events']} input changes, {r['frames']} frames, {r['hashes']} frame hashes")
    return 0


def cmd_replay(a) -> int:
    rec = json.loads(Path(a.file).read_text())
    if rec.get("version") == 2:
        digests = []
        ok = True
        for i in range(a.runs):
            with Emulator(machine=rec.get("machine") or "48k") as emu:
                r = emu.replay(a.file, verify=True)
                if a.output and i == 0:
                    emu.screenshot(a.output)
            seq = [h for _, h in r["hashes"]]
            digests.append(seq)
            print(f"  {'PASS' if r['ok'] else 'FAIL'} run {i + 1}: {r.get('compared', 0)} frame hashes compared, "
                  f"{r.get('mismatches', 0)} mismatches"
                  + (f" (first at frame {r['first_mismatch']})" if r.get("first_mismatch") is not None else ""))
            ok = ok and r["ok"]
        if a.runs > 1:
            same = all(d == digests[0] for d in digests)
            print(f"  {'PASS' if same else 'FAIL'} {a.runs} replays give identical hash sequences ({len(digests[0])} frames)")
            ok = ok and same
        print(f"{'PASS' if ok else 'FAIL'}: replay of {Path(rec['program']).name}, {len(rec['events'])} input changes")
        return 0 if ok else 1
    with Emulator(machine=rec.get("machine") or "48k") as emu:
        r = emu.replay(rec, verify=True)
        if a.output:
            emu.screenshot(a.output)
    for c in r["checkpoints"]:
        print(f"  {'PASS' if c['ok'] else 'FAIL'} {c['label']} at frame {c['f']} t={c['t']}"
              + ("" if c["ok"] else f"  (expected {c['expected'][:8]}, got {c['got'][:8]})"))
    print(f"{'PASS' if r['ok'] else 'FAIL'}: replay of {Path(rec['program']).name}, {r['events']} input events")
    return 0 if r["ok"] else 1


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


def cmd_arcade(a) -> int:
    from . import arcade
    try:
        if a.list:
            for g in arcade.games():
                print(f"{g['project']:<16} {g['machine']:<5} {g['title']:<20} {g['controls']}")
            return 0
        print("arcade: building the games, the menu and the SD image")
        m = arcade.build(do_build=not a.no_build, log=print)
    except arcade.ArcadeError as ex:
        print(f"arcade: {ex}", file=sys.stderr)
        return 1
    if a.image_only:
        return 0
    from .session import serve
    print("window session: NextZXOS boots into the menu (reset to come back from a game).")
    serve(m["image"], "next", True, "running", 0, a.zoom)
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
    p.add_argument("--sd-scratch", action="store_true", help="an SD image's writes aren't saved to the file")

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
            p.add_argument("--press", action="append", metavar="KEYS@FRAME",
                           help="press+release at a frame since load (e.g. SPACE@200, CAPS+1@300)")
            p.add_argument("--hold", action="append", metavar="KEYS@FRAME")
            p.add_argument("--release", action="append", metavar="KEYS@FRAME", help="ALL@FRAME releases everything")
            p.add_argument("--type", action="append", metavar="TEXT@FRAME")
            p.add_argument("--script", help="JSON list of sequence steps with \"at\" frames")
            p.add_argument("--record", metavar="REPLAY.json", help="save the input log as a replay")
        if name == "shot":
            p.add_argument("-o", "--output", default="shot.png")
            p.add_argument("--until")
            p.add_argument("--settle", type=int, default=2, help="frames to run after --until matches")
            p.add_argument("--no-border", action="store_true")
            p.add_argument("--scale", type=int, default=1)
            p.add_argument("--layer", default="screen", choices=["screen", "ula", "layer2", "sprites"])

    p = sub.add_parser("video")
    p.add_argument("program")
    p.add_argument("-o", "--output", default="video.gif", help=".gif, .mp4 (ffmpeg) or a directory")
    p.add_argument("--machine")
    p.add_argument("--until", help="start recording when the log shows this")
    p.add_argument("--frames", type=int, default=150)
    p.add_argument("--every", type=int, default=1)
    p.add_argument("--scale", type=int, default=1)
    p.add_argument("--layer", default="screen", choices=["screen", "ula", "layer2", "sprites"])

    p = sub.add_parser("replay")
    p.add_argument("file")
    p.add_argument("-o", "--output", help="screenshot at the end")
    p.add_argument("--runs", type=int, default=1, help="replay N times and require identical hash sequences")

    p = sub.add_parser("record")
    p.add_argument("program")
    p.add_argument("-o", "--output", default="recording.json")
    p.add_argument("--machine")
    p.add_argument("--frames", type=int, default=500)
    p.add_argument("--press", action="append", metavar="KEYS@FRAME")
    p.add_argument("--hold", action="append", metavar="KEYS@FRAME")
    p.add_argument("--release", action="append", metavar="KEYS@FRAME")
    p.add_argument("--type", action="append", metavar="TEXT@FRAME")
    p.add_argument("--script")

    p = sub.add_parser("ctl")
    p.add_argument("--port", type=int)
    p.add_argument("command")
    p.add_argument("args", nargs="*")

    sub.add_parser("sessions")

    p = sub.add_parser("symbols")
    p.add_argument("program")
    p.add_argument("pattern", nargs="?")

    p = sub.add_parser("arcade", help="build every game + the menu into an SD image and open it")
    p.add_argument("--no-build", action="store_true", help="use what is built")
    p.add_argument("--image-only", action="store_true", help="make the image, don't open a window")
    p.add_argument("--list", action="store_true", help="list the registered games")
    p.add_argument("--zoom", type=int)

    p = sub.add_parser("devbench")
    p.add_argument("--port", type=int, default=3580)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--open", action="store_true")

    a = ap.parse_args(argv)
    if a.cmd == "serve":
        from .session import serve
        serve(resolve_program(a.program), a.machine, a.window, a.mode, a.port, a.zoom, a.quiet, a.sd_scratch)
        return 0
    if a.cmd == "run":
        from .session import serve
        prog = resolve_program(a.program)
        print("window session; ./zx ctl <cmd> drives it, Claude can emu_attach(). Ctrl-C or close the window to stop.")
        serve(prog, a.machine, True, "running", a.port, a.zoom)
        return 0
    if a.cmd == "arcade":
        return cmd_arcade(a)
    if a.cmd == "devbench":
        from .devbench import main as devbench
        devbench(a.port, a.host, a.open)
        return 0
    return {"test": cmd_test, "log": cmd_log, "shot": cmd_shot, "ctl": cmd_ctl,
            "video": cmd_video, "replay": cmd_replay, "record": cmd_record,
            "sessions": cmd_sessions, "symbols": cmd_symbols}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
