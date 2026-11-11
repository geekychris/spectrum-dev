"""The original's 150 levels (amiga_mcp/examples/bullion_dash/level.c:
nibble-packed, column-major) -> src/levels_original.txt, a levelpack/gridpack
level file in the original's own text format (level_save_text: . # @ H - $ X P
E T). These are the levels as released; the game uses the repaired ones in
src/levels.txt (REPAIRED_LEVELS.md). With the repaired level.c it gives those.

    python3 tools/extract_levels.py [path/to/level.c]
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else \
    HERE.parents[2] / "amiga_mcp" / "examples" / "bullion_dash" / "level.c"
CHARS = ".#@H-$XPET"           # TILE_EMPTY .. TILE_TRAP (game.h)
COLS, ROWS = 28, 16

text = SRC.read_text()
body = text[text.index("level_data[150 * 224] = {"):]
body = body[:body.index("};")]
vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
assert len(vals) == 150 * 224, len(vals)
out = ["; Bullion Dash: the original's 150 levels as released (made by tools/extract_levels.py); 149 can't be won:",
       "; ORIGINAL_BUGS.md. The game uses the repaired ones in levels.txt.",
       f"size {COLS} {ROWS}",
       "legend " + "  ".join(f"{c} {i}" for i, c in enumerate(CHARS))]
for lv in range(150):
    p = vals[lv * 224:(lv + 1) * 224]
    g = [[0] * COLS for _ in range(ROWS)]
    i = 0
    for x in range(COLS):              # unpack_level(): column-major, high nibble = even row
        for y in range(0, ROWS, 2):
            g[y][x], g[y + 1][x] = p[i] >> 4, p[i] & 15
            i += 1
    out.append(f"room LEVEL {lv + 1}")
    out += ["".join(CHARS[t] for t in row) for row in g]
(HERE / "src" / "levels_original.txt").write_text("\n".join(out) + "\n")
print("150 levels -> src/levels_original.txt")
