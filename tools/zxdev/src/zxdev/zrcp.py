"""Minimal client for ZEsarUX's remote command protocol (ZRCP, TCP).

ZRCP is line based: send a command + newline, read until the prompt
("command> ", or "command@cpu-step> " while the CPU is held in step mode).
Numbers we send are always decimal: ZRCP reads "8ceb" as 8.
"""
from __future__ import annotations

import re
import socket
import time

_PROMPT = re.compile(rb"\n?command(@cpu-step)?> $")


class ZrcpError(RuntimeError):
    pass


class Zrcp:
    def __init__(self, port: int, host: str = "127.0.0.1", connect_timeout: float = 15.0):
        deadline = time.monotonic() + connect_timeout
        last = None
        while True:
            try:
                self.sock = socket.create_connection((host, port), timeout=2)
                break
            except OSError as ex:
                last = ex
                if time.monotonic() > deadline:
                    raise ZrcpError(f"cannot connect to ZRCP on {host}:{port}: {last}") from None
                time.sleep(0.1)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.step_mode = False
        self._read(10)  # welcome banner + prompt

    def _read(self, timeout: float) -> str:
        self.sock.settimeout(timeout)
        buf = bytearray()
        while True:
            try:
                d = self.sock.recv(1 << 16)
            except socket.timeout:
                raise ZrcpError(f"ZRCP timeout after {timeout}s (partial: {bytes(buf[-200:])!r})") from None
            if not d:
                raise ZrcpError("ZRCP connection closed (emulator exited?)")
            buf += d
            m = _PROMPT.search(buf)
            if m:
                self.step_mode = m.group(1) is not None
                return buf[: m.start()].decode("latin-1").replace("\r", "")

    def cmd(self, line: str, timeout: float = 30.0, check: bool = True) -> str:
        """Send one command, return its output (without the prompt)."""
        if "\n" in line:
            raise ValueError("one command per call")
        self.sock.sendall(line.encode("latin-1") + b"\n")
        out = self._read(timeout)
        if check and out.lstrip().startswith(("ERROR", "Error")):
            raise ZrcpError(f"{line.split()[0]}: {out.strip()}")
        return out

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def kv(text: str) -> dict:
    """Parse "a=1 b=xyz" (the zxh-* reply format) into a dict of strings."""
    out = {}
    for tok in text.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def kv_tail(text: str, key: str) -> str | None:
    """Value of `key=` that runs to the end of the line (e.g. break=PC=8000H)."""
    m = re.search(rf"(?:^|\s){re.escape(key)}=(.*)$", text.strip())
    return m.group(1) if m else None
