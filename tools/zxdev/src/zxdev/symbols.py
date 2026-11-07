"""Symbols from z88dk .map files (zcc -m) and sjasmplus --sym/--lst files.

z88dk map lines look like:

    _main                           = $8CEB ; addr, public, , hello_c, code_compiler, hello.c:5

C symbols carry a leading underscore; lookups accept either spelling.
"""
from __future__ import annotations

import re
from pathlib import Path

_Z88DK = re.compile(r"^(\S+)\s*=\s*\$([0-9A-Fa-f]+)\s*;\s*(.*)$")
_SJASM = re.compile(r"^(\S+?):?\s+(?:EQU|equ)\s+0x([0-9A-Fa-f]+)")


def _cpu(a: int, expr) -> int:
    """a CPU address (wrapping below 0), or a clear error for anything above 64K"""
    if a > 0xFFFF:
        raise ValueError(f"{expr!r} is {a:#x}, above the 64K CPU view: use a banked symbol's "
                         f"name (BANK_n/PAGE_n sections are decoded), or bank=/page= with an offset")
    return a & 0xFFFF


class Symbols:
    def __init__(self):
        self.addr: dict[str, int] = {}
        self.info: dict[str, dict] = {}

    @classmethod
    def load(cls, path) -> "Symbols":
        s = cls()
        if path:
            s.add_file(path)
        return s

    def add_file(self, path):
        p = Path(path)
        if not p.exists():
            return
        for line in p.read_text(errors="replace").splitlines():
            m = _Z88DK.match(line)
            if m:
                name, val = m.group(1), int(m.group(2), 16)
                f = [x.strip() for x in m.group(3).split(",")]
                f += [""] * (6 - len(f))
                kind, scope, module, section, src = f[0], f[1], f[3], f[4], f[5]
                if kind != "addr" or not section:
                    continue  # constants and section-less markers are not locations
                old = self.info.get(name)
                if old and old["scope"] == "public" and scope != "public":
                    continue
                self.addr[name] = val
                self.info[name] = {"scope": scope, "module": module, "section": section}
                continue
            m = _SJASM.match(line)
            if m:
                self.addr[m.group(1)] = int(m.group(2), 16)

    def __len__(self):
        return len(self.addr)

    def lookup(self, name: str) -> int | None:
        for n in (name, "_" + name):
            if n in self.addr:
                return self.addr[n]
        return None

    def banked(self, name: str) -> int | None:
        """A symbol in a z88dk BANK_n / BANK_n_L / BANK_n_H / PAGE_n section is
        linked at bank << 16 | address. Its place in the Next's RAM as an
        offset from page 0 (page * 8192 + offset), or None."""
        for n in (name, "_" + name):
            if n in self.addr:
                val, sec = self.addr[n], self.info.get(n, {}).get("section", "")
                m = re.fullmatch(r"(PAGE|BANK)_(\d+)(_[LH])?", sec)
                if val <= 0xFFFF or not m:
                    return None
                k, low = int(m.group(2)), val & 0xFFFF
                if m.group(1) == "PAGE":
                    return k * 8192 + (low & 0x1FFF)
                if m.group(3):
                    return (2 * k + (m.group(3) == "_H")) * 8192 + (low & 0x1FFF)
                return 2 * k * 8192 + (low & 0x3FFF)
        return None

    def locate(self, expr, regs=None) -> tuple[int, int | None]:
        """(CPU address, Next RAM offset if expr is a banked symbol[+-off] else None)"""
        if isinstance(expr, str):
            e = expr.strip()
            m = re.fullmatch(r"(.+?)\s*([+-])\s*((?:0x|\$)?[0-9A-Fa-f]+h?)", e)
            base, off = (m.group(1), _num(m.group(3)) * (1 if m.group(2) == "+" else -1)) if m else (e, 0)
            lin = self.banked(base)
            if lin is not None:
                return (self.lookup(base) + off) & 0xFFFF, lin + off
        return self.resolve(expr, regs), None

    def resolve(self, expr, regs=None) -> int:
        """Number, symbol, symbol+off, or a register name (pc, sp, hl...) -> address."""
        if isinstance(expr, int):
            return _cpu(expr, expr)
        e = str(expr).strip()
        m = re.fullmatch(r"(.+?)\s*([+-])\s*((?:0x|\$)?[0-9A-Fa-f]+h?)", e)
        if m and not re.fullmatch(r"(0x|\$)?[0-9A-Fa-f]+h?", e):
            base = self.resolve(m.group(1), regs)
            off = _num(m.group(3))
            return _cpu(base + off if m.group(2) == "+" else base - off, expr)
        if regs is not None and e.upper() in regs:
            return regs[e.upper()]
        v = self.lookup(e)
        if v is not None:
            # a banked symbol (bank << 16 | address): the address the CPU sees
            # it at when its page is mapped (read_mem/write_mem use locate())
            if v > 0xFFFF and self.banked(e) is not None:
                return v & 0xFFFF
            return _cpu(v, expr)
        try:
            n = _num(e)
        except ValueError:
            raise ValueError(f"unknown symbol or address: {expr!r}") from None
        return _cpu(n, expr)

    def name_of(self, addr: int) -> str | None:
        """Nearest symbol at or below addr, as name or name+0xN. Named code and
        data (public, or static C functions/variables) win over compiler-local
        labels (l_main_00116, i_1) and linker symbols (__head)."""
        def named(n):
            return not (n.startswith(("l_", "i_", "__")) or n[:1].isdigit())
        for pick in (named, lambda n: True):
            best = None
            for n, a in self.addr.items():
                if a > addr or addr - a > 0x1000 or not pick(n):
                    continue
                if best is None or a > best[1] or (a == best[1] and n.startswith("_")):
                    best = (n, a)
            if best:
                n, a = best
                return n if addr == a else f"{n}+0x{addr - a:x}"
        return None

    def search(self, pattern: str = "", limit: int = 200) -> list[dict]:
        rx = re.compile(pattern or ".", re.I)
        out = [{"name": n, "addr": a, "hex": f"${a:04X}", **self.info.get(n, {})}
               for n, a in self.addr.items() if rx.search(n)]
        out.sort(key=lambda d: d["addr"])
        return out[:limit]


def _num(s: str) -> int:
    s = s.strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    if s.startswith("$") or s.startswith("#"):
        return int(s[1:], 16)
    if s.lower().endswith("h") and len(s) > 1:
        return int(s[:-1], 16)
    return int(s, 10)
