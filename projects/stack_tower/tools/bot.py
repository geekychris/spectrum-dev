"""A Stack Tower player for the tests: reads the game through its probe
(src/game.c st_probe) and the board (the first 384 bytes of `game`), picks
where the piece goes and presses the keys a player would (X / Z to rotate,
O / P to turn the tower, SPACE to drop).

Its choice: every rotation and column the piece can reach, dropped from
where it is, scored on the rings it completes, the holes it leaves under
itself, how high it lands and how uneven the tower gets.
"""
import struct

COLS, ROWS, TOTAL = 16, 20, 24
CELLS = [
    [(-1, 0), (0, 0), (1, 0)],                              # rod
    [(0, 0), (1, 0), (0, 1)],                               # elbow
    [(-1, 1), (-1, 0), (0, 0), (1, 0), (1, 1)],             # cup
    [(0, -1), (0, 0), (1, 0), (0, 1), (1, 1)],              # flag
    [(0, -1), (-1, 0), (0, 0), (1, 0), (0, 1)],             # star
    [(-1, 1), (0, 1), (1, 1), (0, 0), (0, -1)],             # anchor
    [(-1, 1), (-1, 0), (0, 0), (0, -1), (1, -1)],           # stairs
    [(-1, 1), (0, 1), (0, 0), (0, -1), (1, -1)],            # bolt
]
PROBE_FMT = "<BBBBbbbbBBBBlHHbbHBB"
PROBE_SIZE = struct.calcsize(PROBE_FMT)
FIELDS = ("state", "mode", "over", "won", "ptype", "prot", "pcol", "prow", "level", "help", "paused", "fps",
          "score", "pieces", "lines", "hold", "rank", "view", "sel_mode", "sel_level")
TITLE, PLAY, OVER, NAME = range(4)


def decode(raw):
    return dict(zip(FIELDS, struct.unpack(PROBE_FMT, raw[:PROBE_SIZE])))


def cells_of(ptype, rot, col, row):
    out = []
    for x, y in CELLS[ptype]:
        for _ in range(rot & 3):
            x, y = y, -x
        out.append(((col + x) % COLS, row + y))
    return out


def collides(board, cells):
    return any(r < 0 or r >= TOTAL or board[r * COLS + c] for c, r in cells)


def heights(board):
    h = [0] * COLS
    for c in range(COLS):
        for r in range(TOTAL - 1, -1, -1):
            if board[r * COLS + c]:
                h[c] = r + 1
                break
    return h


def evaluate(board, cells):
    b = bytearray(board)
    for c, r in cells:
        b[r * COLS + c] = 1
    full = [r for r in range(TOTAL) if all(b[r * COLS + c] for c in range(COLS))]
    keep = [r for r in range(TOTAL) if r not in full]
    nb = bytearray(COLS * TOTAL)
    for k, r in enumerate(keep):
        nb[k * COLS:(k + 1) * COLS] = b[r * COLS:(r + 1) * COLS]
    h = heights(nb)
    holes = sum(1 for c in range(COLS) for r in range(h[c]) if not nb[r * COLS + c])
    bump = sum(abs(h[c] - h[(c + 1) % COLS]) for c in range(COLS))
    land = max(r for _, r in cells)
    return 8 * len(full) - 5 * holes - 0.5 * bump - 1.0 * land - 0.3 * max(h)


def choose(board, p):
    """(rotation, column) for the piece in probe p"""
    best = None
    for rot in range(4):
        for col in range(COLS):
            cells = cells_of(p["ptype"], rot, col, p["prow"])
            if collides(board, cells):
                continue
            row = p["prow"]
            while not collides(board, cells_of(p["ptype"], rot, col, row - 1)):
                row -= 1
            cells = cells_of(p["ptype"], rot, col, row)
            if all(r >= ROWS for _, r in cells):
                continue
            score = evaluate(board, cells)
            if best is None or score > best[0]:
                best = (score, rot, col)
    return (best[1], best[2]) if best else (p["prot"], p["pcol"])


class Bot:
    def __init__(self, e):
        self.e = e

    def probe(self):
        return decode(self.e.read_mem("st_probe", PROBE_SIZE))

    def board(self):
        return self.e.read_mem("game", COLS * TOTAL)

    def settled(self, frames=12):
        """the probe once the game has taken the last key (it is written once
        a game loop, and a busy loop takes a few frames)"""
        p = self.probe()
        for _ in range(frames):
            self.e.run(1)
            q = self.probe()
            if q == p:
                return q
            p = q
        return p

    def place(self, rot=None, col=None):
        """one piece: rotate, turn, drop; returns the probe after it"""
        e = self.e
        p = self.probe()
        if rot is None:
            rot, col = choose(self.board(), p)
        d = (rot - p["prot"]) % 4
        for key in {1: "X", 2: "X X", 3: "Z"}.get(d, "").split():
            e.press(key)
        for _ in range(12):
            p = self.settled()
            if p["state"] != PLAY:
                return p
            d = (col - p["pcol"]) % COLS
            if d == 0:
                break
            e.press("P" if d <= 8 else "O")
        pieces = p["pieces"]
        e.press("SPACE")
        for _ in range(20):
            p = self.probe()
            if p["pieces"] != pieces or p["state"] != PLAY:
                break
            e.run(1)
        return p
