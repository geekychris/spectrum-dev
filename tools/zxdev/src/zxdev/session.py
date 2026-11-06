"""Session process: one Emulator + a JSON-lines control port + a registry entry.

    zxdev serve projects/hello48/build/hello48.tap [--window] [--mode running]

Clients (./zx ctl, the MCP server, DevBench, scripts) send one JSON object per
line and get one back:

    {"id": 1, "cmd": "press", "args": {"keys": "SPACE"}}
    {"id": 1, "ok": true, "result": {...}}

Commands run one at a time (a lock), so any number of clients can share a
session. The session also leaves ZEsarUX's own ZRCP port open (zrcp_port in
the registry), so ZRCP tools such as DeZog can attach alongside.
"""
from __future__ import annotations

import base64
import io
import json
import os
import signal
import socket
import socketserver
import sys
import threading
import time
from pathlib import Path

from .emu import LOADABLE, Emulator, EmulatorError, repo_root
from .zrcp import ZrcpError

SESSIONS_DIR = repo_root() / "build" / "sessions"


def resolve_program(target: str | None) -> str | None:
    """Project name or file path -> loadable file (newest .nex/.tap in build/)."""
    if not target:
        return None
    p = Path(target)
    if p.suffix.lower() in LOADABLE:
        if not p.exists():
            raise FileNotFoundError(f"{p} not found")
        return str(p.resolve())
    d = repo_root() / "projects" / target / "build"
    cands = [c for ext in (".nex", ".tap", ".sna", ".z80") for c in d.glob(f"*{ext}")]
    if not cands:
        raise FileNotFoundError(f"nothing built for project {target!r} (./zx build {target})")
    return str(max(cands, key=lambda c: c.stat().st_mtime))


