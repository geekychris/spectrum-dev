"""Emulator: one patched ZEsarUX process driven over ZRCP.

    from zxdev.emu import Emulator
    with Emulator(machine="48k") as emu:           # headless, paused
        emu.load("projects/hello48/build/hello48.tap")
        assert emu.run_until("hello48: ready", 1500)
        emu.press("SPACE")
        emu.screenshot("out.png")

Paused emulators only advance when told to (run/run_until/press/continue),
deterministically and as fast as the host allows (the zxh patch removes host
pacing while it runs). A window emulator free-runs in real time; the
deterministic commands briefly hold its CPU, fast-forward, and let it go.

A frame is 1/50 s of guest time (69888 T-states on a 48K).
"""
from __future__ import annotations

import os
import shutil
import socket
import subprocess
import tempfile
import time
from contextlib import contextmanager
from pathlib import Path

from . import keys as K
from .symbols import Symbols
from .zrcp import Zrcp, ZrcpError, kv, kv_tail

# our names -> ZEsarUX --machine ids
MACHINES = {
    "16k": "16k", "48k": "48k", "48k+": "48kp", "128k": "128k",
    "plus2": "P2", "plus2a": "P2A41", "plus3": "P341",
    "pentagon": "Pentagon", "next": "TBBlue",
}
MACHINE_ALIASES = {
    "zx": "48k", "48": "48k", "spectrum": "48k", "128": "128k", "+2": "plus2",
    "+2a": "plus2a", "+3": "plus3", "p3": "plus3", "zxn": "next", "tbblue": "next",
    "spectrum-next": "next",
}
# reported by get-current-machine -> our name
_MACHINE_NAMES = {
    "ZX Spectrum 16k": "16k", "ZX Spectrum 48k": "48k", "ZX Spectrum+ 48k": "48k+",
    "ZX Spectrum+ 128k": "128k", "ZX Spectrum +2": "plus2", "ZX Spectrum +2A (ROM v4.1)": "plus2a",
    "ZX Spectrum +3 (ROM v4.1)": "plus3", "Pentagon": "pentagon", "ZX Spectrum Next": "next",
}

LOADABLE = (".tap", ".tzx", ".pzx", ".sna", ".z80", ".zsf", ".nex", ".snx", ".sp", ".dsk", ".trd")


def repo_root() -> Path:
    env = os.environ.get("ZX_ROOT")
    if env:
        return Path(env)
    return Path(__file__).resolve().parents[4]


def emulator_binary() -> Path:
    return Path(os.environ.get("ZX_EMULATOR", repo_root() / "third_party/zesarux/src/zesarux"))


def machine_id(name: str) -> str:
    n = MACHINE_ALIASES.get(name.lower(), name.lower())
    if n not in MACHINES:
        raise ValueError(f"unknown machine {name!r}; one of {', '.join(MACHINES)}")
    return n


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class EmulatorError(RuntimeError):
    pass


