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

import hashlib
import json
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


_handed_out: set[int] = set()


def free_port() -> int:
    """A free TCP port for ZRCP, from 20000-29999: outside the OS's ephemeral
    range (49152+ on macOS), which every outgoing connection draws from, and
    never one this process handed out before."""
    import random
    for _ in range(500):
        port = random.randrange(20000, 30000)
        if port in _handed_out:
            continue
        try:
            for host in ("127.0.0.1", "0.0.0.0"):
                with socket.socket() as s:
                    s.bind((host, port))
        except OSError:
            continue
        _handed_out.add(port)
        return port
    raise EmulatorError("no free TCP port in 20000-29999")


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
        self._inputs: list[dict] = []   # input changes since load: {seq, f, t, ports, ext} / {joystick}
        self._checkpoints: list[dict] = []
        self._seq = 0
        self._joy_at_load = joystick
        self.last_stop: dict | None = None
        self._bps: dict[int, dict] = {}
        self._watches: dict[int, dict] = {}
        self._tmp = Path(tempfile.mkdtemp(prefix="zxdev-"))
        self.log_path = Path(log_path) if log_path else self._tmp / "zesarux.log"
        self.z: Zrcp | None = None

        self._opts = {"zoom": zoom, "audio": audio, "joystick": joystick, "extra": list(extra_args)}
        self._out = open(self.log_path, "ab")
        for attempt in range(3):
            try:
                self._start()
                break
            except EmulatorError:
                # usually the probed port was taken again (or still in TIME_WAIT
                # from the previous emulator): ZEsarUX then runs without ZRCP
                if attempt == 2:
                    self.close()
                    raise
                self.port = free_port()

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
            self.z = Zrcp(self.port, connect_timeout=8)
        except ZrcpError as ex:
            self._out.flush()
            tail = self.log_path.read_bytes()[-1500:].decode("latin-1", "replace")
            code = self.proc.poll()
            self._stop_process()
            raise EmulatorError(f"ZEsarUX did not open ZRCP on :{self.port} ({ex}; "
                                f"exit code {code}). Log tail:\n{tail}") from None
        # make sure the ZRCP we reached belongs to our process (a port race with
        # another emulator would otherwise let us drive the wrong one)
        other = kv(self.z.cmd("zxh-status", check=False)).get("pid")
        if other != str(self.proc.pid):
            self.z.close()            # never send exit-emulator to someone else's emulator
            self.z = None
            self.proc.kill()
            self.proc.wait(timeout=5)
            raise EmulatorError(f"port {self.port} answered for pid {other}, not our emulator {self.proc.pid}")
        if self.paused:
            self._enter_step()
            # same starting state every time, whatever ran before we got the CPU
            self._power_on_state()
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
        autoload through the ROM's LOAD ""). .nex selects the Next; anything else
        but .zsf moves a Next session back to the 48K. Symbols are
        read from a .map (z88dk) or .sym (sjasmplus) next to the file."""
        p = Path(path).resolve()
        if not p.exists():
            raise EmulatorError(f"{p} not found")
        ext = p.suffix.lower()
        if machine:
            want = machine_id(machine)
        elif ext in (".nex", ".snx"):
            want = "next"
        elif self.machine == "next" and ext not in (".zsf",):
            want = "48k"      # tapes/.sna/.z80 don't autoload on the Next's TBBlue boot
        else:
            want = None
        with self.held():
            if want and want != self.machine:
                self.set_machine(want)
            self._power_on_state()
            self._cmd(f'smartload "{p}"', timeout=60)
        self.program = str(p)
        self.load_frame = self.frame()
        self.symbols = Symbols.load(p.with_suffix(".map"))   # z88dk
        self.symbols.add_file(p.with_suffix(".sym"))         # sjasmplus
        self.mark = self.log_end()
        self._held = []
        self._inputs, self._checkpoints, self._seq = [], [], 0
        self._joy_at_load = self.joystick()
        self.last_stop = None
        return self.status()

    POWER_ON_REGS = {"AF": 0xFFFF, "SP": 0xFFFF, "BC": 0, "DE": 0, "HL": 0, "IX": 0, "IY": 0,
                     "AF'": 0, "BC'": 0, "DE'": 0, "HL'": 0, "I": 0, "R": 0}

    def _power_on_state(self):
        """Hard reset into a defined state: ZEsarUX's hard reset keeps RAM and
        most registers, so a boot would depend on what ran before. Zero RAM,
        fix the registers and the FLASH phase (CPU must be held)."""
        self._cmd("hard-reset-cpu")
        self._cmd("zxh-sync ram")
        for r, v in self.POWER_ON_REGS.items():
            self._cmd(f"set-register {r}={v}")

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
            if hard:
                self._power_on_state()
            else:
                self._cmd("reset-cpu")
        self.mark = self.log_end()

    # ------------------------------------------------------------- running

    def _run(self, frames: int, until: bytes | None = None, since: int | None = None,
             at: tuple[int, int] | None = None) -> dict:
        frames = max(0, int(frames))
        line = f"zxh-run frames={frames}"
        if until:
            line += f" until={until.hex()} since={self.mark if since is None else since}"
        if at:
            line += f" at={at[0]}:{at[1]}"
        with self.held():
            out = self._cmd(line, timeout=30 + frames / 50)
        r = kv(out)
        res = {"frames": int(r.get("frames", 0)), "frame": int(r.get("total", 0)),
               "tstates": int(r.get("tstates", 0)), "reason": r.get("reason"),
               "pc": int(r.get("pc", "0"), 16)}
        res["since_load"] = res["frame"] - self.load_frame
        if "match" in r:
            res["match"] = int(r["match"])
        brk = kv_tail(out, "break")
        if brk:
            res["break"] = brk
        if res["reason"] in ("break", "log"):
            res["symbol"] = self.symbols.name_of(res["pc"])
        self._note_stop(res)
        return res

    _WHY = {"frames": "frame_limit", "log": "log_match", "at": "position", "realtime": "frame_limit"}

    def _note_stop(self, res: dict):
        """last_stop: where and why emulation stopped. why = breakpoint, watchpoint,
        step, frame_limit, log_match or position."""
        why = self._WHY.get(res.get("reason"), res.get("reason"))
        if res.get("reason") == "break":
            why = "watchpoint" if "Memory Breakpoint" in (res.get("break") or "") else "breakpoint"
        res["why"] = why
        self.last_stop = {k: res.get(k) for k in ("why", "reason", "pc", "symbol", "break", "frame",
                                                  "since_load", "tstates")}

    def position(self) -> dict:
        """Where emulation is: frame (absolute), frames since load, T-state in the frame."""
        r = kv(self._cmd("zxh-status"))
        f = int(r["frames"])
        return {"frame": f, "since_load": f - self.load_frame, "tstates": int(r["tstates"])}

    def run_to(self, since_load: int, tstates: int = 0) -> dict:
        """Run to the first instruction boundary at/after (frame since load, T-state).
        Breakpoints still stop it (reason 'break')."""
        target = self.load_frame + since_load
        pos = self.position()
        if (pos["frame"], pos["tstates"]) >= (target, tstates):
            return {"reason": "at", "frames": 0, **pos}
        return self._run(target - pos["frame"] + 2, at=(target, tstates))

    def _free_running(self) -> bool:
        return not self.paused and self.z is not None and not self.z.step_mode

    def run(self, frames: int = 1) -> dict:
        """Advance `frames` frames (stops early at a breakpoint: see result['reason']).
        In a free-running window this fast-forwards on the emulator's main thread,
        so the user's keyboard/joystick keep working meanwhile."""
        if self._free_running():
            return self._fast_forward(frames=frames)
        return self._run(frames)

    def _fast_forward(self, frames: int | None = None, until: bytes | None = None,
                      since: int | None = None, timeout: float = 600) -> dict:
        """Window sessions: run turbo on the main thread (host input stays live),
        polling for the frame count / log text / a breakpoint, then go back to real
        time. Stops within a few frames of the condition (not on the instruction)."""
        start = self.frame()
        since = self.mark if since is None else since
        self._cmd("zxh-turbo 1")
        res = {"reason": "frames"}
        t0 = time.monotonic()
        try:
            while time.monotonic() - t0 < timeout:
                st = kv(self._cmd("zxh-status"))
                f = int(st["frames"])
                if st.get("brk") == "1":
                    res["reason"] = "break"
                    break
                if until:
                    text = self.log_text(since).encode("latin-1")
                    i = text.find(until)
                    if i >= 0:
                        res["reason"] = "log"
                        res["match"] = max(since, self._logbase) + i + len(until)
                        break
                if frames is not None and f - start >= frames:
                    break
                time.sleep(0.01)
            else:
                res["reason"] = "timeout"
        finally:
            self._cmd("zxh-turbo 0")
        if res["reason"] == "break":
            self._enter_step()          # the debugger menu opened in the window; take the CPU
            self.paused = True
        st = kv(self._cmd("zxh-status"))
        res.update(frames=int(st["frames"]) - start, frame=int(st["frames"]),
                   tstates=int(st["tstates"]), pc=int(st["pc"], 16))
        res["since_load"] = res["frame"] - self.load_frame
        res["symbol"] = self.symbols.name_of(res["pc"])
        self._note_stop(res)
        return res

    def run_until(self, text: str, max_frames: int = 3000, since: int | None = None) -> bool:
        """Run until the guest log contains `text` (written after `since`, default:
        after the last log()/reset/load/previous match). Stops on that exact
        instruction. Returns False if max_frames pass first (or a breakpoint hits)."""
        if self._free_running():
            r = self._fast_forward(frames=max_frames, until=text.encode("latin-1"), since=since)
        else:
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
                "breakpoints": len(self._bps), "watchpoints": len(self._watches),
                "since_load": int(r["frames"]) - self.load_frame, "tstates": int(r["tstates"]),
                "inputs_logged": len(self._inputs), "last_stop": self.last_stop}

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
        ports = K.ports_hex(pressed)
        b0, b1 = K.next_ext(pressed)
        self._apply_input(ports, f"{b0:02x}{b1:02x}")

    def _apply_input(self, ports: str, ext: str = "0000", log: bool = True):
        self._cmd("set-ui-io-ports " + ports)
        if self.machine == "next":
            self._cmd(f"zxh-nextkeys {ext[:2]} {ext[2:]}")
        if log:
            pos = self.position()
            self._seq += 1
            self._inputs.append({"seq": self._seq, "f": pos["since_load"], "t": pos["tstates"],
                                 "ports": ports, "ext": ext})

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
        """Get/set the emulated joystick: Kempston, Sinclair 1, Sinclair 2, Cursor, Fuller, None.
        Joystick directions (UP DOWN LEFT RIGHT FIRE) are mapped onto it: Sinclair and
        Cursor joysticks appear as number keys in the keyboard matrix."""
        out = self._cmd("zxh-joystick" + (f" {kind}" if kind else "")).strip()
        if kind and self.program:
            pos = self.position()
            self._seq += 1
            self._inputs.append({"seq": self._seq, "f": pos["since_load"], "t": pos["tstates"],
                                 "joystick": out})
        return out

    def sequence(self, steps) -> list:
        """[{"press": "Q", "hold": 3}, {"run": 20}, {"until": "text", "max": 500}, {"hold": "UP"},
        {"release": "UP"}, {"type": "10 PRINT"}] -> results. Any step may carry "at": N
        (frames since load, plus optional "t") to run there first: timed scripts."""
        out = []
        for st in steps:
            if "at" in st:   # frames since load (optionally "t": T-state in that frame)
                self.run_to(int(st["at"]), int(st.get("t", 0)))
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
            elif "at" in st:
                out.append(self.position())
            else:
                raise ValueError(f"bad step {st!r}")
        return out

    # ------------------------------------------------------------- screen

    def screenshot(self, path=None, border: bool = True, scale: int = 1, layer: str = "screen"):
        """The frame as displayed (PIL RGB image). 48K/128K: 352x304 with border
        (paper 256x192 at 48,56); Next: 704x608 (ZEsarUX renders the Next at 2x
        so 640-wide modes fit). border=False crops to the paper area.
        layer: "screen" (composited output), or one layer rendered from memory at
        its native size: "ula" (256x192), "layer2" (Next, 256x192/320x256/640x256),
        "sprites" (Next, the 320x256 sprite plane, RGBA)."""
        from PIL import Image
        if layer != "screen":
            img = self.render_layer(layer)
            if scale and scale != 1:
                img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
            if path:
                Path(path).parent.mkdir(parents=True, exist_ok=True)
                img.save(path)
            return img
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

    # ------------------------------------------------------------- layers, hashes, video

    def _reg(self, n: int) -> int:
        return int(self._cmd(f"tbblue-get-register {n}").strip().rstrip("Hh"), 16)

    def _palette(self, which: str, second: bool) -> list[int]:
        out = self._cmd(f"tbblue-get-palette {which} {'second' if second else 'first'} 0 256")
        return [int(v, 16) for v in out.split()]

    def _zone_read(self, zone: int, off: int, n: int) -> bytes:
        out = bytearray()
        self._cmd(f"set-memory-zone {zone}")
        try:
            while n > 0:
                k = min(n, 4096)
                out += bytes.fromhex(self._cmd(f"read-memory {off} {k}").strip())
                off += k
                n -= k
        finally:
            self._cmd("set-memory-zone -1")
        return bytes(out)

    def _zone_write(self, zone: int, off: int, data: bytes):
        self._cmd(f"set-memory-zone {zone}")
        try:
            for i in range(0, len(data), 128):
                self._cmd(f"write-memory {off + i} " + " ".join(str(b) for b in data[i:i + 128]))
        finally:
            self._cmd("set-memory-zone -1")

    def render_layer(self, layer: str):
        from . import video
        if layer == "ula":
            if self.machine in ("16k", "48k", "48k+"):
                scr = self.read_mem(0x4000, 6912)
            else:
                bank = 7 if "SCR7" in self._cmd("get-paging-state") else 5
                scr = self.read_mem(0, 6912, bank=bank)
            return video.render_ula(scr)
        if self.machine != "next":
            raise EmulatorError(f"layer {layer!r} needs the Next (machine is {self.machine})")
        if layer == "layer2":
            r70 = self._reg(0x70)
            mode = (r70 >> 4) & 3
            mode = 0 if mode == 3 else mode
            size = 48 * 1024 if mode == 0 else 80 * 1024
            data = self._zone_read(0, video.NEXT_RAM + self._reg(0x12) * 16384, size)
            pal = self._palette("layer2", bool(self._reg(0x43) & 4))
            return video.render_layer2(data, pal, mode, self._reg(0x14), (r70 & 15) << 4)
        if layer == "sprites":
            attrs = self._zone_read(31, 0, 640)
            pats = self._zone_read(14, 0, 16384)
            pal = self._palette("sprite", bool(self._reg(0x43) & 8))
            return video.render_sprites(attrs, pats, pal, self._reg(0x4B))
        raise ValueError(f"unknown layer {layer!r}: screen, ula, layer2, sprites")

    def screen_hash(self, layer: str = "screen") -> str:
        """md5 of the displayed frame's RGB pixels (or of one layer)."""
        img = self.screenshot(layer=layer)
        return hashlib.md5(img.tobytes()).hexdigest()

    def record_video(self, path, frames: int = 100, every: int = 1, scale: int = 1,
                     layer: str = "screen") -> dict:
        """Record `frames` frames (one picture every `every` frames) to .gif, .mp4,
        or a directory of PNGs. Paused sessions advance deterministically; window
        sessions are sampled in real time. MP4 needs ffmpeg on PATH."""
        from PIL import Image
        p = Path(path)
        shots = []
        for i in range(0, max(1, frames), max(1, every)):
            if i:
                self.run(every)
            shots.append(self.screenshot(scale=scale, layer=layer).convert("RGB"))
        fps = 50 / max(1, every)
        if p.suffix.lower() == ".gif":
            p.parent.mkdir(parents=True, exist_ok=True)
            shots[0].save(p, save_all=True, append_images=shots[1:], loop=0,
                          duration=int(1000 / fps), optimize=False)
        elif p.suffix.lower() in (".mp4", ".mov", ".webm"):
            ff = shutil.which("ffmpeg")
            if not ff:
                raise EmulatorError("ffmpeg not found: use a .gif path, or install ffmpeg")
            tmp = self._tmp / "video"
            shutil.rmtree(tmp, ignore_errors=True)
            tmp.mkdir()
            for i, im in enumerate(shots):
                im.save(tmp / f"{i:05d}.png")
            p.parent.mkdir(parents=True, exist_ok=True)
            r = subprocess.run([ff, "-y", "-loglevel", "error", "-framerate", f"{fps:g}",
                                "-i", str(tmp / "%05d.png"), "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2:flags=neighbor",
                                "-pix_fmt", "yuv420p", str(p)], capture_output=True, text=True)
            if r.returncode:
                raise EmulatorError(f"ffmpeg failed: {r.stderr.strip()[-500:]}")
        else:
            p.mkdir(parents=True, exist_ok=True)
            for i, im in enumerate(shots):
                im.save(p / f"{i:05d}.png")
        return {"path": str(p), "frames": len(shots), "fps": fps,
                "size": list(shots[0].size)}

    # ------------------------------------------------------------- replay

    def input_log(self) -> list[dict]:
        """Every input change since the program was loaded, with its position."""
        return list(self._inputs)

    def checkpoint(self, label: str = "") -> dict:
        """Remember the screen hash here (a replay checks it at the same point)."""
        pos = self.position()
        self._seq += 1
        cp = {"seq": self._seq, "f": pos["since_load"], "t": pos["tstates"],
              "label": label or f"cp{len(self._checkpoints) + 1}", "screen": self.screen_hash(),
              "pc": self.regs()["PC"]}
        self._checkpoints.append(cp)
        return cp

    def save_replay(self, path=None) -> dict:
        """Input log + checkpoints + the final position/hash as a replay (JSON).
        Covers everything since the last load()."""
        if not self.program:
            raise EmulatorError("no program loaded: a replay starts from load()")
        end = self.checkpoint("end")
        self._checkpoints.pop()
        rec = {"version": 1, "program": self.program, "machine": self.machine,
               "joystick": self._joy_at_load, "events": self.input_log(),
               "checkpoints": list(self._checkpoints), "end": end}
        if path:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            Path(path).write_text(json.dumps(rec, indent=1))
        return rec

    def replay(self, rec, verify: bool = True) -> dict:
        """Reload the program and re-apply a recorded input log at the same
        frames/T-states; check every checkpoint's screen hash. Deterministic in
        paused sessions (the replay always holds the CPU)."""
        base = None
        if not isinstance(rec, dict):
            base = Path(rec).resolve().parent
            rec = json.loads(Path(rec).read_text())
        if rec.get("version") == 2:
            return self._replay_v2(rec, base, verify)
        if not Path(rec["program"]).exists():
            raise EmulatorError(f"replay program {rec['program']} not found")
        self.delete()
        self.load(rec["program"], rec.get("machine"))
        self.joystick(rec.get("joystick") or "Kempston")
        self._inputs = []
        items = [("event", e) for e in rec["events"]] + [("check", c) for c in rec["checkpoints"]]
        items.append(("check", rec["end"]))
        items.sort(key=lambda it: (it[1]["f"], it[1]["t"], it[1].get("seq", 0)))
        results = []
        with self.held():
            for kind, it in items:
                self.run_to(it["f"], it["t"])
                if kind == "event":
                    if "joystick" in it:
                        self.joystick(it["joystick"])
                    else:
                        self._apply_input(it["ports"], it.get("ext", "0000"))
                elif verify:
                    got = self.screen_hash()
                    results.append({"label": it["label"], "f": it["f"], "t": it["t"],
                                    "expected": it["screen"], "got": got, "ok": got == it["screen"]})
        ok = all(r["ok"] for r in results)
        return {"ok": ok, "checkpoints": results, "events": len(rec["events"]),
                "position": self.position()}

    # ------------------------------------------------------------- input recorder (all sources)

    def record_input(self, action: str = "start", path=None, from_reset: bool = False) -> dict:
        """Record every input change (agent-injected AND the user's keyboard/joystick
        in the window) with frame + T-state, plus a hash of every frame.

        start: from the current point (a .zsf snapshot is taken to replay from; the
               program must be running: snapshots don't hold tape/loader state), or
               from_reset=True: reload the program first and replay from the load.
        stop:  stop recording.
        save:  write the recording (JSON + snapshot next to it); stops if needed.
        status: counts."""
        if action == "start":
            if from_reset and not self.program:
                raise EmulatorError("from_reset needs a loaded program")
            with self.held():          # snapshot and start at the same instruction
                if from_reset:
                    self.reload()
                    snap = None
                else:
                    snap = self.save_state(self._tmp / f"rec-start-{time.monotonic_ns()}.zsf")
                pos = self.position()
                self._cmd("zxh-hash clear")
                self._cmd("zxh-hash on")
                self._cmd("zxh-rec start")
            self._recording = {"program": self.program, "machine": self.machine,
                               "snapshot": snap, "from_reset": bool(from_reset),
                               "frame": pos["frame"], "tstates": pos["tstates"],
                               "since_load": pos["since_load"], "done": False}
            return {"recording": True, **pos}
        rec = getattr(self, "_recording", None)
        if rec is None:
            raise EmulatorError("not recording (record_input start first)")
        if action == "status":
            return {"recording": not rec["done"], **kv(self._cmd("zxh-rec status"))}
        if action in ("stop", "save") and not rec["done"]:
            with self.held():
                pos = self.position()
                self._cmd("zxh-rec stop")
                self._cmd("zxh-hash off")
                rec["events"] = self._rec_events()
                # the frame the recording started in was partly drawn before it
                # (and a replay can't redo that part): compare whole frames only
                rec["hashes"] = [h for h in self._hashes(rec["frame"])
                                 if h[0] > 1 or rec["tstates"] == 0]
                rec["end"] = {"f": pos["frame"] - rec["frame"], "t": pos["tstates"]}
            rec["done"] = True
        if action == "stop":
            return {"recording": False, "events": len(rec["events"]), "frames": rec["end"]["f"]}
        if action == "save":
            if not path:
                raise ValueError("save needs a path")
            p = Path(path).resolve()
            p.parent.mkdir(parents=True, exist_ok=True)
            out = {"version": 2, "program": rec["program"], "machine": rec["machine"],
                   "start": {"from_reset": rec["from_reset"], "tstates": rec["tstates"],
                             "since_load": rec["since_load"]},
                   "events": [{"f": f - rec["frame"], "t": t, "state": st} for f, t, st in rec["events"]],
                   "hashes": rec["hashes"], "end": rec["end"]}
            if rec["snapshot"]:
                snap = p.with_suffix(".zsf")
                shutil.copy(rec["snapshot"], snap)
                shutil.copy(rec["snapshot"] + ".beam", str(snap) + ".beam")
                out["start"]["snapshot"] = snap.name
            p.write_text(json.dumps(out, indent=None, separators=(",", ":")))
            return {"path": str(p), "events": len(out["events"]), "frames": rec["end"]["f"],
                    "hashes": len(out["hashes"]), "snapshot": out["start"].get("snapshot")}
        raise ValueError(f"unknown action {action!r}: start, stop, save, status")

    def _rec_events(self) -> list:
        out = self._cmd("zxh-rec log 0", timeout=60).splitlines()
        evs = []
        for ln in out[1:]:
            f, t, st = ln.split()
            evs.append((int(f), int(t), st))
        return evs

    def _hashes(self, base_frame: int) -> list:
        out = self._cmd("zxh-hash get 0", timeout=60).splitlines()
        return [[int(f) - base_frame, h] for f, h in (ln.split() for ln in out[1:])]

    def _run_to_abs(self, frame: int, tstates: int):
        pos = self.position()
        if (pos["frame"], pos["tstates"]) >= (frame, tstates):
            return
        while True:
            r = self._run(frame - self.position()["frame"] + 2, at=(frame, tstates))
            if r["reason"] == "at":
                return
            if r["reason"] not in ("break",):
                raise EmulatorError(f"replay could not reach frame {frame} t={tstates}: {r}")

    def _replay_v2(self, rec: dict, base: Path | None, verify: bool) -> dict:
        start = rec["start"]
        self.delete()
        if start.get("snapshot"):
            snap = Path(start["snapshot"])
            if not snap.is_absolute() and base is not None:
                snap = base / snap
            self.load_state(snap)
            self.program = rec["program"]
            self.symbols = Symbols.load(Path(rec["program"]).with_suffix(".map"))
            self.symbols.add_file(Path(rec["program"]).with_suffix(".sym"))
        else:
            self.load(rec["program"], rec.get("machine"))
            self._run_to_abs(self.load_frame + start.get("since_load", 0), start.get("tstates", 0))
        with self.held():
            f0 = self.position()["frame"]
            t0 = self.position()["tstates"]
            self._cmd("zxh-hash clear")
            self._cmd("zxh-hash on")
            for ev in rec["events"]:
                self._run_to_abs(f0 + ev["f"], ev["t"])
                self._cmd(f"zxh-input-set {ev['state']}")
            self._run_to_abs(f0 + rec["end"]["f"], rec["end"]["t"])
            self._cmd("zxh-hash off")
            got = self._hashes(f0)
        res = {"ok": True, "frames": rec["end"]["f"], "events": len(rec["events"]),
               "hashes": got, "start_tstates": t0, "recorded_start_tstates": start.get("tstates")}
        if verify and rec.get("hashes"):
            want = {f: h for f, h in rec["hashes"]}
            have = {f: h for f, h in got}
            common = sorted(set(want) & set(have))
            bad = [f for f in common if want[f] != have[f]]
            res.update(ok=not bad and len(common) > 0, compared=len(common),
                       first_mismatch=bad[0] if bad else None, mismatches=len(bad))
        return res

    # ------------------------------------------------------------- restart

    def reload(self) -> dict:
        """Reset and load the same program again."""
        if not self.program:
            raise EmulatorError("no program loaded")
        return self.load(self.program)

    def power_cycle(self) -> dict:
        """Cold start: a fresh ZEsarUX process with the same machine (no program)."""
        paused = self.paused
        self._stop_process()
        self.port = free_port()
        self.paused = True
        self._logtext, self._logbase = bytearray(), 0
        self._bps.clear()
        self._watches.clear()
        self._held = []
        self.program = None
        self.symbols = Symbols()
        self.load_frame = 0
        self._start()
        if not paused:
            self.resume()
        return self.status()

    def reboot(self) -> dict:
        """Recover a wedged emulator: kill it, start a fresh one, reload the program."""
        prog = self.program
        try:
            self.proc.kill()
        except OSError:
            pass
        paused = self.paused
        self.paused = True
        self.power_cycle()
        if prog:
            self.load(prog)
        if not paused:
            self.resume()
        return self.status()

    def memory_map(self) -> dict:
        """What is paged where: 128K banks (and screen bank), or Next MMU pages."""
        if self.machine in ("16k", "48k", "48k+"):
            return {"machine": self.machine, "slots": ["ROM", "RAM 4000", "RAM 8000", "RAM C000"]}
        pages = self._cmd("get-memory-pages").strip()
        out = {"machine": self.machine, "pages": pages, "paging": self._cmd("get-paging-state").strip()}
        if self.machine == "next":
            out["mmu"] = [self._reg(0x50 + i) for i in range(8)]
        return out

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

    def _bank_offset(self, bank, page) -> int:
        if self.machine == "next":
            return 0x40000 + (bank * 16384 if bank is not None else page * 8192)
        if page is not None:
            raise ValueError("8K pages are a Next thing; use bank= (16K) on 128K machines")
        if self.machine in ("16k", "48k", "48k+"):
            raise ValueError("a 48K has no banks: use plain addresses")
        return bank * 16384

    def read_mem(self, addr, length: int = 16, bank: int | None = None, page: int | None = None) -> bytes:
        """Read the 64K CPU view, or (bank=N) a 16K RAM bank on 128K/+2/+3/Next,
        or (page=N) an 8K Next RAM page, whatever is paged in. With bank/page,
        addr is the offset inside it."""
        if bank is not None or page is not None:
            return self._zone_read(0, self._bank_offset(bank, page) + int(self.addr(addr)), length)
        a = self.addr(addr)
        out = bytearray()
        while length > 0:
            n = min(length, 0x10000 - a, 4096)
            out += bytes.fromhex(self._cmd(f"read-memory {a} {n}").strip())
            a = (a + n) & 0xFFFF
            length -= n
        return bytes(out)

    def write_mem(self, addr, data: bytes, bank: int | None = None, page: int | None = None):
        if bank is not None or page is not None:
            return self._zone_write(0, self._bank_offset(bank, page) + int(self.addr(addr)), data)
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
        pos = self.position()
        self._note_stop({"reason": "step", "pc": r["PC"], "symbol": self.symbols.name_of(r["PC"]), **pos})
        return {"regs": r, "pc_symbol": self.symbols.name_of(r.get("PC", 0)),
                "next": self.disasm("pc", 1)}

    def step_over(self) -> dict:
        """Run to the instruction after the current one (steps over CALL/RST)."""
        with self.held():
            self._cmd("cpu-step-over", timeout=60)
        r = self.regs()
        pos = self.position()
        self._note_stop({"reason": "step", "pc": r["PC"], "symbol": self.symbols.name_of(r["PC"]), **pos})
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
        """Snapshot to a file: .zsf (ZEsarUX, complete incl. Next), .z80, .sna (48K/128K).
        A load does not re-render: compare screens one frame after load_state()."""
        p = Path(path)
        if p.suffix == "":
            p = p.with_suffix(".zsf")
        p = p.resolve()
        p.parent.mkdir(parents=True, exist_ok=True)
        if self.machine == "next" and p.suffix.lower() != ".zsf":
            # ZEsarUX can't write .sna/.z80 for the Next; it opens an error
            # dialog and blocks the remote protocol
            raise EmulatorError("the Next can only be snapshotted as .zsf")
        if p.suffix.lower() not in (".zsf", ".z80", ".sna", ".zx", ".sp"):
            raise EmulatorError(f"unsupported snapshot format {p.suffix} (.zsf .z80 .sna .zx .sp; "
                                "ZEsarUX has no .szx writer)")
        # snapshot-save takes the rest of the line verbatim (no quoting)
        tmp = self._tmp / f"state{p.suffix}" if " " in str(p) else p
        with self.held():
            self._cmd(f"snapshot-save {tmp}")
            beam = self._cmd("zxh-tstates").strip()
        if tmp != p:
            shutil.move(tmp, p)
        if not p.exists():
            raise EmulatorError(f"snapshot not written: {p}")
        # snapshots don't hold the position inside the frame: keep it beside
        Path(str(p) + ".beam").write_text(beam + "\n")
        return str(p)

    def load_state(self, path) -> dict:
        p = Path(path)
        if p.suffix == "":
            p = p.with_suffix(".zsf")
        p = p.resolve()
        if not p.exists():
            raise EmulatorError(f"{p} not found")
        beam = Path(str(p) + ".beam")         # position in the frame (save_state writes it)
        if " " in str(p):
            shutil.copy(p, self._tmp / f"state{p.suffix}")
            p = self._tmp / f"state{p.suffix}"
        with self.held():
            self._cmd(f"snapshot-load {p}")
            if beam.exists():
                self._cmd(f"zxh-tstates {beam.read_text().strip()}")
        self.machine = _MACHINE_NAMES.get(self._cmd("get-current-machine").strip(), self.machine)
        return self.status()