def _hexdump(addr: int, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{(addr + i) & 0xFFFF:04X}  {chunk.hex(' '):<47}  {asc}")
    return "\n".join(lines)


class Session:
    def __init__(self, emu: Emulator, info: dict):
        self.emu = emu
        self.info = info
        self.lock = threading.RLock()
        self.quit = threading.Event()
        self.registry: Path | None = None

    # each command: (function, help)
    def commands(self):
        e = self.emu
        return {
            "help": (lambda: {k: v[1] for k, v in sorted(self.commands().items())}, "this list"),
            "status": (lambda: {**e.status(), "session": self.info}, "machine, frame, pc, program, ports"),
            "load": (lambda path, machine=None: e.load(resolve_program(path), machine),
                     "load(path|project, machine?): reset + smartload .tap/.nex/.sna/.z80/.zsf"),
            "reset": (lambda hard=True: (e.reset(hard), e.status())[1], "reset(hard=true): reset the machine"),
            "machine": (lambda name=None: (e.set_machine(name), e.status())[1] if name else {"machine": e.machine},
                        "machine(name): 48k 128k plus2 plus2a plus3 pentagon next"),
            "pause": (lambda: (e.pause(), e.status())[1], "stop free-running"),
            "resume": (lambda turbo=False: (e.resume(turbo), e.status())[1], "resume(turbo=false): free-run"),
            "run": (lambda frames=1: e.run(frames), "run(frames): advance N frames (stops at breakpoints)"),
            "run_until": (lambda text, max_frames=3000, since=None: {
                "found": e.run_until(text, max_frames, since), **e.last_run},
                "run_until(text, max_frames=3000, since?): run until the guest log contains text"),
            "log": (lambda since=None, all=False: {
                "text": "\n".join(e.debug_log) if all else e.log(since), "cursor": e.log_cursor()},
                "log(since?, all=false): guest log since the last log call"),
            "log_cursor": (lambda: {"cursor": e.log_cursor()}, "current end of the log"),
            "press": (lambda keys, hold_frames=3, release_frames=3: e.press(keys, hold_frames, release_frames),
                      "press(keys, hold_frames=3, release_frames=3): e.g. 'SPACE', 'Q+A', 'UP+FIRE'"),
            "hold": (lambda keys: (e.hold(keys), {"held": e._held})[1], "hold(keys): press and keep held"),
            "release": (lambda keys=None: (e.release(keys), {"held": e._held})[1], "release(keys?): release (all)"),
            "type": (lambda text, hold_frames=3, gap_frames=3: e.type(text, hold_frames, gap_frames),
                     "type(text): key by key; \\n = ENTER"),
            "joystick": (lambda kind=None: {"joystick": e.joystick(kind)},
                         "joystick(kind?): Kempston, Sinclair 1, Sinclair 2, Cursor, Fuller, None"),
            "sequence": (lambda steps: e.sequence(steps),
                         "sequence(steps): [{press,hold}, {run}, {until,max}, {type}, {hold}, {release}]"),
            "screenshot": (self._screenshot, "screenshot(path?, border=true, scale=1): PNG (png_b64)"),
            "regs": (lambda: {**e.regs(), "pc_symbol": e.symbols.name_of(e.regs()["PC"])}, "CPU registers"),
            "set_reg": (lambda reg, value: e.set_reg(reg, value), "set_reg(reg, value)"),
            "read_mem": (self._read_mem, "read_mem(addr, length=64): hex + dump; addr may be a symbol"),
            "write_mem": (self._write_mem, "write_mem(addr, hex)"),
            "find_mem": (self._find_mem, "find_mem(hex|text, start=0, end=65536)"),
            "disasm": (lambda addr="pc", count=12: {"listing": "\n".join(e.disasm(addr, count))},
                       "disasm(addr=pc, count=12)"),
            "symbols": (lambda pattern="", limit=200: e.symbols.search(pattern, limit),
                        "symbols(pattern): from the program's .map"),
            "addr2sym": (lambda addr: {"addr": e.addr(addr), "symbol": e.symbols.name_of(e.addr(addr))},
                         "addr2sym(addr)"),
            "break": (lambda addr=None, condition=None: e.break_if(condition) if condition else e.break_at(addr),
                      "break(addr|condition): e.g. 'main', '$8000', condition='A=5 AND PC=32768'"),
            "watch": (lambda addr, length=1, kind="w": e.watch(addr, length, kind),
                      "watch(addr, length=1, kind=r|w|rw)"),
            "delete": (lambda index=None, addr=None: (e.delete(index, addr), e.breakpoints())[1],
                       "delete(index?|addr?): no args = everything"),
            "breakpoints": (lambda: e.breakpoints(), "list breakpoints and watchpoints"),
            "continue": (lambda max_frames=500: e.continue_(max_frames),
                         "continue(max_frames=500): run until a breakpoint/watchpoint"),
            "stepi": (lambda count=1: e.stepi(count), "stepi(count=1)"),
            "step_over": (lambda: e.step_over(), "step over CALL/RST"),
            "backtrace": (lambda items=8: e.backtrace(items), "backtrace(items=8): stack words + symbols"),
            "save_state": (lambda path: {"path": e.save_state(self._state_path(path))},
                           "save_state(path|name): .zsf (default), .z80, .sna"),
            "load_state": (lambda path: e.load_state(self._state_path(path)), "load_state(path|name)"),
            "zrcp": (lambda line, timeout=30: {"output": e.raw(line, timeout)},
                     "zrcp(line): raw ZEsarUX command (zrcp('help'), zrcp('zxh-help'))"),
            "quit": (self._quit, "stop the session and the emulator"),
        }

    def _state_path(self, p) -> str:
        p = Path(p)
        return str(p if p.is_absolute() or p.parent != Path(".") else repo_root() / "build" / "states" / p)

    def _screenshot(self, path=None, border=True, scale=1):
        img = self.emu.screenshot(path, border, scale)
        buf = io.BytesIO()
        img.save(buf, "PNG")
        return {"png_b64": base64.b64encode(buf.getvalue()).decode(), "width": img.width,
                "height": img.height, "path": path, "colors": len(img.getcolors(1 << 16) or [])}

    def _read_mem(self, addr, length=64):
        a = self.emu.addr(addr)
        data = self.emu.read_mem(a, int(length))
        return {"addr": a, "hex": data.hex(), "dump": _hexdump(a, data)}

    def _write_mem(self, addr, hex):
        data = bytes.fromhex(hex.replace(" ", ""))
        self.emu.write_mem(addr, data)
        return {"addr": self.emu.addr(addr), "written": len(data)}

    def _find_mem(self, hex=None, text=None, start=0, end=0x10000):
        needle = bytes.fromhex(hex.replace(" ", "")) if hex else text.encode("latin-1")
        return {"matches": [{"addr": a, "symbol": self.emu.symbols.name_of(a)}
                            for a in self.emu.find_mem(needle, self.emu.addr(start), int(end))]}

    def _quit(self):
        self.quit.set()
        return {"bye": True}

    def call(self, cmd: str, args: dict):
        table = self.commands()
        if cmd not in table:
            raise ValueError(f"unknown command {cmd!r} (try help)")
        with self.lock:
            try:
                return table[cmd][0](**(args or {}))
            finally:
                # keep the registry current (a window session restarts ZEsarUX to switch machines)
                now = {"zrcp_port": self.emu.port, "machine": self.emu.machine,
                       "emulator_pid": self.emu.proc.pid, "program": self.emu.program}
                if any(self.info.get(k) != v for k, v in now.items()):
                    self.info.update(now)
                    if self.registry:
                        self.registry.write_text(json.dumps(self.info))


class _Handler(socketserver.StreamRequestHandler):
    def handle(self):
        sess: Session = self.server.session
        for raw in self.rfile:
            try:
                req = json.loads(raw)
            except json.JSONDecodeError as ex:
                self._send({"ok": False, "error": f"bad json: {ex}"})
                continue
            rid = req.get("id")
            try:
                res = sess.call(req.get("cmd", ""), req.get("args") or {})
                self._send({"id": rid, "ok": True, "result": res})
            except (EmulatorError, ZrcpError, ValueError, TypeError, KeyError, FileNotFoundError) as ex:
                self._send({"id": rid, "ok": False, "error": f"{type(ex).__name__}: {ex}"})
            if sess.quit.is_set():
                return

    def _send(self, obj):
        self.wfile.write((json.dumps(obj) + "\n").encode())
        self.wfile.flush()


class _Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(program: str | None = None, machine: str | None = None, window: bool = False,
          mode: str | None = None, port: int = 0, zoom: int | None = None, quiet: bool = False):
    paused = (mode or ("running" if window else "paused")) == "paused"
    if machine is None:
        machine = "next" if program and program.lower().endswith((".nex", ".snx")) else "48k"
    SESSIONS_DIR.mkdir(parents=True, exist_ok=True)
    for old in sorted(SESSIONS_DIR.glob("zesarux-*.log"), key=lambda p: p.stat().st_mtime)[:-20]:
        old.unlink(missing_ok=True)   # keep the 20 newest emulator logs
    emu = Emulator(machine=machine, window=window, paused=True, zoom=zoom,
                   log_path=SESSIONS_DIR / f"zesarux-{os.getpid()}.log")
    if program:
        emu.load(program)
    if not paused:
        emu.resume()
    srv = _Server(("127.0.0.1", port), _Handler)
    info = {"pid": os.getpid(), "control_port": srv.server_address[1], "zrcp_port": emu.port,
            "program": program, "machine": emu.machine, "window": window,
            "mode": "paused" if paused else "running", "started": time.time(),
            "emulator_pid": emu.proc.pid}
    sess = Session(emu, info)
    srv.session = sess
    reg = SESSIONS_DIR / f"{os.getpid()}.json"
    reg.write_text(json.dumps(info))
    sess.registry = reg
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print("ZXDEV-SESSION " + json.dumps(info), flush=True)
    if not quiet:
        print(f"session pid {info['pid']}: control :{info['control_port']}  ZRCP :{info['zrcp_port']}  "
              f"({'window' if window else 'headless'}, {info['mode']})", file=sys.stderr, flush=True)

    def _stop(*_):
        sess.quit.set()
    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    try:
        # exit when told to, or when the emulator goes away (window closed)
        while not sess.quit.wait(0.5):
            # (under the lock: a window session restarts ZEsarUX inside a command)
            if sess.lock.acquire(blocking=False):
                gone = not emu.alive
                sess.lock.release()
                if gone:
                    break
    finally:
        reg.unlink(missing_ok=True)
        srv.shutdown()
        with sess.lock:
            emu.close()
