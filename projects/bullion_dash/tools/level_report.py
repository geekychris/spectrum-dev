"""Which of the original's levels can be finished at all (ORIGINAL_BUGS.md).

Reads src/levels.txt and checks, per level, two things a win needs, with a
movement model that is generous to the player (player.c's moves: walk with
support or on a bar/ladder, climb ladders, fall; plus dig a brick below-left /
right and drop through it; the guards are ignored, holes never close):

  - every gold can be reached from the start
  - row 0 can be reached from the start once the hidden ladders show

A level failing either can't be won. The counts are also checked for a ladder
(or hidden ladder) top that leads into row 0 at all.

    python3 tools/level_report.py [-v]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "sdk" / "tools"))
from levelpack import parse  # noqa: E402

E, B, S, L, R, G, X, P, EN, T = range(10)
W, H = 28, 16


def enter(t):
    return t in (E, G, L, R, X, T)


def solid(t):
    return t in (B, S, T)


def reach(g, start):
    def tile(x, y):
        return g[y][x] if 0 <= x < W and 0 <= y < H else S

    def support(x, y):
        return y >= H - 1 or tile(x, y) == L or solid(tile(x, y + 1)) or tile(x, y + 1) == L

    seen, todo = {start}, [start]
    while todo:
        x, y = todo.pop()
        t = tile(x, y)
        nb = []
        if t not in (L, R) and not support(x, y):
            nb = [(x, y + 1)]                               # falling
        else:
            for d in (-1, 1):
                if enter(tile(x + d, y)):
                    nb.append((x + d, y))
                    if tile(x + d, y + 1) == B:
                        nb.append((x + d, y + 1))           # dig and drop in
            if t == L and enter(tile(x, y - 1)):
                nb.append((x, y - 1))
            if (t == L or tile(x, y + 1) == L) and enter(tile(x, y + 1)):
                nb.append((x, y + 1))
        for n in nb:
            if n not in seen and 0 <= n[1] < H:
                seen.add(n)
                todo.append(n)
    return seen


w, h, rooms = parse((Path(__file__).resolve().parents[1] / "src" / "levels.txt").read_text())
gold_ok = top_ok = both = no_top = 0
winnable = []
for name, cells in rooms:
    g = [[cells[y * W + x] for x in range(W)] for y in range(H)]
    start = next((x, y) for y in range(H) for x in range(W) if g[y][x] == P)
    g = [[E if t in (P, EN) else t for t in row] for row in g]
    golds = [(x, y) for y in range(H) for x in range(W) if g[y][x] == G]
    a = all(c in reach(g, start) for c in golds)
    shown = [[L if t == X else t for t in row] for row in g]
    b = any(y == 0 for _, y in reach(shown, start))
    has_top = any(g[1][x] in (L, X) and enter(g[0][x]) for x in range(W))
    gold_ok += a
    top_ok += b
    no_top += not has_top
    if a and b:
        both += 1
        winnable.append(name)
    if "-v" in sys.argv:
        print(f"{name:10s} gold {'yes' if a else 'no ':3s} top {'yes' if b else 'no ':3s} ladder top {has_top}")
print(f"{len(rooms)} levels")
print(f"  no ladder top into row 0 at all:            {no_top}")
print(f"  every gold reachable from the start:        {gold_ok}")
print(f"  row 0 reachable from the start:             {top_ok}")
print(f"  both (can perhaps be won, guards aside):    {both} {winnable}")
