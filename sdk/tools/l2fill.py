"""Host model of sdk/src/next/l2fill.asm, pixel for pixel: what l2f_poly
draws, for host pictures that must match the Next's (Marble Tilt's board,
projects/fixfilltest).

    from l2fill import fill
    fill(canvas, [(x, y), ...], colour, top=0, bot=192)

canvas: 192 rows of 256 values (lists), changed in place. Each edge is
stepped from its top vertex in 16.8 fixed point (x + 0.5 at the top; the
slope |dx| * (65536 // dy) >> 8 for dy < 256, (|dx| << 8) // dy above),
rows ytop <= y < ybot, floor(x) at both ends of each row, clipped to the
screen and to rows top..bot-1.
"""


def fill(canvas, pts, colour, top=0, bot=192):
    n = len(pts)
    tab = {}
    ys = [p[1] for p in pts]
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        if y0 == y1:
            continue
        col = 0 if y0 < y1 else 1
        (tx, ty), (bx, by) = ((x0, y0), (x1, y1)) if y0 < y1 else ((x1, y1), (x0, y0))
        if by <= 0 or ty >= 192:
            continue
        dy, dx = by - ty, bx - tx
        if dy == 1:
            mag = abs(dx) * 256
        elif dy < 256:
            mag = (abs(dx) * (65536 // dy)) >> 8
        else:
            mag = (abs(dx) << 8) // dy
        slope = -mag if dx < 0 else mag
        x = tx * 256 + 128
        while ty < 0:
            x += slope
            ty += 1
        for y in range(ty, min(by, 192)):
            tab.setdefault(y, [None, None])[col] = x >> 8
            x += slope
    for y in range(max(min(ys), top), min(max(ys), bot)):
        a, b = tab[y]
        lo, hi = min(a, b), max(a, b)
        if hi < 0 or lo > 255:
            continue
        row = canvas[y]
        for x in range(max(lo, 0), min(hi, 255) + 1):
            row[x] = colour
