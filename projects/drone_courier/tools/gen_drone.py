#!/usr/bin/env python3
"""The drone as hardware sprites (port: the original draws it as 10-13
polygons among the city's, render.c drone()).

The chase camera is always 170 behind the drone (along the camera's
heading) and 70 above it, so the drone's picture depends only on the
camera's pitch, the drone's heading against the camera's (it eases after
it), whether it carries a parcel, and the rotors' size this tick. The model
is symmetric front to back and side to side, so headings 0..90 degrees (5
steps of 22.5) cover the circle: the rest are the same picture or its
mirror. Each picture is rendered here from render.c's own quads, its depth
order and pens, at the Next's view scale (4/5 of the original's, full
rows), into an 80 x 32 box (10 sprites, 4 bits a pixel) with the drone's
middle at (40, 19).

  gfx/bank_25.bin ..   frames of 1280 bytes (10 x 16x16 4-bit patterns,
                       row by row), 6 to an 8K page from page 50
  src/drone_data.h     the layout, the pitch steps

frame index = ((pitch * 5 + heading) * 2 + parcel) * 2 + rotor phase
pens: 3 transparent (NextReg 0x4B's low nibble), 1 the body's top (17),
2 its ends (18), 4 rotors (19), 5 the parcel (20; palette offset +1: 21)
"""
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
FIRST_BANK = 25
W, H, CX, CY = 80, 32, 40, 19
PITCHES = [1200 + 1000 * k for k in range(8)] + [9000]    # camera pitch steps (16-bit angle), nearest
HEADINGS = 5
FOV = 160                                                 # view.c FOV (the original's 200 at 4/5)
TR, P_TOP, P_END, P_ROTOR, P_PARCEL = 3, 1, 2, 4, 5
FRAME = W * H // 2


def quads(rel, carrying, phase):
    """render.c drone(), around the drone's middle: (points, bias, pen)"""
    fx, fz = math.sin(rel), math.cos(rel)
    rx, rz = fz, -fx
    P = lambda a, b, y: (fx * a + rx * b, y, fz * a + rz * b)
    q = [([P(-10, -8, 4), P(-10, 8, 4), P(10, 8, 4), P(10, -8, 4)], -2, P_TOP),
         ([P(10, -8, 0), P(10, 8, 0), P(10, 8, 4), P(10, -8, 4)], -1, P_END),
         ([P(-10, -8, 0), P(-10, 8, 0), P(-10, 8, 4), P(-10, -8, 4)], -1, P_END)]
    for k in range(4):
        a, b, s = (16 if k & 1 else -16), (16 if k & 2 else -16), 7 + ((phase + k) & 1)
        q.append(([P(a - s, b - s, 6), P(a - s, b + s, 6), P(a + s, b + s, 6), P(a + s, b - s, 6)], -3, P_ROTOR))
    if carrying:
        q += [([P(-6, -6, -9), P(-6, 6, -9), P(6, 6, -9), P(6, -6, -9)], -1, P_PARCEL),
              ([P(-6, -6, -9), P(-6, 6, -9), P(-6, 6, 0), P(-6, -6, 0)], 0, P_PARCEL),
              ([P(6, -6, -9), P(6, 6, -9), P(6, 6, 0), P(6, -6, 0)], 0, P_PARCEL)]
    return q


def render(pitch, heading, carrying, phase):
    p = pitch * 2 * math.pi / 65536
    cp, sp = math.cos(p), math.sin(p)

    def cam(v):                        # camera 170 behind, 70 above, its heading 0
        dx, dy, dz = v[0], v[1] - 70, v[2] + 170
        return dx, dy * cp + dz * sp, dz * cp - dy * sp
    c = cam((0, 0, 0))
    ox, oy = FOV * c[0] / c[2], -FOV * c[1] / c[2]
    S = 4                              # supersampled, then the pen most of each pixel has
    img = Image.new("P", (W * S, H * S), TR)
    d = ImageDraw.Draw(img)
    polys = []
    for i, (pts, bias, pen) in enumerate(quads(heading * math.pi / 8, carrying, phase)):
        v = [cam(q) for q in pts]
        depth = sum(q[2] for q in v) / 4 + bias
        xy = [((FOV * q[0] / q[2] - ox + CX) * S, (-FOV * q[1] / q[2] - oy + CY) * S) for q in v]
        polys.append((-depth, i, pen, xy))
    for _, _, pen, xy in sorted(polys):
        d.polygon(xy, fill=pen)
    out = Image.new("P", (W, H), TR)
    for y in range(H):
        for x in range(W):
            counts = {}
            for yy in range(S):
                for xx in range(S):
                    q = img.getpixel((x * S + xx, y * S + yy))
                    counts[q] = counts.get(q, 0) + 1
            best = max(counts, key=lambda k: (counts[k], k != TR))
            out.putpixel((x, y), best if counts[best] * 2 >= S * S or best != TR else TR)
    return out


