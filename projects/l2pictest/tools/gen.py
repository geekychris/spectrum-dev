#!/usr/bin/env python3
"""l2pictest's picture: 1000 columns, each rows 16..255 in runs whose
lengths and colours depend on the column, so every column differs
(test.py makes the same columns to check Layer 2 against). Packed with
sdk/tools/l2pic.py into 8K pages from page 32 (the index in page 32).

    python3 tools/gen.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PROJ, "..", "..", "sdk", "tools"))
from l2pic import pack  # noqa: E402

NCOLS, ROW0, INDEX_PAGE = 1000, 16, 32


def column(x):
    col, y, k = [0] * 256, 0, 0
    while y < 256:
        n = 4 + (x * 7 + k * 13) % 29
        c = (x * 31 + k * 17) % 254 + 1
        for i in range(y, min(256, y + n)):
            col[i] = c
        y += n
        k += 1
    return col


def main():
    pages, index = pack([column(x) for x in range(NCOLS)], ROW0, INDEX_PAGE + 1)
    pages = [index] + pages
    out = ["; made by tools/gen.py: the index and the column RLE, page by page"]
    for i, p in enumerate(pages):
        open(os.path.join(PROJ, "gfx", f"page_{INDEX_PAGE + i}.bin"), "wb").write(p)
        out += [f"SECTION PAGE_{INDEX_PAGE + i}", f'BINARY "gfx/page_{INDEX_PAGE + i}.bin"']
    open(os.path.join(PROJ, "src", "pic.asm"), "w").write("\n".join(out) + "\n")
    open(os.path.join(PROJ, "zpragma.inc"), "w").write(
        "// made by tools/gen.py: the picture's pages at 0x4000\n"
        + "".join(f"#pragma output CRT_ORG_PAGE_{INDEX_PAGE + i} = 0x{(INDEX_PAGE + i) << 16 | 0x4000:06X}\n"
                  for i in range(len(pages))))
    print(f"{NCOLS} columns, {sum(len(p) for p in pages[1:])} bytes in {len(pages) - 1} pages + the index")


if __name__ == "__main__":
    main()
