#!/usr/bin/env python3
"""SD card images (MBR + one FAT16 partition) for the Spectrum Next, in pure Python.

Reads an existing image (e.g. ZEsarUX's tbblue.mmc, which holds NextZXOS) into a
tree, and writes a new image from a tree. Used by `./zx arcade` to make an SD
card with NextZXOS plus the games; no mtools or hdfmonkey needed.

    tree = read_image("tbblue.mmc")              # {"nextzxos/autoexec.bas": b"...", "games": None, ...}
    tree = drop(tree, ["games", "demos"])        # remove directories (and what's in them)
    tree["arcade/menu.bin"] = data                # add / replace files (parent dirs are made)
    write_image(tree, "arcade.mmc", size_mb=64)

A tree maps "dir/sub/file" paths to bytes (a file) or None (a directory). Names
keep their case: 8.3 names are written as 8.3 entries (all-lower-case parts
through the NT case flags), mixed-case 8.3 names as the upper-case short name
plus long-name (VFAT) entries, and longer names as long-name entries plus a
NAME~N short alias. Files are written contiguously, in path
order, so writing the same tree gives the same image.

CLI:
    fatimg.py ls  IMAGE [DIR]
    fatimg.py cat IMAGE PATH > out
"""
import struct
import sys

SECTOR = 512
PART_START = 2048                 # first partition at 1 MB, like tbblue.mmc
DATE = (2026 - 1980) << 9 | 11 << 5 | 6     # fixed timestamps: reproducible images
TIME = 12 << 11


# ------------------------------------------------------------------ reading

