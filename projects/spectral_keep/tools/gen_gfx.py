#!/usr/bin/env python3
"""Spectral Keep's pictures for the Spectrum Next, from the original's own
renderer (tools/export.c runs vox.c and the room builder on the host).

Layer 2 (320 x 256, the original's 320 x 240 frame 8 lines down):

  Each room is drawn on the Next when you enter it, from its pieces in
  the original's order (export.c's draw list). A room has its own Layer 2
  palette: entry 0 is transparent (the copper's backdrop shows through),
  1..127 are the room's colours (median cut over the room's own picture)
  and 128..255 are the same at half brightness, so a shadow is "OR 0x80".
  Floor tiles, wall bricks and blocks come in brightness variants of one
  shape (MatLib.Vary): the base is stored once and each variant is a
  128-byte colour translation table (LUT).

  Pieces are column RLE (Layer 2's 320 mode is column-major), the format
  of sdk/include/l2rle.h (nextpic.piece): per column, runs of (skip, len,
  len pixels), ended by skip 0xFF. The same runs are the piece's
  silhouette, which the game uses to hide sprites behind it. A room's blob
  (palette, colour tables, pieces, draw list) fits one 8K page.

Sprites: every model frame, in one 256-colour sprite palette (0xE3
transparent), as a group of 16 x 16 patterns (nextspr.h, one DMA upload),
packed into 8K pages without straddling.

Outputs:
  gfx/bank_NN.bin      16K banks for gfx_data.asm
  gfx/expected_gate_l2.png  the gate room as the Next should draw it (test.py
                       checks Layer 2 against it; black = not the room)
  src/gfx_data.c/.h    directories (frames, rooms), the sprite palette,
                       the shadow shape
  build/host/next_<l>_<r>.png   the room as the Next will draw it, next to
                       the original's (ref_*.bin), for a look

    python3 tools/gen_gfx.py build/host
"""
import glob
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "sdk", "tools"))
from nextpic import group_patterns, nearest, piece, quantize, rgb9_from_rgb  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FIRST_BANK = 16                 # 16K banks 16.. (8K pages 32..); L2 is 9-13
TRANSP = 0xE3


def pal9(c):
    return rgb9_from_rgb(*(int(v) for v in c))


def l2_safe(c):
    """Layer 2's transparency compares a colour's top 8 bits with 0xE3"""
    v = pal9(c)
    return v ^ 2 if (v >> 1) == TRANSP else v

# piece ids (export.c)
P_TILE, P_BRICK, P_STEP, P_BLOCK, P_ARCH, P_SPIKES, P_GATE, P_THRONE, P_COUNT = 0, 4, 8, 10, 16, 18, 19, 21, 22


def rgb15_to_rgb(v):
    r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)], -1).astype(np.int32)


def read_sprite(b, o):
    w, h, ax, ay = struct.unpack_from("<HHhh", b, o)
    o += 8
    px = np.frombuffer(b, dtype="<u2", count=w * h, offset=o).reshape(h, w).copy() if w * h else np.zeros((0, 0), np.uint16)
    return dict(w=w, h=h, ax=ax, ay=ay, px=px), o + 2 * w * h


class Pages:
    """8K pages from FIRST_BANK*2 up; an item never straddles a page unless
    it asks for a run of whole pages"""

    def __init__(self):
        self.data = bytearray()

    def page_of(self, off):
        return FIRST_BANK * 2 + off // 8192

    def add(self, blob, align_page=False):
        if align_page or (len(self.data) % 8192) + len(blob) > 8192:
            self.data += bytes((-len(self.data)) % 8192)
        off = len(self.data)
        self.data += blob
        return self.page_of(off), off % 8192


# draw-list kinds (scene.c)
OK_FLOOR, OK_BLOCK, OK_SPIKES, OK_THRONE, OK_GATE, OK_ARCH = range(6)