def frame_bytes(img):
    """10 4-bit 16 x 16 patterns: sprite (row, col) = (k // 5, k % 5)"""
    b = bytearray()
    for k in range(10):
        r, c = divmod(k, 5)
        for y in range(16):
            for x in range(0, 16, 2):
                hi = img.getpixel((c * 16 + x, r * 16 + y))
                lo = img.getpixel((c * 16 + x + 1, r * 16 + y))
                b.append(hi << 4 | lo)
    assert len(b) == FRAME
    return bytes(b)


def main():
    frames = []
    for pitch in PITCHES:
        for heading in range(HEADINGS):
            for carrying in (0, 1):
                for phase in (0, 1):
                    frames.append(render(pitch, heading, carrying, phase))
    pages = []
    for i in range(0, len(frames), 6):
        pages.append(b"".join(frame_bytes(f) for f in frames[i:i + 6]).ljust(8192, b"\0"))
    if len(pages) & 1:
        pages.append(bytes(8192))
    asm = ["; made by tools/gen_drone.py: the drone's sprite frames, into banks by the .nex", ""]
    gfx = os.path.join(PROJ, "gfx")
    for i in range(0, len(pages), 2):
        bank = FIRST_BANK + i // 2
        with open(os.path.join(gfx, f"bank_{bank}.bin"), "wb") as f:
            f.write(pages[i] + pages[i + 1])
        asm += [f"SECTION BANK_{bank}", f'BINARY "gfx/bank_{bank}.bin"']
    with open(os.path.join(PROJ, "src", "drone_pages.asm"), "w") as f:
        f.write("\n".join(asm) + "\n")
    last = FIRST_BANK + len(pages) // 2 - 1
    with open(os.path.join(PROJ, "src", "drone_data.h"), "w") as f:
        f.write(f'''/* made by tools/gen_drone.py: the drone's sprite frames (see there) */
#ifndef DRONE_DATA_H
#define DRONE_DATA_H
#define DRONE_PAGE     {FIRST_BANK * 2}      /* frames from here, 6 to a page */
#define DRONE_LAST_BANK {last}
#define DRONE_FRAME    {FRAME}    /* bytes: 10 4-bit patterns */
#define DRONE_W        {W}
#define DRONE_H        {H}
#define DRONE_CX       {CX}       /* the drone's middle in the box */
#define DRONE_CY       {CY}
#define DRONE_PITCHES  {len(PITCHES)}
#define DRONE_HEADINGS {HEADINGS}
#define DRONE_PITCH0   {PITCHES[0]}      /* pitch steps: DRONE_PITCH0 + 1000 k, then 9000 */
#endif
''')
    sheet = Image.new("P", (W * 10, H * len(PITCHES)), TR)
    for pi in range(len(PITCHES)):
        for h in range(HEADINGS):
            sheet.paste(frames[((pi * HEADINGS + h) * 2 + 1) * 2], (h * W * 2, pi * H))
            sheet.paste(frames[((pi * HEADINGS + h) * 2) * 2 + 1], (h * W * 2 + W, pi * H))
    sheet.putpalette([0, 0, 0, 238, 238, 238, 153, 153, 153, 40, 60, 90, 68, 68, 68, 187, 136, 85] + [0] * 750)
    os.makedirs(os.path.join(PROJ, "build"), exist_ok=True)
    sheet.convert("RGB").resize((W * 20, H * len(PITCHES) * 2), Image.NEAREST).save(os.path.join(PROJ, "build", "drone_sheet.png"))
    print(f"{len(frames)} frames, {len(pages)} pages, banks {FIRST_BANK}..{last}")


if __name__ == "__main__":
    main()