class FatImage:
    """A FAT12/16 partition inside an image (first MBR partition, or no MBR)."""

    def __init__(self, data: bytes):
        self.data = data
        start = 0
        if data[510:512] == b"\x55\xaa" and data[0] not in (0xEB, 0xE9):
            start = struct.unpack_from("<I", data, 446 + 8)[0] * SECTOR
        b = data[start:start + SECTOR]
        (self.bps, self.spc, reserved, nfats, self.root_entries, total16, _media,
         self.fat_sectors) = struct.unpack_from("<HBHBHHBH", b, 11)
        total = total16 or struct.unpack_from("<I", b, 32)[0]
        if self.fat_sectors == 0:
            raise ValueError("FAT32 is not supported (only FAT12/16)")
        self.fat_off = start + reserved * self.bps
        self.root_off = self.fat_off + nfats * self.fat_sectors * self.bps
        self.data_off = self.root_off + (self.root_entries * 32 + self.bps - 1) // self.bps * self.bps
        self.cluster_bytes = self.spc * self.bps
        clusters = (total - (self.data_off - start) // self.bps) // self.spc
        self.fat12 = clusters < 4085

    def _next(self, c: int) -> int:
        if self.fat12:
            v = struct.unpack_from("<H", self.data, self.fat_off + c * 3 // 2)[0]
            v = v >> 4 if c & 1 else v & 0xFFF
            return v if v < 0xFF8 else -1
        v = struct.unpack_from("<H", self.data, self.fat_off + c * 2)[0]
        return v if v < 0xFFF8 else -1

    def _chain(self, c: int) -> bytes:
        out = bytearray()
        seen = 0
        while 2 <= c and seen < 1 << 16:
            off = self.data_off + (c - 2) * self.cluster_bytes
            out += self.data[off:off + self.cluster_bytes]
            c = self._next(c)
            seen += 1
        return bytes(out)

    def _entries(self, raw: bytes):
        """(name, is_dir, first_cluster, size) for each entry of a directory."""
        lfn = {}
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = {}
                continue
            attr = e[11]
            if attr == 0x0F:
                seq = e[0] & 0x1F
                part = (e[1:11] + e[14:26] + e[28:32]).decode("utf-16-le", "replace")
                lfn[seq] = part.split("\0")[0]
                continue
            if attr & 0x08:            # volume label
                lfn = {}
                continue
            base, ext = e[0:8].decode("latin-1").rstrip(), e[8:11].decode("latin-1").rstrip()
            if base in (".", ".."):
                lfn = {}
                continue
            if lfn:
                name = "".join(lfn[k] for k in sorted(lfn))
            else:
                if e[12] & 0x08:
                    base = base.lower()
                if e[12] & 0x10:
                    ext = ext.lower()
                name = base + ("." + ext if ext else "")
            lfn = {}
            cl = struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            yield name, bool(attr & 0x10), cl, size

    def listdir(self, path: str = ""):
        raw = self.data[self.root_off:self.root_off + self.root_entries * 32]
        for part in [p for p in path.split("/") if p]:
            for name, is_dir, cl, _ in self._entries(raw):
                if is_dir and name.lower() == part.lower():
                    raw = self._chain(cl)
                    break
            else:
                raise FileNotFoundError(path)
        return list(self._entries(raw))

    def tree(self) -> dict:
        out = {}

        def walk(raw, prefix):
            for name, is_dir, cl, size in self._entries(raw):
                p = prefix + name
                if is_dir:
                    out[p] = None
                    walk(self._chain(cl), p + "/")
                else:
                    out[p] = self._chain(cl)[:size] if size else b""

        walk(self.data[self.root_off:self.root_off + self.root_entries * 32], "")
        return out

    def read(self, path: str) -> bytes:
        d, _, f = path.rpartition("/")
        for name, is_dir, cl, size in self.listdir(d):
            if not is_dir and name.lower() == f.lower():
                return self._chain(cl)[:size] if size else b""
        raise FileNotFoundError(path)


def read_image(path) -> dict:
    with open(path, "rb") as f:
        return FatImage(f.read()).tree()


def find(tree: dict, path: str):
    """The key in tree that matches path case-insensitively (FAT semantics), or None."""
    want = path.strip("/").lower()
    return next((k for k in tree if k.lower() == want), None)


def drop(tree: dict, paths) -> dict:
    """tree without these files/directories (case-insensitive) and their contents."""
    gone = [p.strip("/").lower() for p in paths]
    return {k: v for k, v in tree.items()
            if not any(k.lower() == g or k.lower().startswith(g + "/") for g in gone)}


def put(tree: dict, path: str, data: bytes):
    """Add or replace a file (matching an existing name case-insensitively); make parents."""
    path = path.strip("/")
    old = find(tree, path)
    if old is not None:
        del tree[old]
    parts = path.split("/")
    for i in range(1, len(parts)):
        d = "/".join(parts[:i])
        if find(tree, d) is None:
            tree[d] = None
    tree[path] = data


# ------------------------------------------------------------------ writing

_SHORT_OK = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")


def _short_83(name: str):
    """(11-byte short name, NT case flags, needs long-name entries) when the name
    fits 8.3 once upper-cased, else None. Mixed case keeps its upper-case short
    name (as Windows writes it) plus long-name entries: NextZXOS's boot loader
    only matches short names."""
    base, dot, ext = name.partition(".")
    if "." in ext or not base or len(base) > 8 or len(ext) > 3:
        return None
    flags, mixed = 0, False
    for part, flag in ((base, 0x08), (ext, 0x10)):
        if part != part.upper():
            if part != part.lower():
                mixed = True
            flags |= flag
    up = (base.upper(), ext.upper())
    if not all(c in _SHORT_OK for c in up[0] + up[1]):
        return None
    return (up[0].ljust(8) + up[1].ljust(3)).encode("ascii"), 0 if mixed else flags, mixed


def _alias(name: str, used: set) -> bytes:
    base, _, ext = name.rpartition(".") if "." in name else (name, "", "")
    clean = lambda s: "".join(c for c in s.upper() if c in _SHORT_OK)  # noqa: E731
    b, x = clean(base) or "X", clean(ext)[:3]
    for n in range(1, 100000):
        tail = f"~{n}"
        short = (b[:8 - len(tail)] + tail).ljust(8) + x.ljust(3)
        if short not in used:
            used.add(short)
            return short.encode("ascii")
    raise ValueError("too many aliases")


def _lfn_entries(name: str, short: bytes) -> bytes:
    csum = 0
    for c in short:
        csum = (((csum & 1) << 7) + (csum >> 1) + c) & 0xFF
    units = list(name.encode("utf-16-le"))
    chars = [units[i] | units[i + 1] << 8 for i in range(0, len(units), 2)]
    if len(chars) % 13:
        chars += [0] + [0xFFFF] * (12 - len(chars) % 13)
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    out = b""
    for seq in range(len(parts), 0, -1):        # stored last part first
        p = parts[seq - 1]
        e = bytearray(32)
        e[0] = seq | (0x40 if seq == len(parts) else 0)
        e[1:11] = struct.pack("<5H", *p[0:5])
        e[11], e[13] = 0x0F, csum
        e[14:26] = struct.pack("<6H", *p[5:11])
        e[28:32] = struct.pack("<2H", *p[11:13])
        out += bytes(e)
    return out


def _dirent(short: bytes, attr: int, case: int, cluster: int, size: int) -> bytes:
    return struct.pack("<11sBBBHHHHHHHI", short, attr, case, 0, TIME, DATE, DATE, 0,
                       TIME, DATE, cluster, size)


def write_image(tree: dict, out_path, size_mb: int = 64, label: str = "SPECNEXT"):
    """Write tree as a FAT16 image with an MBR (size_mb >= 16)."""
    total = size_mb * 1024 * 1024 // SECTOR
    psec = total - PART_START
    root_entries = 512
    spc = 1
    while True:
        fat_sectors = ((psec // spc) + 2) * 2 // SECTOR + 1
        clusters = (psec - 1 - 2 * fat_sectors - root_entries * 32 // SECTOR) // spc
        if clusters < 65525 - 16:
            break
        spc *= 2
    if clusters < 4085:
        raise ValueError("image too small for FAT16")
    cbytes = spc * SECTOR

    # children of each directory, sorted (dirs and files mixed, by name)
    norm = {}
    for p, v in tree.items():
        parts = p.strip("/").split("/")
        for i in range(1, len(parts)):
            norm.setdefault("/".join(parts[:i]), None)
        norm["/".join(parts)] = v
    kids = {"": []}
    for p in sorted(norm, key=str.lower):
        kids.setdefault(p, []) if norm[p] is None else None
        parent = p.rpartition("/")[0]
        kids.setdefault(parent, []).append(p)

    # directory sizes depend on entry counts; lay out clusters in path order
    def dir_entries(d):
        used, n = set(), 0 if d == "" else 2
        for p in kids[d]:
            name = p.rpartition("/")[2]
            s = _short_83(name)
            if s and s[0].decode() not in used:
                used.add(s[0].decode())
                n += 1 + (s[2] and (len(name) + 12) // 13)
            else:
                n += 1 + (len(name) + 12) // 13
        return n

    alloc, nxt = {}, 2
    for p in sorted(norm, key=str.lower):
        size = dir_entries(p) * 32 if norm[p] is None else len(norm[p])
        n = max(1, -(-size // cbytes)) if (norm[p] is None or size) else 0
        alloc[p] = (nxt if n else 0, n)
        nxt += n
    if nxt - 2 > clusters:
        raise ValueError(f"tree needs {(nxt - 2) * cbytes} bytes, image holds {clusters * cbytes}")

    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
    for first, n in alloc.values():
        for k in range(n):
            struct.pack_into("<H", fat, (first + k) * 2, first + k + 1 if k < n - 1 else 0xFFFF)

    def dir_bytes(d):
        out = bytearray()
        if d:
            out += _dirent(b".          ", 0x10, 0, alloc[d][0], 0)
            parent = d.rpartition("/")[0]
            out += _dirent(b"..         ", 0x10, 0, alloc[parent][0] if parent else 0, 0)
        used = set()
        for p in kids[d]:
            name = p.rpartition("/")[2]
            s = _short_83(name)
            if s and s[0].decode() not in used:
                used.add(s[0].decode())
                short, case = s[0], s[1]
                lfn = _lfn_entries(name, short) if s[2] else b""
            else:
                short, case = _alias(name, used), 0
                lfn = _lfn_entries(name, short)
            is_dir = norm[p] is None
            out += lfn + _dirent(short, 0x10 if is_dir else 0x20, case, alloc[p][0],
                                 0 if is_dir else len(norm[p]))
        return bytes(out)

    root = dir_bytes("")
    if len(root) > root_entries * 32:
        raise ValueError("too many entries in the root directory")

    reserved = 1
    boot = bytearray(SECTOR)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"SPECNEXT"
    struct.pack_into("<HBHBHHBHHHII", boot, 11, SECTOR, spc, reserved, 2, root_entries,
                     psec if psec < 65536 else 0, 0xF8, fat_sectors, 32, 64, PART_START,
                     psec if psec >= 65536 else 0)
    struct.pack_into("<BBBI11s8s", boot, 36, 0x80, 0, 0x29, 0x5A584E58,
                     label.upper().ljust(11)[:11].encode(), b"FAT16   ")
    boot[510:512] = b"\x55\xaa"

    def chs(lba):
        c, rem = divmod(lba, 64 * 32)
        h, s = divmod(rem, 32)
        c = min(c, 1023)
        return bytes([h, (s + 1) | (c >> 2 & 0xC0), c & 0xFF])

    mbr = bytearray(SECTOR)
    mbr[446:462] = (b"\x00" + chs(PART_START) + bytes([0x06 if psec >= 65536 else 0x04])
                    + chs(total - 1) + struct.pack("<II", PART_START, psec))
    mbr[510:512] = b"\x55\xaa"

    poff = PART_START * SECTOR
    fat_off = poff + reserved * SECTOR
    root_off = fat_off + 2 * fat_sectors * SECTOR
    data_off = root_off + root_entries * 32
    with open(out_path, "wb") as f:
        f.truncate(total * SECTOR)
        for off, blob in ((0, mbr), (poff, boot), (fat_off, fat), (fat_off + len(fat), fat), (root_off, root)):
            f.seek(off)
            f.write(blob)
        for p, (first, n) in alloc.items():
            if not n:
                continue
            f.seek(data_off + (first - 2) * cbytes)
            f.write(dir_bytes(p) if norm[p] is None else norm[p])


def main(argv):
    if len(argv) < 2 or argv[0] not in ("ls", "cat"):
        print(__doc__)
        return 2
    with open(argv[1], "rb") as f:
        img = FatImage(f.read())
    if argv[0] == "ls":
        for name, is_dir, _, size in img.listdir(argv[2] if len(argv) > 2 else ""):
            print(f"{'<DIR>' if is_dir else size:>10}  {name}")
    else:
        sys.stdout.buffer.write(img.read(argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
