"""Render individual display layers from emulated memory (host side).

The composited frame comes from ZEsarUX (Emulator.screenshot). These render
one layer each, at the hardware's native resolution, straight from RAM and
the Next's registers/palettes, so tests can check what a program put in a
layer even when another layer covers it:

  ula     256x192 bitmap + attributes (screen at 0x4000; on 128K/Next the
          bank 5 copy, or bank 7 when the shadow screen is shown)
  layer2  Next Layer 2: 256x192 / 320x256 (8 bpp) or 640x256 (4 bpp), with
          its palette; transparent pixels (NextReg 0x14) shown as magenta
          unless transparent=True (then alpha)
  sprites Next hardware sprites on the 320x256 sprite plane (paper at
          32,32): visibility, 8 bpp / 4 bpp patterns, palette offset, X/Y
          mirror, rotate, X/Y scaling, relative sprites (composite type; unified
          sprites are drawn with their own transform, not the anchor's)
"""
from __future__ import annotations

from PIL import Image

NEXT_RAM = 0x40000          # zone-0 offset of Next RAM page 0 in ZEsarUX

# ULA colours (ZEsarUX's default palette: 0xC0 normal, 0xFF bright)
_ULA = [(0, 0, 0), (0, 0, 0xC0), (0xC0, 0, 0), (0xC0, 0, 0xC0),
        (0, 0xC0, 0), (0, 0xC0, 0xC0), (0xC0, 0xC0, 0), (0xC0, 0xC0, 0xC0)]
_ULA_BRIGHT = [(0, 0, 0), (0, 0, 0xFF), (0xFF, 0, 0), (0xFF, 0, 0xFF),
               (0, 0xFF, 0), (0, 0xFF, 0xFF), (0xFF, 0xFF, 0), (0xFF, 0xFF, 0xFF)]


def render_ula(screen: bytes, flash_phase: bool = False) -> Image.Image:
    """6912 bytes (bitmap + attributes) -> 256x192 RGB."""
    img = Image.new("RGB", (256, 192))
    px = img.load()
    for y in range(192):
        row = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2)
        for cx in range(32):
            bits = screen[row + cx]
            attr = screen[6144 + (y >> 3) * 32 + cx]
            pal = _ULA_BRIGHT if attr & 0x40 else _ULA
            ink, paper = pal[attr & 7], pal[(attr >> 3) & 7]
            if attr & 0x80 and flash_phase:
                ink, paper = paper, ink
            for b in range(8):
                px[cx * 8 + b, y] = ink if bits & (0x80 >> b) else paper
    return img


def rgb333(v: int) -> tuple[int, int, int]:
    r, g, b = (v >> 6) & 7, (v >> 3) & 7, v & 7
    return (r * 255 // 7, g * 255 // 7, b * 255 // 7)


def render_layer2(data: bytes, palette: list[int], mode: int, transp: int,
                  pal_offset: int = 0, transparent: bool = False) -> Image.Image:
    """mode 0: 256x192 row-major 8bpp; 1: 320x256 column-major 8bpp;
    2: 640x256 column-major 4bpp."""
    w, h = {0: (256, 192), 1: (320, 256), 2: (640, 256)}[mode]
    img = Image.new("RGBA" if transparent else "RGB", (w, h))
    px = img.load()
    cols = [rgb333(c) for c in palette]
    hole = (0, 0, 0, 0) if transparent else (0xFF, 0, 0xFF)

    def put(x, y, idx):
        if idx == transp:
            px[x, y] = hole
        else:
            c = cols[(idx + pal_offset) & 0xFF] if mode != 2 else cols[((pal_offset & 0xF0) | idx) & 0xFF]
            px[x, y] = c + (255,) if transparent else c

    if mode == 0:
        for y in range(192):
            for x in range(256):
                put(x, y, data[y * 256 + x])
    elif mode == 1:
        for x in range(320):
            for y in range(256):
                put(x, y, data[x * 256 + y])
    else:
        for x in range(320):
            for y in range(256):
                b = data[x * 256 + y]
                put(2 * x, y, b >> 4)
                put(2 * x + 1, y, b & 15)
    return img


def render_sprites(attrs: bytes, patterns: bytes, palette: list[int], transp: int,
                   background=(0, 0, 0, 0)) -> Image.Image:
    """128 sprites x 5 attribute bytes, 16 KB patterns -> 320x256 RGBA plane.
    Sprite 0 is drawn first (lowest priority, as with NextReg 0x15 bit 6 = 0)."""
    img = Image.new("RGBA", (320, 256), background)
    px = img.load()
    cols = [rgb333(c) for c in palette]
    anchor = None
    for n in range(128):
        a = attrs[n * 5:n * 5 + 5]
        a0, a1, a2, a3, a4 = a
        visible = a3 & 0x80
        extended = a3 & 0x40
        if extended and (a4 & 0xC0) == 0x40:           # relative sprite (attr4 bits 7-6 = 01)
            if anchor is None:
                continue
            ax, ay, apal, avis, h8, apat = anchor
            x = ax + (a0 if a0 < 128 else a0 - 256)
            y = ay + (a1 if a1 < 128 else a1 - 256)
            pal_off = ((a2 >> 4) + apal) & 15 if a2 & 1 else a2 >> 4      # PO: relative palette
            mx, my, rot = (a2 >> 3) & 1, (a2 >> 2) & 1, (a2 >> 1) & 1
            visible = visible and avis
            n6 = (a4 >> 5) & 1 if not h8 else 0
            pattern = (a3 & 0x3F) * 2 + n6
            if a4 & 1:                                                    # PR: relative pattern
                pattern = (pattern + apat) & 0x7F
            xs, ys = 1 << ((a4 >> 3) & 3), 1 << ((a4 >> 1) & 3)          # composite: own scale
        else:
            x = a0 | ((a2 & 1) << 8)
            y = a1 | (((a4 & 1) << 8) if extended else 0)
            pal_off = a2 >> 4
            mx, my, rot = (a2 >> 3) & 1, (a2 >> 2) & 1, (a2 >> 1) & 1
            h8 = not (extended and a4 & 0x80)                # H=1 -> 4-bit pattern
            n6 = (a4 >> 6) & 1 if extended and not h8 else 0
            xs = 1 << ((a4 >> 3) & 3) if extended else 1
            ys = 1 << ((a4 >> 1) & 3) if extended else 1
            pattern = (a3 & 0x3F) * 2 + n6
            anchor = (x, y, pal_off, visible, h8, pattern)
        if not visible:
            continue
        for sy in range(16):
            for sx in range(16):
                # transform sprite-space (sx, sy) -> pattern pixel (u, v)
                u, v = sx, sy
                if mx:
                    u = 15 - u
                if my:
                    v = 15 - v
                if rot:
                    u, v = v, 15 - u
                if h8:
                    idx = patterns[((pattern >> 1) * 256 + v * 16 + u) & 0x3FFF]
                    if idx == transp:
                        continue
                    c = cols[(idx + (pal_off << 4)) & 0xFF]
                else:
                    byte = patterns[(pattern * 128 + v * 8 + u // 2) & 0x3FFF]
                    idx = byte >> 4 if u % 2 == 0 else byte & 15
                    if idx == (transp & 15):
                        continue
                    c = cols[(pal_off << 4) | idx]
                for dy in range(ys):
                    for dx in range(xs):
                        X = (x + sx * xs + dx) % 512
                        Y = (y + sy * ys + dy) % 512
                        if X < 320 and Y < 256:
                            px[X, Y] = c + (255,)
    return img