def op_kind(p, kind):
    if kind == 0:
        return OK_FLOOR
    if kind == 3:
        return OK_ARCH
    if kind == 2:
        return OK_GATE
    return OK_SPIKES if p == P_SPIKES else OK_THRONE if p == P_THRONE else OK_BLOCK


def main(host):
    os.makedirs(os.path.join(ROOT, "gfx"), exist_ok=True)
    pages = Pages()

    # ---------------------------------------------------------------- sprites
    b = open(os.path.join(host, "sprites.bin"), "rb").read()
    n = struct.unpack_from("<H", b)[0]
    o = 2
    sprites = []
    for _ in range(n):
        s, o = read_sprite(b, o)
        sprites.append(s)
    shadows = sprites[-3:]
    frames = sprites[:-3]
    allpix = np.concatenate([rgb15_to_rgb(s["px"][s["px"] != 0]) for s in frames])
    spal = quantize(np.unique(allpix, axis=0).astype(np.uint8), 255)
    pal_entries = [i for i in range(256) if i != TRANSP][:len(spal)]
    spr_pal = [0] * 256
    for k, i in enumerate(pal_entries):
        spr_pal[i] = pal9(spal[k])
    spr_pal[TRANSP] = pal9((255, 0, 255))
    lut = np.array(pal_entries, np.int32)
    frame_dir = []
    for s in frames:
        idx = lut[nearest(rgb15_to_rgb(s["px"]), spal)]
        idx[s["px"] == 0] = TRANSP
        data, cols, rows = group_patterns(idx)
        page, off = pages.add(data)
        frame_dir.append((page, 0x4000 + off, cols, rows, s["ax"], s["ay"]))
    sh = shadows[1]
    shadow_rows = []
    for y in range(sh["h"]):
        xs = [x for x in range(sh["w"]) if sh["px"][y, x]]
        shadow_rows.append((min(xs), max(xs) - min(xs) + 1) if xs else (0, 0))

    # ---------------------------------------------------------------- rooms
    room_dir = []
    names = sorted(glob.glob(os.path.join(host, "room_*.bin")),
                   key=lambda p: tuple(int(v) for v in os.path.basename(p)[5:-4].split("_")))
    for path in names:
        lr = os.path.basename(path)[5:-4]
        b = open(path, "rb").read()
        np_, o = struct.unpack_from("<H", b)[0], 2
        pieces = []
        for _ in range(np_):
            s, o = read_sprite(b, o)
            pieces.append(s)
        nops = struct.unpack_from("<H", b, o)[0]
        o += 2
        ops = [struct.unpack_from("<BBhhhhh", b, o + 12 * i) for i in range(nops)]
        o += 12 * nops
        ox, oy = struct.unpack_from("<hh", b, o)

        # the room's picture in true colour, for its palette
        canvas = np.zeros((240, 320, 3), np.int32)
        cover = np.zeros((240, 320), bool)
        for p, kind, x, y, cx, cy, cz in ops:
            s = pieces[p]
            ys, xs = np.nonzero(s["px"] != 0)
            X, Y = xs + x, ys + y
            ok = (X >= 0) & (X < 320) & (Y >= 0) & (Y < 240)
            canvas[Y[ok], X[ok]] = rgb15_to_rgb(s["px"][ys[ok], xs[ok]])
            cover[Y[ok], X[ok]] = True
        rpal = quantize(np.unique(canvas[cover], axis=0).astype(np.uint8), 127)

        # bases: one variant of each family is stored, the others are colour
        # tables from it (MatLib.Vary scales brightness only)
        families = [(P_TILE, 4), (P_BRICK, 4), (P_BLOCK, 3), (P_BLOCK + 3, 3)]
        base_of = {i: i for i in range(P_COUNT)}
        for first, cnt in families:
            for v in range(cnt):
                base_of[first + v] = first
        used_pieces = sorted({p for p, *_ in ops})
        bases = sorted({base_of[p] for p in used_pieces})
        piece_idx, piece_blob = {}, {}
        for p in bases:
            s = pieces[p]
            idx = nearest(rgb15_to_rgb(s["px"]), rpal) + 1
            piece_idx[p] = idx
            piece_blob[p] = piece(idx, s["px"] != 0)
        luts, lut_of = [], {}
        for p in used_pieces:
            if base_of[p] == p:
                lut_of[p] = 0
                continue
            base_idx = piece_idx[base_of[p]]
            var_rgb = rgb15_to_rgb(pieces[p]["px"])
            m = pieces[p]["px"] != 0
            table = list(range(128))
            for k in np.unique(base_idx[m]):
                c = var_rgb[(base_idx == k) & m].mean(0)
                table[k] = int(nearest(c.reshape(1, 3).astype(np.int32), rpal)[0]) + 1
            luts.append(bytes(table))
            lut_of[p] = len(luts)

        # the blob: header, palette, tables, pieces, draw list (one 8K page)
        pnum = {p: i for i, p in enumerate(bases)}
        head = 9 + 2 * len(bases)
        body = bytearray()
        pal_off = head
        for c in rpal:
            body += struct.pack("<H", l2_safe(c))
        body += bytes(2 * (127 - len(rpal)))
        lut_off = head + len(body)
        for t in luts:
            body += t
        poffs = []
        for p in bases:
            poffs.append(head + len(body))
            body += piece_blob[p]
        ops_off = head + len(body)
        for p, kind, x, y, cx, cy, cz in ops:
            body += struct.pack("<BBBhhbbb", pnum[base_of[p]], lut_of[p], op_kind(p, kind), x, y + 8, cx, cy, cz)
        blob = struct.pack("<HHHHB", pal_off, lut_off, ops_off, nops, len(bases)) + \
            b"".join(struct.pack("<H", v) for v in poffs) + body
        assert len(blob) <= 8192, (lr, len(blob))
        page, off = pages.add(blob, align_page=True)
        room_dir.append((lr, page, len(blob), ox, oy + 8))

        # what the Next will show, beside the original's picture
        out = np.zeros((256, 320, 3), np.uint8)
        out[:, :] = (18, 19, 26)
        npal = np.vstack([[0, 0, 0], rpal])
        for p, kind, x, y, cx, cy, cz in ops:
            s = pieces[p]
            idx = piece_idx[base_of[p]]
            if lut_of[p]:
                idx = np.array(list(luts[lut_of[p] - 1]))[idx]
            ys, xs = np.nonzero(s["px"] != 0)
            X, Y = xs + x, ys + y + 8
            ok = (X >= 0) & (X < 320) & (Y >= 0) & (Y < 256)
            out[Y[ok], X[ok]] = npal[idx[ys[ok], xs[ok]]]
        ref = np.fromfile(os.path.join(host, f"ref_{lr}.bin"), "<u2").reshape(240, 320)
        both = np.zeros((256, 640, 3), np.uint8)
        both[:, :320] = out
        both[8:248, 320:] = rgb15_to_rgb(ref)
        Image.fromarray(both).save(os.path.join(host, f"next_{lr}.png"))
        if lr == "0_0":                  # the test compares the Next's Layer 2 with this
            ref_l2 = out.copy()
            room = np.zeros((256, 320), bool)
            for p, kind, x, y, cx, cy, cz in ops:
                ys, xs = np.nonzero(pieces[p]["px"] != 0)
                X, Y = xs + x, ys + y + 8
                ok = (X >= 0) & (X < 320) & (Y >= 0) & (Y < 256)
                room[Y[ok], X[ok]] = True
            ref_l2[~room] = 0
            Image.fromarray(ref_l2).save(os.path.join(ROOT, "gfx", "expected_gate_l2.png"))
        print(f"room {lr}: {len(rpal)} colours, {len(bases)} pieces, {len(luts)} tables, {nops} ops, {len(blob)} bytes")

    # ---------------------------------------------------------------- out
    data = bytes(pages.data) + bytes((-len(pages.data)) % 16384)
    nbanks = len(data) // 16384
    for f in glob.glob(os.path.join(ROOT, "gfx", "bank_*.bin")):
        os.remove(f)
    for i in range(nbanks):
        open(os.path.join(ROOT, "gfx", f"bank_{FIRST_BANK + i}.bin"), "wb").write(data[i * 16384:(i + 1) * 16384])
    with open(os.path.join(ROOT, "src", "gfx_data.asm"), "w") as f:
        f.write("; generated by tools/gen_gfx.py - do not edit: the pictures, straight\n"
                "; into Next memory banks by the .nex (sprite frames, then a room a page)\n\n")
        for i in range(nbanks):
            f.write(f"SECTION BANK_{FIRST_BANK + i}\nBINARY \"gfx/bank_{FIRST_BANK + i}.bin\"\n")
    with open(os.path.join(ROOT, "src", "gfx_data.h"), "w") as f:
        f.write(f"""/* generated by tools/gen_gfx.py - do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>
/* a model frame: a group of cols x rows sprite patterns at 8K page `page`,
   address `addr` when the page is in MMU slot 2; the model's origin is at
   (ax, ay) in it */
typedef struct {{ uint8_t page; uint16_t addr; uint8_t cols, rows; int8_t ax, ay; }} Frame;
/* a room's blob: 8K page (mapped at 0x6000), where its origin is on the 320 x 256 screen */
typedef struct {{ uint8_t page; int16_t ox, oy; }} RoomGfx;
#define NFRAMES {len(frame_dir)}
#define NROOMGFX {len(room_dir)}
#define GFX_FIRST_BANK {FIRST_BANK}
#define GFX_NBANKS {nbanks}
#define SHADOW_W {sh['w']}
#define SHADOW_H {sh['h']}
#define SHADOW_AX {sh['ax']}
#define SHADOW_AY {sh['ay']}
/* frame numbers (export.c's order) */
#define FR_WALKER(m, f, k) ((m) * 24 + (f) * 3 + (k))
#define FR_SAGE 72
#define FR_GHOST 80
#define FR_BOUNCER 88
#define FR_RELIC 96
#define FR_KEY 112
#define FR_POTION 128
#define FR_CRATE 129
#define FR_LIFT 130
#define FR_PISTON 131
/* draw-list kinds */
enum {{ OK_FLOOR, OK_BLOCK, OK_SPIKES, OK_THRONE, OK_GATE, OK_ARCH }};
extern const Frame frames[NFRAMES];
extern const RoomGfx room_gfx[NROOMGFX];
extern const uint16_t spr_pal[256];
extern const uint8_t shadow_rows[SHADOW_H][2];
#endif
""")
    with open(os.path.join(ROOT, "src", "gfx_data.c"), "w") as f:
        f.write('/* generated by tools/gen_gfx.py - do not edit */\n#include "gfx_data.h"\n\n')
        f.write("const Frame frames[NFRAMES] = {\n")
        for page, addr, cols, rows, ax, ay in frame_dir:
            f.write(f"    {{ {page}, 0x{addr:04X}, {cols}, {rows}, {ax}, {ay} }},\n")
        f.write("};\n\nconst RoomGfx room_gfx[NROOMGFX] = {\n")
        for lr, page, ln, ox, oy in room_dir:
            f.write(f"    {{ {page}, {ox}, {oy} }},   /* {lr}: {ln} bytes */\n")
        f.write("};\n\nconst uint16_t spr_pal[256] = {\n")
        for i in range(0, 256, 16):
            f.write("    " + ", ".join(f"0x{v:03X}" for v in spr_pal[i:i + 16]) + ",\n")
        f.write("};\n\nconst uint8_t shadow_rows[SHADOW_H][2] = {\n")
        f.write("    " + ", ".join(f"{{ {a}, {b} }}" for a, b in shadow_rows) + "\n};\n")
    print(f"{len(frame_dir)} frames, {len(room_dir)} rooms, {nbanks} banks ({FIRST_BANK}..{FIRST_BANK + nbanks - 1})")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "host"))