class Emulator:
    def __init__(self, machine: str = "48k", window: bool = False, paused: bool | None = None,
                 port: int | None = None, zoom: int | None = None, audio: bool | None = None,
                 joystick: str = "Kempston", extra_args=(), log_path=None):
        self.machine = machine_id(machine)
        self.window = window
        self.paused = (not window) if paused is None else paused
        self.port = port or free_port()
        self.program: str | None = None
        self.load_frame = 0           # frame counter when the program was loaded
        self.symbols = Symbols()
        self.mark = 0                 # run_until/log default "since" offset
        self._logtext = bytearray()   # all guest log bytes fetched so far
        self._logbase = 0             # absolute offset of _logtext[0]
        self._held: list[str] = []
        self._bps: dict[int, dict] = {}
        self._watches: dict[int, dict] = {}
        self._tmp = Path(tempfile.mkdtemp(prefix="zxdev-"))
        self.log_path = Path(log_path) if log_path else self._tmp / "zesarux.log"
        self.z: Zrcp | None = None

        self._opts = {"zoom": zoom, "audio": audio, "joystick": joystick, "extra": list(extra_args)}
        self._out = open(self.log_path, "ab")
        self._start()

    def _start(self):
        exe = emulator_binary()
        if not exe.exists():
            raise EmulatorError(f"emulator not built: {exe} (run ./zx setup or scripts/build-emulator.sh)")
        o = self._opts
        args = [str(exe), "--noconfigfile",
                "--machine", MACHINES[self.machine],
                "--realvideo", "--hardware-debug-ports",
                "--enable-remoteprotocol", "--remoteprotocol-port", str(self.port),
                "--nowelcomemessage", "--nosplash", "--quickexit",
                "--joystickemulated", o["joystick"]]
        if self.window:
            args += ["--vo", "sdl", "--ao", "sdl" if o["audio"] is not False else "null"]
            if o["zoom"]:
                args += ["--zoom", str(o["zoom"])]
        else:
            args += ["--vo", "null", "--ao", "null", "--disablerealjoystick"]
        args += o["extra"]
        self.proc = subprocess.Popen(args, stdout=self._out, stderr=subprocess.STDOUT,
                                     stdin=subprocess.DEVNULL, start_new_session=True)
        try:
            self.z = Zrcp(self.port)
        except ZrcpError:
            self.close()
            raise EmulatorError(f"ZEsarUX did not open ZRCP on :{self.port}; see {self.log_path}") from None
        if self.paused:
            self._enter_step()
            # same starting state every time, whatever ran before we got the CPU
            self.z.cmd("hard-reset-cpu")
        self.mark = self.log_end()

    def _stop_process(self):
        if self.z is not None:
            try:
                self.z.sock.sendall(b"exit-emulator\n")
            except OSError:
                pass
            self.z.close()
            self.z = None
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    # ------------------------------------------------------------- lifecycle

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    @property
    def alive(self) -> bool:
        return self.proc.poll() is None

    def close(self):
        self._stop_process()
        self._out.close()
        shutil.rmtree(self._tmp, ignore_errors=True)

    def _cmd(self, line: str, timeout: float = 30.0) -> str:
        if self.z is None or not self.alive:
            raise EmulatorError(f"emulator has exited (log: {self.log_path})")
        return self.z.cmd(line, timeout=timeout)

    def raw(self, line: str, timeout: float = 30.0) -> str:
        """Any ZRCP command (`help` lists them; zxh-help lists ours)."""
        return self._cmd(line, timeout=timeout)

    @contextmanager
    def held(self):
        """Hold the CPU (ZRCP cpu-step mode) for the duration, if it isn't already."""
        if self.z is not None and self.z.step_mode:
            yield
            return
        self._enter_step()
        try:
            yield
        finally:
            if not self.paused:
                self._cmd("exit-cpu-step")

    def _enter_step(self):
        # Some operations (set-machine) leave ZEsarUX's menu open, which
        # blocks cpu-step mode; there is no menu to show us here, so close it.
        out = self.z.cmd("enter-cpu-step", check=False)
        if not self.z.step_mode:
            self._cmd("close-all-menus")
            self._cmd("enter-cpu-step")
            if not self.z.step_mode:
                raise EmulatorError(f"cannot hold the CPU: {out.strip()}")

    def pause(self):
        """Stop free-running (frames then only advance on run/press/continue)."""
        self.paused = True
        if not self.z.step_mode:
            self._enter_step()

    def resume(self, turbo: bool = False):
        """Free-run in real time (turbo=True: as fast as possible)."""
        self.paused = False
        self._cmd(f"zxh-turbo {1 if turbo else 0}")
        if self.z.step_mode:
            self._cmd("exit-cpu-step")

    # ------------------------------------------------------------- programs

    def load(self, path, machine: str | None = None):
        """Reset and load a .tap/.tzx/.sna/.z80/.zsf/.nex (ZEsarUX smartload; tapes
        autoload through the ROM's LOAD ""). .nex selects the Next. Symbols are
        read from a .map (z88dk) or .sym (sjasmplus) next to the file."""
        p = Path(path).resolve()
        if not p.exists():
            raise EmulatorError(f"{p} not found")
        want = machine_id(machine) if machine else ("next" if p.suffix.lower() in (".nex", ".snx") else None)
        with self.held():
            if want and want != self.machine:
                self.set_machine(want)
            self._cmd("hard-reset-cpu")
            self._cmd(f'smartload "{p}"', timeout=60)
        self.program = str(p)
        self.load_frame = self.frame()
        self.symbols = Symbols.load(p.with_suffix(".map"))   # z88dk
        self.symbols.add_file(p.with_suffix(".sym"))         # sjasmplus
        self.mark = self.log_end()
        self._held = []
        return self.status()

    def set_machine(self, name: str):
        """Switch machine (resets). A window emulator is restarted instead: on
        macOS, ZEsarUX re-creates the window from the ZRCP thread and AppKit
        aborts. The restart gets a new ZRCP port and an empty log; breakpoints
        are dropped."""
        m = machine_id(name)
        if self.window:
            was_paused = self.paused
            self._stop_process()
            self.machine, self.paused = m, True
            self.port = free_port()
            self._logtext, self._logbase = bytearray(), 0
            self._bps.clear()
            self._watches.clear()
            self._start()
            if not was_paused:
                self.paused = False   # the caller's held() exits cpu-step
            return
        with self.held():
            self._cmd(f"set-machine {MACHINES[m]}")    # drops cpu-step mode, opens a menu
            self._enter_step()
            self._cmd("hard-reset-cpu")
        self.machine = m
        self.mark = self.log_end()

    def reset(self, hard: bool = True):
        """Reset the machine (the loaded program is gone; use load() to rerun it)."""
        with self.held():
            self._cmd("hard-reset-cpu" if hard else "reset-cpu")
        self.mark = self.log_end()

    # ------------------------------------------------------------- running

    def _run(self, frames: int, until: bytes | None = None, since: int | None = None) -> dict:
        frames = max(0, int(frames))
        line = f"zxh-run frames={frames}"
        if until:
            line += f" until={until.hex()} since={self.mark if since is None else since}"
        with self.held():
            out = self._cmd(line, timeout=30 + frames / 50)
        r = kv(out)
        res = {"frames": int(r.get("frames", 0)), "frame": int(r.get("total", 0)),
               "reason": r.get("reason"), "pc": int(r.get("pc", "0"), 16)}
        if "match" in r:
            res["match"] = int(r["match"])
        brk = kv_tail(out, "break")
        if brk:
            res["break"] = brk
        if res["reason"] == "break":
            res["symbol"] = self.symbols.name_of(res["pc"])
        return res

    def run(self, frames: int = 1) -> dict:
        """Advance `frames` frames (stops early at a breakpoint: see result['reason'])."""
        if not self.paused and self.z is not None and not self.z.step_mode:
            time.sleep(frames / 50)
            return {"frames": frames, "reason": "realtime", "frame": self.frame()}
        return self._run(frames)

    def run_until(self, text: str, max_frames: int = 3000, since: int | None = None) -> bool:
        """Run until the guest log contains `text` (written after `since`, default:
        after the last log()/reset/load/previous match). Stops on that exact
        instruction. Returns False if max_frames pass first (or a breakpoint hits)."""
        r = self._run(max_frames, until=text.encode("latin-1"), since=since)
        self.last_run = r
        if r["reason"] == "log":
            self.mark = r["match"]
            return True
        return False

    def frame(self) -> int:
        return int(kv(self._cmd("zxh-status"))["frames"])

    def status(self) -> dict:
        r = kv(self._cmd("zxh-status"))
        return {"machine": self.machine, "frame": int(r["frames"]), "pc": int(r["pc"], 16),
                "pc_symbol": self.symbols.name_of(int(r["pc"], 16)),
                "paused": self.paused, "window": self.window, "program": self.program,
                "log_end": int(r["log"]), "symbols": len(self.symbols), "zrcp_port": self.port,
                "breakpoints": len(self._bps), "watchpoints": len(self._watches)}

    # ------------------------------------------------------------- debug log

    def log_end(self) -> int:
        return int(kv(self._cmd("zxh-status"))["log"])

    def log_cursor(self) -> int:
        """Absolute offset of the end of the log (pass as since= to run_until)."""
        return self.log_end()

    def _sync_log(self):
        while True:
            have = self._logbase + len(self._logtext)
            out = self._cmd(f"zxh-log {have} 65536")
            head, _, data = out.partition("\n")
            h = kv(head)
            base, nxt, since = int(h["base"]), int(h["next"]), int(h["since"])
            if since > have:  # ring dropped text we never saw
                self._logtext = bytearray()
                self._logbase = since
            self._logtext += bytes.fromhex(data.strip())
            if nxt >= int(h["end"]):
                return

    def log_text(self, since: int | None = None) -> str:
        self._sync_log()
        start = max(0, (self.mark if since is None else since) - self._logbase)
        return self._logtext[start:].decode("latin-1")

    def log(self, since: int | None = None) -> str:
        """Guest log text written since `since` (default: since the last log()
        call / reset / load); moves the mark to the end."""
        t = self.log_text(since)
        self.mark = self._logbase + len(self._logtext)
        return t

    @property
    def debug_log(self) -> list[str]:
        """Every log line so far (as far back as the emulator's 4 MB ring goes)."""
        self._sync_log()
        return self._logtext.decode("latin-1").splitlines()

    # ------------------------------------------------------------- input

    def _ports(self, pressed):
        self._cmd("set-ui-io-ports " + K.ports_hex(pressed))

    def hold(self, keys):
        """Press and keep holding keys / joystick directions ('Q', 'SPACE', 'UP+FIRE')."""
        for k in K.parse(keys):
            if k not in self._held:
                self._held.append(k)
        self._ports(self._held)

    def release(self, keys=None):
        """Release some (or all) held keys."""
        self._held = [] if keys is None else [k for k in self._held if k not in K.parse(keys)]
        self._ports(self._held)

    def press(self, keys, hold_frames: int = 3, release_frames: int = 3) -> dict:
        """Press keys together for hold_frames, release, run release_frames."""
        ks = K.parse(keys)
        self._ports(self._held + [k for k in ks if k not in self._held])
        r = self.run(hold_frames)
        self._ports(self._held)
        if release_frames and r.get("reason") != "break":   # don't run past a breakpoint
            r = self.run(release_frames)
        return r

    def type(self, text: str, hold_frames: int = 3, gap_frames: int = 3) -> dict:
        """Type text key by key (letters lower case unless upper; \\n = ENTER).
        Note BASIC's keyword entry: in K mode a letter types a keyword."""
        r = {}
        for ch in text:
            r = self.press(K.chord_for_char(ch), hold_frames, gap_frames)
        return r

    def joystick(self, kind: str | None = None) -> str:
        """Get/set the emulated joystick: Kempston, Sinclair 1, Sinclair 2, Cursor, Fuller, None."""
        return self._cmd("zxh-joystick" + (f" {kind}" if kind else "")).strip()

    def sequence(self, steps) -> list:
        """[{"press": "Q", "hold": 3}, {"run": 20}, {"until": "text", "max": 500}, {"hold": "UP"},
        {"release": "UP"}, {"type": "10 PRINT"}] -> results."""
        out = []
        for st in steps:
            if "press" in st:
                out.append(self.press(st["press"], st.get("hold", 3), st.get("release", 3)))
            elif "run" in st:
                out.append(self.run(st["run"]))
            elif "until" in st:
                out.append(self.run_until(st["until"], st.get("max", 3000)))
            elif "type" in st:
                out.append(self.type(st["type"]))
            elif "hold" in st:
                self.hold(st["hold"]); out.append(True)
            elif "release" in st:
                self.release(st["release"] or None); out.append(True)
            else:
                raise ValueError(f"bad step {st!r}")
        return out

    # ------------------------------------------------------------- screen

    def screenshot(self, path=None, border: bool = True, scale: int = 1):
        """The frame as displayed (PIL RGB image). 48K/128K: 352x304 with border
        (paper 256x192 at 48,56); Next: 704x608 (ZEsarUX renders the Next at 2x
        so 640-wide modes fit). border=False crops to the paper area."""
        from PIL import Image
        bmp = self._tmp / f"shot-{time.monotonic_ns()}.bmp"
        self._cmd(f'save-screen "{bmp}"')
        for _ in range(50):
            if bmp.exists() and bmp.stat().st_size > 0:
                break
            time.sleep(0.02)
        img = Image.open(bmp).convert("RGB")
        img.load()
        bmp.unlink(missing_ok=True)
        if not border:
            w, h = img.size
            k = 2 if w >= 600 else 1
            x, y = (w - 256 * k) // 2, (h - 192 * k) // 2
            img = img.crop((x, y, x + 256 * k, y + 192 * k))
        if scale and scale != 1:
            img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        if path:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            img.save(path)
        return img

    # ------------------------------------------------------------- registers / memory

    def addr(self, expr) -> int:
        """Number, $hex/0xhex/NNh, symbol (main or _main), symbol+off, or register."""
        regs = None
        if isinstance(expr, str) and expr.strip().upper() in ("PC", "SP", "HL", "DE", "BC", "IX", "IY"):
            regs = self.regs()
        return self.symbols.resolve(expr, regs)

    def regs(self) -> dict:
        out = self._cmd("get-registers").strip()
        r: dict = {}
        for tok in out.split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                if k in ("F", "F'"):
                    r["FLAGS" if k == "F" else "FLAGS'"] = v
                elif k == "MMU":
                    r["MMU"] = v
                else:
                    try:
                        r[k] = int(v, 16)
                    except ValueError:
                        r[k] = v
            elif tok.startswith("IM"):
                r["IM"] = int(tok[2:]) if tok[2:].isdigit() else tok
            elif tok.startswith("IFF"):
                r["IFF"] = tok[3:]
        return r

    def set_reg(self, name: str, value) -> dict:
        v = self.addr(value) if isinstance(value, str) else int(value)
        self._cmd(f"set-register {name.upper()}={v}")
        return self.regs()

    def read_mem(self, addr, length: int = 16) -> bytes:
        a = self.addr(addr)
        out = bytearray()
        while length > 0:
            n = min(length, 0x10000 - a, 4096)
            out += bytes.fromhex(self._cmd(f"read-memory {a} {n}").strip())
            a = (a + n) & 0xFFFF
            length -= n
        return bytes(out)

    def write_mem(self, addr, data: bytes):
        a = self.addr(addr)
        for i in range(0, len(data), 128):
            chunk = data[i:i + 128]
            self._cmd(f"write-memory {a + i} " + " ".join(str(b) for b in chunk))

    def peek(self, addr) -> int:
        return self.read_mem(addr, 1)[0]

    def peek16(self, addr) -> int:
        b = self.read_mem(addr, 2)
        return b[0] | b[1] << 8

    def poke(self, addr, value: int):
        self.write_mem(addr, bytes([value & 0xFF]))

    def find_mem(self, needle: bytes, start: int = 0, end: int = 0x10000) -> list[int]:
        mem = self.read_mem(start, end - start)
        out, i = [], mem.find(needle)
        while i >= 0 and len(out) < 100:
            out.append(start + i)
            i = mem.find(needle, i + 1)
        return out

    def disasm(self, addr="pc", count: int = 8) -> list[str]:
        a = self.addr(addr)
        lines = self._cmd(f"disassemble {a} {count}").splitlines()
        out = []
        for ln in lines:
            ln = ln.strip()
            if not ln:
                continue
            try:
                la = int(ln.split()[0], 16)
                name = self.symbols.name_of(la)
                if name and "+" not in name:
                    out.append(f"{name}:")
            except ValueError:
                pass
            out.append("  " + ln)
        return out

    # ------------------------------------------------------------- debugger

    def _enable_breakpoints(self):
        self._cmd("noop")  # liveness check; the next call may legitimately "fail"
        self.z.cmd("enable-breakpoints", check=False)  # "Already enabled" is fine

    def _free_bp(self) -> int:
        for i in range(1, 100):
            if i not in self._bps:
                return i
        raise EmulatorError("all 99 breakpoint slots used")

    def break_at(self, addr) -> dict:
        """Breakpoint at an address/symbol (stops before executing it)."""
        a = self.addr(addr)
        return self.break_if(f"PC={a}", desc=f"{addr} (${a:04X})", addr=a)

    def break_if(self, condition: str, desc: str | None = None, addr: int | None = None) -> dict:
        """Breakpoint on any ZEsarUX condition, e.g. 'PC=32768', 'A=5 AND PC=40000',
        'MWA=23560' (memory written at). See `help set-breakpoint`."""
        i = self._free_bp()
        self._enable_breakpoints()
        self._cmd(f"set-breakpoint {i} {condition}")
        self._bps[i] = {"index": i, "condition": condition, "desc": desc or condition, "addr": addr}
        return self._bps[i]

    def watch(self, addr, length: int = 1, kind: str = "w") -> dict:
        """Data watchpoint: stop when the CPU reads ('r'), writes ('w') or
        accesses ('rw') [addr, addr+length)."""
        a = self.addr(addr)
        t = {"r": 1, "w": 2, "rw": 3, "wr": 3}[kind]
        self._enable_breakpoints()
        self._cmd(f"set-membreakpoint {a} {t} {length}")
        self._watches[a] = {"addr": a, "length": length, "kind": kind, "desc": str(addr)}
        return self._watches[a]

    def delete(self, index: int | None = None, addr=None):
        """Remove a breakpoint (by index), a watchpoint (by addr), or everything."""
        if index is None and addr is None:
            for i in list(self._bps):
                self._cmd(f"set-breakpoint {i}")
            self._bps.clear()
            self._cmd("clear-membreakpoints")
            self._watches.clear()
            return
        if index is not None and index in self._bps:
            self._cmd(f"set-breakpoint {index}")
            del self._bps[index]
        if addr is not None:
            a = self.addr(addr)
            w = self._watches.pop(a, None)
            if w:
                self._cmd(f"set-membreakpoint {a} 0 {w['length']}")

    def breakpoints(self) -> dict:
        return {"breakpoints": list(self._bps.values()), "watchpoints": list(self._watches.values())}

    def continue_(self, max_frames: int = 500) -> dict:
        """Run until a breakpoint/watchpoint fires (or max_frames pass)."""
        r = self._run(max_frames)
        r["stopped"] = r["reason"] == "break"
        if r["stopped"]:
            r["regs"] = self.regs()
        return r

    def stepi(self, count: int = 1) -> dict:
        """Execute `count` instructions (holds the CPU)."""
        with self.held():
            for _ in range(max(1, count)):
                self._cmd("cpu-step")
        r = self.regs()
        return {"regs": r, "pc_symbol": self.symbols.name_of(r.get("PC", 0)),
                "next": self.disasm("pc", 1)}

    def step_over(self) -> dict:
        """Run to the instruction after the current one (steps over CALL/RST)."""
        with self.held():
            self._cmd("cpu-step-over", timeout=60)
        r = self.regs()
        return {"regs": r, "pc_symbol": self.symbols.name_of(r.get("PC", 0)),
                "next": self.disasm("pc", 1)}

    def backtrace(self, items: int = 8) -> list[dict]:
        """Raw 16-bit words on the stack from SP (return addresses show symbols)."""
        sp = self.regs()["SP"]
        data = self.read_mem(sp, min(items * 2, 0x10000 - sp))
        items = len(data) // 2
        out = []
        for i in range(items):
            w = data[2 * i] | data[2 * i + 1] << 8
            out.append({"sp": (sp + 2 * i) & 0xFFFF, "value": w, "symbol": self.symbols.name_of(w)})
        return out

    # ------------------------------------------------------------- snapshots

    def save_state(self, path) -> str:
        """Snapshot to a file: .zsf (ZEsarUX, complete incl. Next), .z80, .sna."""
        p = Path(path)
        if p.suffix == "":
            p = p.with_suffix(".zsf")
        p = p.resolve()
        p.parent.mkdir(parents=True, exist_ok=True)
        # snapshot-save takes the rest of the line verbatim (no quoting)
        tmp = self._tmp / f"state{p.suffix}" if " " in str(p) else p
        with self.held():
            self._cmd(f"snapshot-save {tmp}")
        if tmp != p:
            shutil.move(tmp, p)
        if not p.exists():
            raise EmulatorError(f"snapshot not written: {p}")
        return str(p)

    def load_state(self, path) -> dict:
        p = Path(path)
        if p.suffix == "":
            p = p.with_suffix(".zsf")
        p = p.resolve()
        if not p.exists():
            raise EmulatorError(f"{p} not found")
        if " " in str(p):
            shutil.copy(p, self._tmp / f"state{p.suffix}")
            p = self._tmp / f"state{p.suffix}"
        with self.held():
            self._cmd(f"snapshot-load {p}")
        self.machine = _MACHINE_NAMES.get(self._cmd("get-current-machine").strip(), self.machine)
        return self.status()
