"""The test picture for sdk l2ring.h (projects/ringtest): 700 x 520, a grid,
diagonal bands and blocks of blocks, so the columns and rows differ; 12 colours,
void 11. Writes gfx/bank_20.bin.. and src/picture.asm (from 8K page 40)."""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / "sdk" / "tools"))
import l2ring  # noqa: E402

W, H, PAGE = 700, 520, 40


def pixel(x, y):
    if x % 50 == 0 or y % 40 == 0:
        return 10                                    # the grid
    if (x // 100 + y // 70) % 5 == 0:
        return 1 + (x // 25 + y // 20) % 7           # blocks of small blocks
    return ((x + 2 * y) // 41) % 9                   # bands


def picture():
    return [[pixel(x, y) for x in range(W)] for y in range(H)]


PAL = [(r << 6) | (g << 3) | b for r, g, b in
       [(0, 0, 2), (7, 0, 0), (0, 7, 0), (0, 0, 7), (7, 7, 0), (0, 7, 7), (7, 0, 7), (4, 4, 4), (2, 5, 1), (5, 2, 1),
        (7, 7, 7), (1, 1, 1)]]


def main():
    data = l2ring.encode(picture(), PAL, 11, 123, -45)
    asm = ["; made by tools/gen.py: the test picture (sdk/tools/l2ring.py)"]
    for k in range(0, len(data), 16384):
        bank = PAGE // 2 + k // 16384
        part = data[k:k + 16384]
        (HERE.parent / "gfx" / f"bank_{bank}.bin").write_bytes(part + bytes(16384 - len(part)))
        asm += [f"SECTION BANK_{bank}", f'BINARY "gfx/bank_{bank}.bin"']
    (HERE.parent / "src" / "picture.asm").write_text("\n".join(asm) + "\n")
    print(f"{len(data)} bytes from page {PAGE}")


if __name__ == "__main__":
    main()
