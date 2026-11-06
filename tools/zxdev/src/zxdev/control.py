"""Client side of a session's control port, and the session registry.

    from zxdev.control import SessionClient
    s = SessionClient.spawn("projects/hello48/build/hello48.tap")   # or SessionClient(port)
    s.call("run_until", text="hello48: ready")
    s.call("break", addr="main"); print(s.call("continue"))
    s.close(stop=True)
"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time

from .session import SESSIONS_DIR


class ProtocolError(RuntimeError):
    pass


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def list_sessions() -> list[dict]:
    """Running sessions, oldest first (stale registry files are removed)."""
    out = []
    if SESSIONS_DIR.exists():
        for f in SESSIONS_DIR.glob("*.json"):
            try:
                info = json.loads(f.read_text())
            except (OSError, json.JSONDecodeError):
                continue
            if _alive(info["pid"]):
                out.append(info)
            else:
                f.unlink(missing_ok=True)
    return sorted(out, key=lambda i: i.get("started", 0))


class SessionClient:
    def __init__(self, port: int, host: str = "127.0.0.1", timeout: float = 600.0, proc=None):
        self.port = port
        self.proc = proc
        self.info: dict = {}
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(timeout)
        self.f = self.sock.makefile("rwb")
        self._id = 0

    @classmethod
    def spawn(cls, program: str | None = None, window: bool = False, machine: str | None = None,
              mode: str | None = None, zoom: int | None = None) -> "SessionClient":
        """Start a new session process (headless+paused unless window=True)."""
        cmd = [sys.executable, "-m", "zxdev.cli", "serve", "--quiet"]
        if program:
            cmd.append(str(program))
        if window:
            cmd.append("--window")
        if machine:
            cmd += ["--machine", machine]
        if mode:
            cmd += ["--mode", mode]
        if zoom:
            cmd += ["--zoom", str(zoom)]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                stdin=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            line = proc.stdout.readline().decode(errors="replace")
            if line.startswith("ZXDEV-SESSION "):
                info = json.loads(line.split(" ", 1)[1])
                c = cls(info["control_port"], proc=proc)
                c.info = info
                return c
            if not line and proc.poll() is not None:
                break
        err = proc.stderr.read().decode(errors="replace") if proc.poll() is not None else ""
        proc.kill()
        raise ProtocolError(f"session failed to start: {err.strip()[-2000:]}")

    @property
    def alive(self) -> bool:
        if self.proc is not None:
            return self.proc.poll() is None
        pid = self.info.get("pid")
        return _alive(pid) if pid else True

    def call(self, cmd: str, **args):
        self._id += 1
        self.f.write((json.dumps({"id": self._id, "cmd": cmd, "args": args}) + "\n").encode())
        self.f.flush()
        line = self.f.readline()
        if not line:
            raise ProtocolError("session closed the connection")
        r = json.loads(line)
        if not r.get("ok"):
            raise ProtocolError(r.get("error", "error"))
        return r["result"]

    def close(self, stop: bool = False):
        if stop:
            try:
                self.call("quit")
            except (ProtocolError, OSError):
                pass
            if self.proc is not None:
                try:
                    self.proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
        try:
            self.sock.close()
        except OSError:
            pass
